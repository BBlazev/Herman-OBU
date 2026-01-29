#include "validator/nfc_reader.hpp"
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <poll.h>

namespace validator {

NfcReader::NfcReader(const char* port) : port_(port) {}

NfcReader::~NfcReader()
{
    stop();
    serial_.close();
}

Result<bool> NfcReader::configure_serial()
{
    int fd = ::open(port_.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        last_error_ = "Failed to open port";
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    
    if (tcgetattr(fd, &tty) != 0) {
        ::close(fd);
        last_error_ = "Failed to get port attributes";
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    cfsetospeed(&tty, B921600);
    cfsetispeed(&tty, B921600);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        ::close(fd);
        last_error_ = "Failed to set port attributes";
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    tcflush(fd, TCIOFLUSH);
    ::close(fd);
    
    auto result = serial_.open(port_);
    if (!result.ok()) {
        last_error_ = "Failed to open port";
        return Result<bool>::failure(result.error());
    }
    
    return Result<bool>::success(true);
}

Result<bool> NfcReader::send_command(uint8_t cmd, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> frame;
    frame.push_back(ADDR_REQ);
    frame.push_back(cmd);
    frame.push_back(counter_++);
    
    if (!data.empty()) {
        frame.insert(frame.end(), data.begin(), data.end());
    }
    
    auto result = serial_.write(frame.data(), frame.size());
    if (!result.ok()) {
        last_error_ = "Failed to send command";
        return Result<bool>::failure(result.error());
    }
    
    return Result<bool>::success(true);
}

Result<std::vector<uint8_t>> NfcReader::read_response(int timeout_ms)
{
    serial_.set_timeout_ms(timeout_ms);
    bool running = true;
    return serial_.read(running);
}

Result<bool> NfcReader::authenticate()
{
    auto send_result = send_command(CMD_AUTH_A);
    if (!send_result.ok()) {
        return Result<bool>::failure(send_result.error());
    }
    
    auto resp_result = read_response(1000);
    
    std::vector<uint8_t> key;
    if (resp_result.ok() && resp_result.value().size() >= 4) {
        auto& resp = resp_result.value();
        key.assign(resp.begin() + 4, resp.end());
    }
    
    send_result = send_command(CMD_AUTH_B, key);
    if (!send_result.ok()) {
        return Result<bool>::failure(send_result.error());
    }
    
    read_response(1000);
    return Result<bool>::success(true);
}

Result<bool> NfcReader::enable_reading()
{
    return send_command(CMD_ENABLE);
}

Result<bool> NfcReader::initialize()
{
    if (initialized_.load()) {
        return Result<bool>::success(true);
    }
    
    auto config_result = configure_serial();
    if (!config_result.ok()) {
        return Result<bool>::failure(config_result.error());
    }
    
    auto auth_result = authenticate();
    if (!auth_result.ok()) {
        last_error_ = "Authentication failed";
        return Result<bool>::failure(auth_result.error());
    }
    
    initialized_.store(true);
    return Result<bool>::success(true);
}

std::string NfcReader::bytes_to_hex(const std::vector<uint8_t>& data)
{
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (uint8_t byte : data) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::optional<NfcCardInfo> NfcReader::parse_card_info(const std::vector<uint8_t>& frame)
{
    if (frame.size() < 10) {
        return std::nullopt;
    }
    
    size_t start = 0;
    while (start + 1 < frame.size()) {
        if (frame[start] == 0x10 && frame[start + 1] == 0x16) {
            break;
        }
        ++start;
    }
    
    if (start + 1 >= frame.size()) {
        return std::nullopt;
    }
    
    size_t data_start = start + 2;
    size_t etx_pos = frame.size();
    bool etx_found = false;
    
    for (size_t i = data_start; i + 1 < frame.size(); ++i) {
        if (frame[i] == 0x10 && frame[i + 1] == 0x03) {
            etx_pos = i;
            etx_found = true;
            break;
        }
    }
    
    if (!etx_found || etx_pos + 4 > frame.size()) {
        return std::nullopt;
    }
    
    std::vector<uint8_t> payload;
    payload.reserve(etx_pos - data_start);
    
    for (size_t i = data_start; i < etx_pos; ++i) {
        uint8_t byte = frame[i];
        if (byte == 0x10) {
            if (i + 1 >= etx_pos || frame[i + 1] != 0x10) {
                return std::nullopt;
            }
            payload.push_back(0x10);
            ++i;
            continue;
        }
        payload.push_back(byte);
    }
    
    if (payload.size() < 6) {
        return std::nullopt;
    }
    
    size_t pos = 0;
    pos++; // dest_addr
    uint8_t service = payload[pos++];
    pos++; // counter
    pos++; // source_addr
    uint8_t ack = payload[pos++];
    
    if (service != SERVICE_READ_CARD || ack != 0) {
        return std::nullopt;
    }
    
    if (payload.size() - pos < 13) {
        return std::nullopt;
    }
    
    NfcCardInfo info;
    
    info.atqa = static_cast<uint16_t>((static_cast<uint16_t>(payload[pos]) << 8) | payload[pos + 1]);
    pos += 2;
    
    info.ct = payload[pos++];
    
    std::vector<uint8_t> uid;
    uid.insert(uid.end(), payload.begin() + pos, payload.begin() + pos + 3);
    pos += 3;
    
    info.bcc1 = payload[pos++];
    
    uid.insert(uid.end(), payload.begin() + pos, payload.begin() + pos + 4);
    pos += 4;
    
    info.bcc2 = payload[pos++];
    info.sak = payload[pos++];
    
    info.uid_hex = bytes_to_hex(uid);
    
    if (payload.size() > pos) {
        info.extra.assign(payload.begin() + pos, payload.end());
    }
    
    return info;
}

Result<bool> NfcReader::start_reading()
{
    if (!initialized_.load()) {
        auto init_result = initialize();
        if (!init_result.ok()) {
            return Result<bool>::failure(init_result.error());
        }
    }
    
    running_.store(true);
    std::vector<uint8_t> frame_buffer;
    
    while (running_.load()) {
        auto enable_result = enable_reading();
        if (!enable_result.ok()) {
            last_error_ = "Failed to enable reading";
            running_.store(false);
            return Result<bool>::failure(enable_result.error());
        }
        
        frame_buffer.clear();
        
        while (running_.load()) {
            serial_.set_timeout_ms(100);
            bool run_flag = running_.load();
            auto read_result = serial_.read(run_flag);
            
            if (!read_result.ok()) {
                if (read_result.error() == Error::TIMEOUT) {
                    continue;
                }
                last_error_ = "Read error";
                running_.store(false);
                return Result<bool>::failure(read_result.error());
            }
            
            auto& data = read_result.value();
            frame_buffer.insert(frame_buffer.end(), data.begin(), data.end());
            
            auto card_info = parse_card_info(frame_buffer);
            if (card_info.has_value()) {
                if (card_callback_) {
                    card_callback_(card_info.value());
                }
                break;
            }
            
            if (frame_buffer.size() > 1024) {
                frame_buffer.clear();
            }
        }
    }
    
    return Result<bool>::success(true);
}

Result<NfcCardInfo> NfcReader::read_single_card(int timeout_ms)
{
    if (!initialized_.load()) {
        auto init_result = initialize();
        if (!init_result.ok()) {
            return Result<NfcCardInfo>::failure(init_result.error());
        }
    }
    
    auto enable_result = enable_reading();
    if (!enable_result.ok()) {
        return Result<NfcCardInfo>::failure(enable_result.error());
    }
    
    std::vector<uint8_t> frame_buffer;
    int elapsed = 0;
    constexpr int poll_interval = 100;
    
    while (timeout_ms == 0 || elapsed < timeout_ms) {
        serial_.set_timeout_ms(poll_interval);
        bool running = true;
        auto read_result = serial_.read(running);
        
        if (read_result.ok()) {
            auto& data = read_result.value();
            frame_buffer.insert(frame_buffer.end(), data.begin(), data.end());
            
            auto card_info = parse_card_info(frame_buffer);
            if (card_info.has_value()) {
                return Result<NfcCardInfo>::success(card_info.value());
            }
        }
        
        elapsed += poll_interval;
        
        if (frame_buffer.size() > 1024) {
            frame_buffer.clear();
        }
    }
    
    last_error_ = "Timeout waiting for card";
    return Result<NfcCardInfo>::failure(Error::TIMEOUT);
}

}
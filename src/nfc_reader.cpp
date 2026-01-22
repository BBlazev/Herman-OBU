#include "devices/nfc_reader.hpp"
#include "common/helpers.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cerrno>
#include <poll.h>

void Nfc_reader::log(const std::string& msg)
{
    if (log_callback_) {
        log_callback_(msg);
    }
}

Nfc_reader::Nfc_reader(const char* device, speed_t baud)
{
    fd_ = ::open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
        init_error_ = std::string("Failed to open ") + device + ": " + strerror(errno);
        return;
    }
    
    int flags = fcntl(fd_, F_GETFL, 0);
    fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);
    
    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        init_error_ = std::string("tcgetattr failed: ") + strerror(errno);
        ::close(fd_);
        fd_ = -1;
        return;
    }
    
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);
    cfmakeraw(&tty);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;  // 100ms timeout

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        init_error_ = std::string("tcsetattr failed: ") + strerror(errno);
        ::close(fd_);
        fd_ = -1;
        return;
    }

    tcflush(fd_, TCIOFLUSH);
    init_error_ = "";
}

Nfc_reader::~Nfc_reader()
{
    stop();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Nfc_reader::do_auth()
{
    log("[NFC] AUTH PART A");
    send_command(nfc::Command::AuthA);
    auto result = read_response(16);

    std::vector<uint8_t> key;
    if (result.ok() && result.value().size() >= 4) {
        key.assign(result.value().begin() + 4, result.value().end());
        log("[NFC] Got key: " + std::to_string(key.size()) + " bytes");
    } else {
        log("[NFC] No valid Auth Key, continuing with empty key");
    }

    log("[NFC] AUTH PART B");
    send_command(nfc::Command::AuthB, key);
    auto result2 = read_response(64);

    log("[NFC] AUTH done");
    return true;
}

void Nfc_reader::initialize()
{
    if (fd_ < 0) {
        log("[NFC] Cannot init - port not open: " + init_error_);
        return;
    }
    
    if (initialized_.load()) {
        log("[NFC] Already initialized");
        return;
    }
    
    if (do_auth()) {
        initialized_.store(true);
        log("[NFC] Initialized OK");
    } else {
        log("[NFC] Init failed");
    }
}

Result<bool> Nfc_reader::send_command(nfc::Command cmd, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> frame;
    frame.reserve(3 + data.size());
    frame.push_back(static_cast<uint8_t>(nfc::Command::AddrReq));
    frame.push_back(static_cast<uint8_t>(cmd));
    frame.push_back(counter_++);
    frame.insert(frame.end(), data.begin(), data.end());

    std::ostringstream oss;
    oss << "[NFC] TX: ";
    for (uint8_t b : frame) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b) << " ";
    }
    log(oss.str());

    ssize_t nw = ::write(fd_, frame.data(), frame.size());
    if (nw < 0 || static_cast<size_t>(nw) != frame.size())
        return Result<bool>::failure(Error::CMD_FAILURE);

    return Result<bool>::success(true);
}

Result<std::vector<uint8_t>> Nfc_reader::read_response(size_t len)
{
    struct pollfd pfd = {fd_, POLLIN, 0};
    int ret = poll(&pfd, 1, 1000);  // 1 sec timeout
    
    if (ret <= 0) {
        log("[NFC] RX: timeout");
        return Result<std::vector<uint8_t>>::success(std::vector<uint8_t>());
    }
    
    std::vector<uint8_t> buffer(len);
    ssize_t n = ::read(fd_, buffer.data(), buffer.size());
    
    if (n > 0) {
        buffer.resize(static_cast<size_t>(n));
        std::ostringstream oss;
        oss << "[NFC] RX: ";
        for (uint8_t b : buffer) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b) << " ";
        }
        log(oss.str());
    } else {
        buffer.clear();
        log("[NFC] RX: empty");
    }
    
    return Result<std::vector<uint8_t>>::success(std::move(buffer));
}

void Nfc_reader::start()
{
    if (!initialized_.load()) {
        log("[NFC] Cannot start - not initialized");
        return;
    }
    
    running_.store(true);
    log("[NFC] Started - waiting for cards");
    
    while (running_.load()) 
    {
        log("[NFC] ENABLE CARD READING");
        send_command(nfc::Command::Enable);

        std::vector<uint8_t> frameBuffer;

        while (running_.load()) {
            struct pollfd pfd = {fd_, POLLIN, 0};
            int ret = poll(&pfd, 1, 100);  // 100ms timeout - allows checking running_ flag
            
            if (ret < 0) {
                log("[NFC] Poll error: " + std::string(strerror(errno)));
                break;
            }
            
            if (ret == 0) {
                continue;  // Timeout, check running_ and loop
            }
            
            uint8_t b = 0;
            ssize_t bytesRead = ::read(fd_, &b, 1);
            
            if (bytesRead > 0) {
                frameBuffer.push_back(b);

                auto parsed = parseCardInfo(frameBuffer);
                if (parsed && parsed->service == static_cast<uint8_t>(nfc::Command::ReadCard) 
                    && parsed->ack == 0) 
                {
                    std::ostringstream oss;
                    oss << "[NFC] Card UID=" << parsed->uidHex
                        << " ATQA=0x" << std::hex << std::setw(4) << std::setfill('0') << parsed->atqa
                        << " SAK=0x" << std::setw(2) << static_cast<int>(parsed->sak);
                    log(oss.str());
                    
                    if (card_callback_) {
                        card_callback_(*parsed);
                    }
                    frameBuffer.clear();
                    break;
                }
            } 
            else if (bytesRead < 0 && errno != EAGAIN) {
                log("[NFC] Read error: " + std::string(strerror(errno)));
                break;
            }
        }
    }
    
    log("[NFC] Stopped");
}
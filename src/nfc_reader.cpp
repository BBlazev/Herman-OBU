#include "devices/nfc_reader.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <sstream>
#include <iomanip>

void Nfc_reader::log(const std::string& msg)
{
    if (log_callback_) {
        log_callback_(msg);
    }
}

Nfc_reader::Nfc_reader(const char* device)
{
    auto result = init(device);
    if (!result.ok()) {
        log("[NFC] Init failed: " + std::string(strerror(errno)));
    }
}

Nfc_reader::~Nfc_reader()
{
    stop();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

Result<bool> Nfc_reader::init(const char* device)
{
    fd_ = ::open(device, O_RDWR | O_NOCTTY);
    if (fd_ < 0)
        return Result<bool>::failure(Error::NFC_INIT);
    
    struct termios tty{};
    if (tcgetattr(fd_, &tty) != 0) {
        ::close(fd_);
        fd_ = -1;
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    cfsetispeed(&tty, B921600);
    cfsetospeed(&tty, B921600);
    cfmakeraw(&tty);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        ::close(fd_);
        fd_ = -1;
        return Result<bool>::failure(Error::PORT_ERROR);
    }

    tcflush(fd_, TCIOFLUSH);
    
    auto auth_result = auth();
    initialized_.store(auth_result.ok());
    return auth_result;
}

Result<bool> Nfc_reader::send_command(nfc::Command cmd, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> frame;
    frame.reserve(3 + data.size());
    frame.push_back(static_cast<uint8_t>(nfc::Command::AddrReq));
    frame.push_back(static_cast<uint8_t>(cmd));
    frame.push_back(counter_++);
    frame.insert(frame.end(), data.begin(), data.end());

    ssize_t nw = ::write(fd_, frame.data(), frame.size());
    if (nw < 0 || static_cast<size_t>(nw) != frame.size())
        return Result<bool>::failure(Error::CMD_FAILURE);

    return Result<bool>::success(true);
}

Result<std::vector<uint8_t>> Nfc_reader::read_response(size_t len)
{
    std::vector<uint8_t> buffer(len);
    ssize_t n = ::read(fd_, buffer.data(), buffer.size());
    
    if (n < 0)
        return Result<std::vector<uint8_t>>::failure(Error::READ_ERROR);
    
    buffer.resize(static_cast<size_t>(n));
    return Result<std::vector<uint8_t>>::success(std::move(buffer));
}

Result<bool> Nfc_reader::auth()
{
    log("[NFC] AUTH PART A");
    auto send_result = send_command(nfc::Command::AuthA);
    if (!send_result.ok()) {
        log("[NFC] AUTH A send failed");
        return Result<bool>::failure(Error::CMD_FAILURE);
    }
    
    auto result = read_response(16);
    if (!result.ok()) {
        log("[NFC] AUTH A read failed");
        return Result<bool>::failure(Error::READ_ERROR);
    }

    std::vector<uint8_t> key;
    if (result.value().size() >= 4) {
        key.assign(result.value().begin() + 4, result.value().end());
        log("[NFC] Got auth key, " + std::to_string(key.size()) + " bytes");
    } else {
        log("[NFC] No valid Auth Key, continuing with empty key");
    }

    log("[NFC] AUTH PART B");
    send_result = send_command(nfc::Command::AuthB, key);
    if (!send_result.ok()) {
        log("[NFC] AUTH B send failed");
        return Result<bool>::failure(Error::CMD_FAILURE);
    }
    
    auto result2 = read_response(64);
    if (!result2.ok()) {
        log("[NFC] AUTH B read failed");
        return Result<bool>::failure(Error::READ_ERROR);
    }

    if (!result.value().empty() && !result2.value().empty()) {
        log("[NFC] AUTH failed - unexpected response");
        return Result<bool>::failure(Error::NFC_AUTH);
    }
    
    log("[NFC] AUTH OK");
    return Result<bool>::success(true);
}

void Nfc_reader::start()
{
    if (!initialized_.load()) {
        log("[NFC] Cannot start - not initialized");
        return;
    }
    
    running_.store(true);
    log("[NFC] Started");
    
    while (running_.load()) 
    {
        log("[NFC] Enabling card reading...");
        auto send_result = send_command(nfc::Command::Enable);
        if (!send_result.ok()) {
            log("[NFC] Failed to enable card reading");
            continue;
        }

        std::vector<uint8_t> frameBuffer;

        while (running_.load()) {
            uint8_t b = 0;
            ssize_t bytesRead = ::read(fd_, &b, 1);
            
            if (bytesRead > 0) {
                frameBuffer.push_back(b);

                auto parsed = parseCardInfo(frameBuffer);
                if (parsed && parsed->service == static_cast<uint8_t>(nfc::Command::ReadCard) 
                    && parsed->ack == 0) 
                {
                    std::stringstream ss;
                    ss << "[NFC] Card: UID=" << parsed->uidHex
                       << " ATQA=0x" << std::hex << std::setw(4) << std::setfill('0') << parsed->atqa
                       << " SAK=0x" << std::setw(2) << static_cast<int>(parsed->sak);
                    log(ss.str());
                    
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
#include "devices/nfc_reader.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <iostream>
#include <iomanip>
#include "common/helpers.hpp"
#include "common/types.hpp"

Nfc_reader::Nfc_reader(const char* device)
{
    auto result = init(device);
    if (!result.ok()) {
        std::cerr << "[NFC] Init failed: " << strerror(errno) << "\n";
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
    tty.c_cc[VTIME] = 10;  // 1 sec timeout

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
    std::cout << "[NFC] AUTH PART A\n";
    send_command(nfc::Command::AuthA);
    auto result = read_response(16);

    std::vector<uint8_t> key;
    if (result.ok() && result.value().size() >= 4) {
        key.assign(result.value().begin() + 4, result.value().end());
    } else {
        std::cout << "[NFC] No valid Auth Key received, continuing with empty key\n";
    }

    std::cout << "[NFC] AUTH PART B\n";
    send_command(nfc::Command::AuthB, key);
    auto result2 = read_response(64);

    if (!result.value().empty() && !result2.value().empty())
        return Result<bool>::failure(Error::NFC_AUTH);
    
    return Result<bool>::success(true);
}

void Nfc_reader::start()
{
    if (!initialized_.load()) {
        std::cerr << "[NFC] Cannot start - not initialized\n";
        return;
    }
    
    running_.store(true);
    
    while (running_.load()) 
    {
        std::cout << "[NFC] ENABLE CARD READING\n";
        send_command(nfc::Command::Enable);

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
                    std::cout << "[NFC] Card UID=" << parsed->uidHex << "\n";
                    
                    if (card_callback_) {
                        card_callback_(*parsed);
                    }
                    break;
                }
            } 
            else if (bytesRead < 0 && errno != EAGAIN) {
                std::cerr << "[NFC] Read error: " << strerror(errno) << "\n";
                break;
            }
        }
    }
    
    std::cout << "[NFC] Stopped\n";
}
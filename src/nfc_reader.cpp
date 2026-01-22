#include "devices/nfc_reader.hpp"
#include "common/helpers.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <sstream>
#include <iomanip>

// Command constants
static constexpr uint8_t ADDR_REQ  = 0xF2;
static constexpr uint8_t AUTH_A    = 0x02;
static constexpr uint8_t AUTH_B    = 0x03;
static constexpr uint8_t ENABLE    = 0x63;
static constexpr uint8_t READ_CARD = 0xE3;

void Nfc_reader::log(const std::string& msg)
{
    if (log_callback_) {
        log_callback_(msg);
    }
}

Nfc_reader::Nfc_reader(const char* device)
{
    fd_ = ::open(device, O_RDWR | O_NOCTTY);
    if (fd_ < 0) {
        log("[NFC] Failed to open " + std::string(device) + ": " + strerror(errno));
        return;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(fd_, &tty) != 0) {
        log("[NFC] tcgetattr failed: " + std::string(strerror(errno)));
        ::close(fd_);
        fd_ = -1;
        return;
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

    if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
        log("[NFC] tcsetattr failed: " + std::string(strerror(errno)));
        ::close(fd_);
        fd_ = -1;
        return;
    }

    initialize();
}

Nfc_reader::~Nfc_reader()
{
    stop();
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void Nfc_reader::send_cmd(uint8_t cmd, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> frame = { ADDR_REQ, cmd, counter_++ };
    frame.insert(frame.end(), data.begin(), data.end());

    std::ostringstream oss;
    oss << "[NFC] TX: ";
    for (uint8_t byte : frame) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
    }
    log(oss.str());

    ssize_t nw = ::write(fd_, frame.data(), frame.size());
    if (nw < 0) {
        log("[NFC] Write failed: " + std::string(strerror(errno)));
    }
}

std::vector<uint8_t> Nfc_reader::read_resp(size_t expected_size)
{
    std::vector<uint8_t> buffer(expected_size);
    ssize_t n = ::read(fd_, buffer.data(), expected_size);
    if (n > 0) {
        buffer.resize(n);
    } else {
        buffer.clear();
    }
    return buffer;
}

void Nfc_reader::initialize()
{
    if (initialized_.load() || fd_ < 0) {
        return;
    }

    log("[NFC] AUTH PART A");
    send_cmd(AUTH_A);
    auto resp = read_resp(16);

    std::vector<uint8_t> key;
    if (resp.size() < 4) {
        log("[NFC] No valid Auth Key received, continuing with empty key");
    } else {
        key.assign(resp.begin() + 4, resp.end());
        log("[NFC] Got auth key, " + std::to_string(key.size()) + " bytes");
    }

    log("[NFC] AUTH PART B");
    send_cmd(AUTH_B, key);
    read_resp(64);

    initialized_.store(true);
    log("[NFC] Initialized OK");
}

void Nfc_reader::start()
{
    if (!initialized_.load()) {
        log("[NFC] Cannot start - not initialized");
        return;
    }

    running_.store(true);
    log("[NFC] Reader started");

    while (running_.load()) 
    {
        log("[NFC] ENABLE CARD READING");
        send_cmd(ENABLE);

        std::vector<uint8_t> frameBuffer;

        while (running_.load()) 
        {
            uint8_t b = 0;
            ssize_t bytesRead = ::read(fd_, &b, 1);
            
            if (bytesRead > 0) 
            {
                frameBuffer.push_back(b);

                auto parsed = parseCardInfo(frameBuffer);
                if (parsed && parsed->service == READ_CARD && parsed->ack == 0) 
                {
                    std::ostringstream oss;
                    oss << std::uppercase << std::hex
                        << "[NFC] Card UID=" << parsed->uidHex
                        << " ATQA=0x" << std::setw(4) << std::setfill('0') << parsed->atqa
                        << " CT=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(parsed->ct)
                        << " SAK=0x" << std::setw(2) << std::setfill('0') << static_cast<int>(parsed->sak);
                    
                    if (!parsed->extraBytes.empty()) {
                        oss << " EXTRA=0x";
                        for (uint8_t extra : parsed->extraBytes) {
                            oss << std::setw(2) << static_cast<int>(extra);
                        }
                    }
                    log(oss.str());

                    // Callback
                    if (card_callback_) {
                        card_callback_(*parsed);
                    }
                    
                    frameBuffer.clear();
                    break;
                }
            } 
            else if (bytesRead < 0 && errno != EAGAIN) 
            {
                log("[NFC] Read error: " + std::string(strerror(errno)));
                break;
            }
        }
    }

    log("[NFC] Reader stopped");
}
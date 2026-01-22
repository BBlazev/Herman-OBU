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
    tty.c_cc[VTIME] = 1;

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
    log("[NFC] Skipping auth - passive listen mode");
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
    
    initialized_.store(true);
    log("[NFC] Ready (passive mode)");
}

Result<bool> Nfc_reader::send_command(nfc::Command cmd, const std::vector<uint8_t>& data)
{
    return Result<bool>::success(true);
}

Result<std::vector<uint8_t>> Nfc_reader::read_response(size_t len)
{
    return Result<std::vector<uint8_t>>::success(std::vector<uint8_t>());
}

void Nfc_reader::start()
{
    if (!initialized_.load()) {
        log("[NFC] Cannot start - not initialized");
        return;
    }
    
    running_.store(true);
    log("[NFC] Listening for data on port (passive mode)...");
    log("[NFC] Scan a card on the Ingenico terminal");
    
    std::vector<uint8_t> buffer;
    
    while (running_.load()) 
    {
        struct pollfd pfd = {fd_, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);
        
        if (ret < 0) {
            log("[NFC] Poll error: " + std::string(strerror(errno)));
            break;
        }
        
        if (ret == 0) {
            continue;
        }
        
        uint8_t b = 0;
        ssize_t bytesRead = ::read(fd_, &b, 1);
        
        if (bytesRead > 0) {
            buffer.push_back(b);
            
            // Log svaki primljeni byte
            std::ostringstream oss;
            oss << "[NFC] RX byte: 0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
            oss << " (total: " << std::dec << buffer.size() << " bytes)";
            log(oss.str());
            
            // Probaj parsirati kao CardInfo
            auto parsed = parseCardInfo(buffer);
            if (parsed && parsed->ack == 0 && !parsed->uidHex.empty()) 
            {
                std::ostringstream cardOss;
                cardOss << "[NFC] Card UID=" << parsed->uidHex
                    << " ATQA=0x" << std::hex << std::setw(4) << std::setfill('0') << parsed->atqa
                    << " SAK=0x" << std::setw(2) << static_cast<int>(parsed->sak);
                log(cardOss.str());
                
                if (card_callback_) {
                    card_callback_(*parsed);
                }
                buffer.clear();
            }
            
            // Ako buffer preraste, resetiraj
            if (buffer.size() > 256) {
                log("[NFC] Buffer overflow, clearing");
                buffer.clear();
            }
        }
    }
    
    log("[NFC] Stopped");
}
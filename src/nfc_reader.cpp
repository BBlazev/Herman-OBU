#include "devices/nfc_reader.hpp"
#include "common/helpers.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
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

Nfc_reader::Nfc_reader(const char* host, int port)
{
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        init_error_ = std::string("Failed to create socket: ") + strerror(errno);
        return;
    }
    
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    
    if (::connect(fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        init_error_ = std::string("Failed to connect to ") + host + ":" + std::to_string(port) + ": " + strerror(errno);
        ::close(fd_);
        fd_ = -1;
        return;
    }
    
    init_error_ = "";
    log(std::string("[NFC] Connected to ECRProxy at ") + host + ":" + std::to_string(port));
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
    return true;
}

void Nfc_reader::initialize()
{
    if (fd_ < 0) {
        log("[NFC] Cannot init - not connected: " + init_error_);
        return;
    }
    
    if (initialized_.load()) {
        log("[NFC] Already initialized");
        return;
    }
    
    initialized_.store(true);
    log("[NFC] Ready - listening to ECRProxy");
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
    log("[NFC] Listening to ECRProxy for card events...");
    log("[NFC] Tap a card on the Ingenico terminal");
    
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
        
        uint8_t buf[256];
        ssize_t bytesRead = ::read(fd_, buf, sizeof(buf));
        
        if (bytesRead > 0) {
            std::ostringstream oss;
            oss << "[NFC] RX " << bytesRead << " bytes: ";
            for (ssize_t i = 0; i < bytesRead; i++) {
                oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buf[i]) << " ";
                buffer.push_back(buf[i]);
            }
            log(oss.str());
            
            if (buffer.size() > 1024) {
                log("[NFC] Buffer overflow, clearing");
                buffer.clear();
            }
        }
        else if (bytesRead == 0) {
            log("[NFC] Connection closed by ECRProxy");
            break;
        }
        else if (errno != EAGAIN) {
            log("[NFC] Read error: " + std::string(strerror(errno)));
            break;
        }
    }
    
    log("[NFC] Stopped");
}
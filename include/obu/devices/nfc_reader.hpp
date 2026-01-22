#pragma once

#include "common/types.hpp"
#include "common/response.hpp"
#include <string>
#include <atomic>
#include <functional>
#include <vector>
#include <termios.h>
namespace nfc {
    enum class Command : uint8_t {
        AddrReq   = 0xF2,
        AuthA     = 0x02,
        AuthB     = 0x03,
        Enable    = 0x63,
        ReadCard  = 0xE3
    };
}

class Nfc_reader
{
public:
    explicit Nfc_reader(const char* device = "/dev/ttyACM2", speed_t baud = B9600);
    ~Nfc_reader();
    
    Nfc_reader(const Nfc_reader&) = delete;
    Nfc_reader& operator=(const Nfc_reader&) = delete;
    
    [[nodiscard]] bool is_initialized() const { return initialized_.load(); }
    [[nodiscard]] bool is_port_open() const { return fd_ >= 0; }
    [[nodiscard]] std::string get_init_error() const { return init_error_; }
    
    void initialize();
    void start();
    void stop() { running_.store(false); }
    
    using CardCallback = std::function<void(const CardInfo&)>;
    using LogCallback = std::function<void(const std::string&)>;
    
    void set_card_callback(CardCallback cb) { card_callback_ = std::move(cb); }
    void set_log_callback(LogCallback cb) { log_callback_ = std::move(cb); }

private:
    int fd_ = -1;
    uint8_t counter_ = 0;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::string init_error_;
    CardCallback card_callback_;
    LogCallback log_callback_;
    
    void log(const std::string& msg);
    bool do_auth();
    Result<bool> send_command(nfc::Command cmd, const std::vector<uint8_t>& data = {});
    Result<std::vector<uint8_t>> read_response(size_t len);
};
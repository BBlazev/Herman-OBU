#pragma once

#include "transport/serial.hpp"
#include "common/types.hpp"
#include "common/response.hpp"
#include "common/helpers.hpp"
#include <string>
#include <vector>
#include <atomic>
#include <functional>

namespace nfc {
    enum class Command : uint8_t {
        AddrReq   = 0xF2,
        AuthA     = 0x02,
        AuthB     = 0x03,
        Enable    = 0x63,
        ReadCard  = 0xE3
    };
}

#pragma once

#include "common/types.hpp"
#include "common/response.hpp"
#include <string>
#include <vector>
#include <atomic>
#include <functional>

class Nfc_reader
{
public:
    explicit Nfc_reader(const char* device = "/dev/ttymxc1");
    ~Nfc_reader();
    
    Nfc_reader(const Nfc_reader&) = delete;
    Nfc_reader& operator=(const Nfc_reader&) = delete;
    
    [[nodiscard]] bool is_initialized() const { return initialized_.load(); }
    
    void start();
    void stop() { running_.store(false); }
    
    // Callbacks
    using CardCallback = std::function<void(const CardInfo&)>;
    using LogCallback = std::function<void(const std::string&)>;
    
    void set_card_callback(CardCallback cb) { card_callback_ = std::move(cb); }
    void set_log_callback(LogCallback cb) { log_callback_ = std::move(cb); }

private:
    int fd_ = -1;
    uint8_t counter_ = 0;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    CardCallback card_callback_;
    LogCallback log_callback_;
    
    void log(const std::string& msg);
    void initialize();
    void send_cmd(uint8_t cmd, const std::vector<uint8_t>& data = {});
    std::vector<uint8_t> read_resp(size_t expected_size);
};
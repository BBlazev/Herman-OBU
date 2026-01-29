#pragma once

#include "transport/serial.hpp"
#include "common/types.hpp"
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <cstdint>

namespace validator {

struct NfcCardInfo {
    std::string uid_hex;
    uint16_t atqa{0};
    uint8_t sak{0};
    uint8_t ct{0};
    uint8_t bcc1{0};
    uint8_t bcc2{0};
    std::vector<uint8_t> extra;
};

class NfcReader
{
public:
    static constexpr uint8_t ADDR_REQ = 0xF2;
    static constexpr uint8_t CMD_AUTH_A = 0x02;
    static constexpr uint8_t CMD_AUTH_B = 0x03;
    static constexpr uint8_t CMD_ENABLE = 0x63;
    static constexpr uint8_t SERVICE_READ_CARD = 0xE3;
    
    static constexpr const char* DEFAULT_PORT = "/dev/ttymxc1";
    static constexpr int DEFAULT_BAUD = 921600;
    
    using CardCallback = std::function<void(const NfcCardInfo&)>;
    
    explicit NfcReader(const char* port = DEFAULT_PORT);
    ~NfcReader();
    
    NfcReader(const NfcReader&) = delete;
    NfcReader& operator=(const NfcReader&) = delete;
    
    Result<bool> initialize();
    bool is_initialized() const { return initialized_.load(); }
    bool is_running() const { return running_.load(); }
    
    void set_card_callback(CardCallback callback) { card_callback_ = std::move(callback); }
    
    Result<bool> start_reading();
    void stop() { running_.store(false); }
    Result<NfcCardInfo> read_single_card(int timeout_ms = 5000);
    
    std::string get_last_error() const { return last_error_; }

private:
    SerialPort serial_;
    std::string port_;
    uint8_t counter_{0};
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::string last_error_;
    CardCallback card_callback_;
    
    Result<bool> configure_serial();
    Result<bool> send_command(uint8_t cmd, const std::vector<uint8_t>& data = {});
    Result<std::vector<uint8_t>> read_response(int timeout_ms = 1000);
    Result<bool> authenticate();
    Result<bool> enable_reading();
    
    static std::optional<NfcCardInfo> parse_card_info(const std::vector<uint8_t>& frame);
    static std::string bytes_to_hex(const std::vector<uint8_t>& data);
};

} // namespace validator
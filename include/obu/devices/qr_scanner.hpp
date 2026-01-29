#pragma once

#include "transport/serial.hpp"
#include "common/types.hpp"
#include <string>
#include <functional>
#include <atomic>


class QrScanner
{
public:
    using ScanCallback = std::function<void(const std::string& code)>;
    

    explicit QrScanner(SerialPort& serial) : serial_(serial) {}
    

    Result<bool> initialize();
    bool is_initialized() const { return initialized_; }

    Result<bool> trigger_on();
    Result<bool> trigger_off();
    Result<std::string> read_code();
    Result<std::string> scan_once();

    void set_scan_callback(ScanCallback callback) { scan_callback_ = std::move(callback); }
    Result<bool> start_continuous();
    void stop() { running_.store(false); }
    bool is_running() const { return running_.load(); }

private:
    SerialPort& serial_;
    ScanCallback scan_callback_;
    std::atomic<bool> running_{false};
    bool initialized_{false};
    
    std::string parse_scan_data(const std::vector<unsigned char>& data);
    Result<bool> send_command(uint8_t cmd);
};
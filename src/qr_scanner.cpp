#include "devices/qr_scanner.hpp"
#include <algorithm>

namespace {
    constexpr uint8_t CMD_PREFIX = 0x16;
    constexpr uint8_t CMD_SUFFIX = 0x0D;
    constexpr uint8_t CMD_TRIGGER_ON = 0x54;   
    constexpr uint8_t CMD_TRIGGER_OFF = 0x55;  
    
    constexpr uint8_t RESP_ACK = 0x06;
    constexpr uint8_t RESP_NAK = 0x15;
}

Result<bool> QrScanner::send_command(uint8_t cmd)
{
    unsigned char packet[] = {CMD_PREFIX, cmd, CMD_SUFFIX};
    auto result = serial_.write(packet, sizeof(packet));
    if (!result.ok()) {
        return Result<bool>::failure(result.error());
    }
    return Result<bool>::success(true);
}

Result<bool> QrScanner::initialize()
{
    if (initialized_) {
        return Result<bool>::success(true);
    }
    
    auto result = send_command(CMD_TRIGGER_OFF);
    if (!result.ok()) {
        return Result<bool>::failure(result.error());
    }
    
    bool running = true;
    serial_.set_timeout_ms(200);
    serial_.read(running); 
    serial_.set_timeout_ms(3000);  
    
    initialized_ = true;
    return Result<bool>::success(true);
}

Result<bool> QrScanner::trigger_on()
{
    return send_command(CMD_TRIGGER_ON);
}

Result<bool> QrScanner::trigger_off()
{
    return send_command(CMD_TRIGGER_OFF);
}

std::string QrScanner::parse_scan_data(const std::vector<unsigned char>& data)
{
    if (data.empty()) {
        return "";
    }
    
    std::string code(data.begin(), data.end());
    
    while (!code.empty()) {
        char c = code.back();
        if (c == '\r' || c == '\n' || c == RESP_ACK || c == RESP_NAK || 
            c == '.' || c == '!' || c == ' ' || c == '\0') {
            code.pop_back();
        } else {
            break;
        }
    }
    
    size_t start = 0;
    while (start < code.size()) {
        char c = code[start];
        if (c == '\r' || c == '\n' || c == RESP_ACK || c == RESP_NAK || 
            c == ' ' || c == '\0') {
            start++;
        } else {
            break;
        }
    }
    
    if (start > 0) {
        code = code.substr(start);
    }
    
    return code;
}

Result<std::string> QrScanner::read_code()
{
    bool running = true;
    auto result = serial_.read(running);
    
    if (!result.ok()) {
        return Result<std::string>::failure(result.error());
    }
    
    std::string code = parse_scan_data(result.value());
    
    if (code.empty()) {
        return Result<std::string>::failure(Error::INVALID_RESPONSE);
    }
    
    return Result<std::string>::success(code);
}

Result<std::string> QrScanner::scan_once()
{
    if (!initialized_) {
        auto init_result = initialize();
        if (!init_result.ok()) {
            return Result<std::string>::failure(init_result.error());
        }
    }
    
    auto on_result = trigger_on();
    if (!on_result.ok()) {
        return Result<std::string>::failure(on_result.error());
    }
    
    auto read_result = read_code();
    
    trigger_off();
    
    return read_result;
}

Result<bool> QrScanner::start_continuous()
{
    if (!initialized_) {
        auto init_result = initialize();
        if (!init_result.ok()) {
            return Result<bool>::failure(init_result.error());
        }
    }
    
    running_.store(true);
    
    auto on_result = trigger_on();
    if (!on_result.ok()) {
        running_.store(false);
        return Result<bool>::failure(on_result.error());
    }
    
    std::string last_code;
    auto last_scan_time = std::chrono::steady_clock::now();
    constexpr auto DUPLICATE_THRESHOLD = std::chrono::milliseconds(1000);
    
    while (running_.load()) 
    {
        bool run_flag = running_.load();
        auto result = serial_.read(run_flag);
        
        if (!result.ok()) {
            if (result.error() == Error::TIMEOUT) {
                continue;
            }
            running_.store(false);
            trigger_off();
            return Result<bool>::failure(result.error());
        }
        
        std::string code = parse_scan_data(result.value());
        
        if (!code.empty() && scan_callback_) {
            auto now = std::chrono::steady_clock::now();
            
            if (code != last_code || 
                (now - last_scan_time) > DUPLICATE_THRESHOLD) 
            {
                last_code = code;
                last_scan_time = now;
                scan_callback_(code);
            }
        }
    }
    
    trigger_off();
    return Result<bool>::success(true);
}
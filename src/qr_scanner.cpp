#include "devices/qr_scanner.hpp"

Result<bool> QrScanner::trigger_on()
{
    unsigned char cmd[] = {0x16, 0x54, 0x0D};
    auto result = serial_.write(cmd, sizeof(cmd));
    if (!result.ok()) {
        return Result<bool>::failure(result.error());
    }
    return Result<bool>::success(true);
}

Result<bool> QrScanner::trigger_off()
{
    unsigned char cmd[] = {0x16, 0x55, 0x0D};
    auto result = serial_.write(cmd, sizeof(cmd));
    if (!result.ok()) {
        return Result<bool>::failure(result.error());
    }
    return Result<bool>::success(true);
}

Result<std::string> QrScanner::read_code()
{
    bool running = true;
    auto result = serial_.read(running);
    
    if (!result.ok()) {
        return Result<std::string>::failure(result.error());
    }
    
    auto& data = result.value();
    std::string code(data.begin(), data.end());
    
    while (!code.empty() && (code.back() == '\r' || code.back() == '\n' || 
           code.back() == 0x06 || code.back() == 0x15 || code.back() == '.' || code.back() == '!')) {
        code.pop_back();
    }
    
    return Result<std::string>::success(code);
}
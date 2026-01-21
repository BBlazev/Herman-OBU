#pragma once

#include "transport/serial.hpp"
#include "common/types.hpp"
#include "common/response.hpp"

#include <stdint.h>



class Terminal
{
public:
    Terminal(SerialPort& serial) : serial_(serial) {}
    
    Result<TerminalAliveResponse> alive(TerminalAddress addr = TerminalAddress::TERMINAL_A);
    Result<bool> beep(TerminalAddress addr = TerminalAddress::TERMINAL_A);

private:
    SerialPort& serial_;
    
    Result<std::vector<uint8_t>> send_command(TerminalAddress addr, uint8_t service, const uint8_t* data = nullptr, size_t len = 0);
    
    uint8_t make_request_addr(TerminalAddress addr) {
        return static_cast<uint8_t>(addr) | 0x80;  
    }
};
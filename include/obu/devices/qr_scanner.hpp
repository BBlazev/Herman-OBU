#pragma once

#include "transport/serial.hpp"
#include "common/types.hpp"
#include <string>

class QrScanner
{
public:
    QrScanner(SerialPort& serial) : serial_(serial) {}
    
    Result<bool> trigger_on();
    Result<bool> trigger_off();
    Result<std::string> read_code();

private:
    SerialPort& serial_;
};
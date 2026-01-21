#include "serial.hpp"

bool SerialPort::open(const std::string& port, int baud = baud_)
{
    port_ = port;
    
    fd_ = ::open()
}
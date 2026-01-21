#pragma once

#include <stdint.h>
#include <string>



struct AliveResponse
{
    uint16_t status;
    uint16_t hw_version;
    uint16_t sw_version;
    uint16_t bootloader_version;
    uint32_t uptime_seconds;
};

struct GpsData
{
    std::string nmea;  
};

struct TerminalAliveResponse
{
    uint16_t status;
    uint16_t hw_version;
    uint16_t sw_version;
    uint16_t bootloader_version;
};

enum class TerminalAddress : uint8_t
{
    TERMINAL_A = 0x30,
    TERMINAL_B = 0x31,
    ADAPTER_A = 0x32,
    ADAPTER_B = 0x33
};
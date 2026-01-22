#pragma once

#include <stdint.h>
#include <string>
#include <cstdint>
#include <string>
#include <vector>

static constexpr uint8_t ADDR_REQ = 0xF2;
static constexpr uint8_t AUTH_A = 0x02;
static constexpr uint8_t AUTH_B = 0x03;
static constexpr uint8_t ENABLE = 0x63;
static constexpr uint8_t READ_CARD = 0xE3;

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

struct CardInfo {
    uint8_t destAddr = 0;
    uint8_t sourceAddr = 0;
    uint8_t service = 0;
    uint8_t counter = 0;
    uint8_t ack = 0;
    uint16_t atqa = 0;
    uint8_t ct = 0;
    std::string uidHex;
    uint8_t bcc1 = 0;
    uint8_t bcc2 = 0;
    uint8_t sak = 0;
    std::vector<uint8_t> extraBytes;
};
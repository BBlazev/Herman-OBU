#pragma once
#include <stdint.h>

namespace Protocol
{
    // Timeouts (ms)
    constexpr int MBOARD_TIMEOUT_MS = 150;
    constexpr int TERMINAL_TIMEOUT_MS = 120;
    constexpr int EPP_TIMEOUT_MS = 150;
    constexpr int QR_FRAME_TIMEOUT_MS = 200;
    
    // EPDI framing bytes
    constexpr uint8_t DLE = 0x10;
    constexpr uint8_t SYNC = 0x16;
    constexpr uint8_t ETX = 0x03;
    
    // Addresses
    namespace Mboard {
        constexpr uint8_t REQUEST = 0xF2;
        constexpr uint8_t RESPONSE = 0x72;
    }
    
    namespace Terminal {
        constexpr uint8_t TERM_A_BASE = 0x30;
        constexpr uint8_t TERM_B_BASE = 0x31;
        // Request = base | 0x80
    }
    
    namespace Epp {
        constexpr uint8_t REQUEST = 0xF0;
        constexpr uint8_t RESPONSE = 0x70;
    }
    
    // Service codes
    namespace Service {
        constexpr uint8_t ALIVE = 0x00;
        constexpr uint8_t READ_REGISTERS = 0x30;
        constexpr uint8_t WRITE_REGISTERS = 0x31;
        constexpr uint8_t BEEP = 0x31;  
        constexpr uint8_t GPS = 0x70;
    }
}
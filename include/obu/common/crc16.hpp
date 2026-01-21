#pragma once
#include <stdint.h>

class CRC16
{
private:
    static inline unsigned short crc_table[256];
    static inline bool initialized = false;
    
    static void init_table()
    {
        if (initialized) return;
        for (int i = 0; i < 256; i++) {
            unsigned short fcs = i << 8;
            for (int j = 8; j > 0; j--) {
                if (fcs & 0x8000) fcs = (fcs << 1) ^ 0x8005;
                else fcs = fcs << 1;
            }
            crc_table[i] = fcs;
        }
        initialized = true;
    }

public:
    static unsigned short calculate(const unsigned char* data, size_t len)
    {
        init_table();
        unsigned short crc = 0;
        for (size_t i = 0; i < len; i++)
            crc = crc_table[(crc >> 8) ^ data[i]] ^ (crc << 8);
        return crc;
    }
};
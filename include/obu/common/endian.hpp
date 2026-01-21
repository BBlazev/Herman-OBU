#pragma once
#include <cstdint>
#include <bit>      
#include <cstring>  


constexpr uint16_t to_big_endian_16(uint16_t val) {
    if constexpr (std::endian::native == std::endian::big) {
        return val;
    } else {
        return std::byteswap(val);
    }
}

constexpr uint16_t from_big_endian_16(uint16_t val) {
    return to_big_endian_16(val);
}

void write_be16(uint8_t* buf, uint16_t val) {
    uint16_t be_val = to_big_endian_16(val);
    std::memcpy(buf, &be_val, sizeof(be_val));
}

uint16_t read_be16(const uint8_t* buf) {
    uint16_t temp;
    std::memcpy(&temp, buf, sizeof(temp));
    return from_big_endian_16(temp);
}
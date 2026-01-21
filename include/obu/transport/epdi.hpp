#pragma once

#include <vector>
#include <stdint.h>

#include "common/types.hpp"

class EpdiFrame{
public:
    static std::vector<uint8_t> encode(const uint8_t* data, size_t len);
    static Result<std::vector<uint8_t>> decode(const uint8_t* frame, size_t len);
};


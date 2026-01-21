#pragma once

#include "transport/serial.hpp"
#include "common/types.hpp"
#include "common/response.hpp"
#include <vector>
#include <stdint.h>

class Mboard
{
public:

    Mboard(SerialPort& serial);

    Result<AliveResponse> alive();    
    //Result<GpsData> gps();
    Result<std::vector<uint8_t>> read_registers(uint8_t start, uint8_t count);

private:

    SerialPort& serial_;
    uint16_t counter_ = 0;

    Result<std::vector<uint8_t>> send_command(uint8_t service, const uint8_t* data = nullptr, size_t len = 0);

};
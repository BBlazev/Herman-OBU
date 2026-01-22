#pragma once

#include "transport/serial.hpp"
#include "common/types.hpp"
#include "common/response.hpp"
#include "common/helpers.hpp"
#include <string>



class Nfc_reader
{
public:

    Nfc_reader();
    ~Nfc_reader();
    
    Result<bool> initialized = Result<bool>::success(true);

private:
    int fd_;
    //SerialPort& serial_;
    uint8_t counter;


    Result<bool> init();
    Result<bool> send_command(uint8_t cmd, std::span<const uint8_t> data = {});
    void start_NFC();
    [[nodiscard]]Result<std::vector<uint8_t>> read_response(size_t len);
    [[nodiscard]] Result<bool> auth();

};
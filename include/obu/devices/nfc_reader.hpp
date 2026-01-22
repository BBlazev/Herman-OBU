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
    

private:
    int fd_;
    //SerialPort& serial_;
    uint8_t counter;
    Result<bool> initialized = Result<bool>::success(true);


    Result<bool> init();
    Result<bool> auth();
    Result<bool> send_command(uint8_t cmd, const std::vector<uint8_t>& data = {});
    Result<std::vector<uint8_t>> read_response(size_t len);
    void start_NFC();

};
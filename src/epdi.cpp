#include "transport/epdi.hpp"
#include "common/crc16.hpp"


std::vector<uint8_t>EpdiFrame::encode(const uint8_t* data, size_t len)
{
    std::vector<uint8_t> frame;
    
    frame.push_back(0x10);  // DLE
    frame.push_back(0x16);  // SYNC
    
    for (size_t i = 0; i < len; i++) {
        if (data[i] == 0x10) {
            frame.push_back(0x10);  
        }
        frame.push_back(data[i]);
    }
    
    frame.push_back(0x10);  // DLE
    frame.push_back(0x03);  // ETX
    
    unsigned short crc = CRC16::calculate(data, len);
    frame.push_back((crc >> 8) & 0xFF);  // MSB
    frame.push_back(crc & 0xFF);         // LSB
    
    return frame;
}

Result<std::vector<uint8_t>>EpdiFrame::decode(const uint8_t* frame, size_t len)
{
// DLE + SYNC + DLE + ETX + CRC = 6 bytes min
    if (len < 6) {
        return Result<std::vector<uint8_t>>::failure(Error::INVALID_RESPONSE);
    }
    
    if (frame[0] != 0x10 || frame[1] != 0x16) {
        return Result<std::vector<uint8_t>>::failure(Error::INVALID_RESPONSE);
    }
    
    std::vector<uint8_t> data;
    size_t i = 2;  
    
    while (i < len - 4) {  
        if (frame[i] == 0x10) {
            if (frame[i + 1] == 0x03) {
                break;  
            } else if (frame[i + 1] == 0x10) {
                data.push_back(0x10);  
                i += 2;
                continue;
            }
        }
        data.push_back(frame[i]);
        i++;
    }
    
    unsigned short received_crc = (frame[len - 2] << 8) | frame[len - 1];
    unsigned short calculated_crc = CRC16::calculate(data.data(), data.size());
    
    if (received_crc != calculated_crc) {
        return Result<std::vector<uint8_t>>::failure(Error::CRC_MISSMATCH);
    }
    
    return Result<std::vector<uint8_t>>::success(std::move(data));
}

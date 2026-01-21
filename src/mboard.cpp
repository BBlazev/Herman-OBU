#include "devices/mboard.hpp"
#include "transport/epdi.hpp"
#include "common/protocol.hpp"



Mboard::Mboard(SerialPort& serial) : serial_(serial) {}

Result<AliveResponse>Mboard::alive()
{
    auto result = send_command(Protocol::Service::ALIVE);
    if (!result.ok()) {
        return Result<AliveResponse>::failure(result.error());
    }
    
    auto& payload = result.value();
    if (payload.size() < 17) {
        return Result<AliveResponse>::failure(Error::INVALID_RESPONSE);
    }
    
    AliveResponse response;
    response.status = (payload[5] << 8) | payload[6];
    response.hw_version = (payload[7] << 8) | payload[8];
    response.sw_version = (payload[9] << 8) | payload[10];
    response.bootloader_version = (payload[11] << 8) | payload[12];
    response.uptime_seconds = (payload[13] << 24) | (payload[14] << 16) | (payload[15] << 8) | payload[16];
    
    return Result<AliveResponse>::success(response);

}

Result<std::vector<uint8_t>> Mboard::send_command(uint8_t service, const uint8_t* data, size_t data_len)
{
    std::vector<uint8_t> cmd;
    cmd.push_back(Protocol::Mboard::REQUEST);
    cmd.push_back(service);
    cmd.push_back(static_cast<uint8_t>(counter_ >> 8));
    cmd.push_back(static_cast<uint8_t>(counter_ & 0xFF));
    counter_++;
    
    if (data && data_len > 0) {
        cmd.insert(cmd.end(), data, data + data_len);
    }
    
    std::vector<uint8_t> frame = EpdiFrame::encode(cmd.data(), cmd.size());
    
    auto write_result = serial_.write(frame.data(), frame.size());
    if (!write_result.ok()) {
        return Result<std::vector<uint8_t>>::failure(write_result.error());
    }
    
    bool running = true;
    auto read_result = serial_.read(running);
    if (!read_result.ok()) {
        return Result<std::vector<uint8_t>>::failure(read_result.error());
    }
    
    return EpdiFrame::decode(read_result.value().data(), read_result.value().size());
}

Result<std::vector<uint8_t>> Mboard::read_registers(uint8_t start_reg, uint8_t count)
{
    uint8_t data[] = { start_reg, count };
    
    auto result = send_command(Protocol::Service::READ_REGISTERS, data, sizeof(data));
    if (!result.ok()) {
        return result;  
    }
    
    auto& payload = result.value();
    if (payload.size() < 5) {
        return Result<std::vector<uint8_t>>::failure(Error::INVALID_RESPONSE);
    }
    
    std::vector<uint8_t> registers(payload.begin() + 5, payload.end());
    return Result<std::vector<uint8_t>>::success(std::move(registers));
}
#include "devices/terminal.hpp"
#include "common/protocol.hpp"
#include "transport/epdi.hpp"

Result<TerminalAliveResponse> Terminal::alive(TerminalAddress addr)
{
    auto result = send_command(addr, Protocol::Service::ALIVE);

    if(!result.ok())
        return Result<TerminalAliveResponse>::failure(result.error());

    auto& payload = result.value();
    

    
    if (payload.size() < 10) {
        return Result<TerminalAliveResponse>::failure(Error::INVALID_RESPONSE);
    }
    
    TerminalAliveResponse response;
    response.status = (payload[2] << 8) | payload[3];
    response.hw_version = (payload[4] << 8) | payload[5];
    response.sw_version = (payload[6] << 8) | payload[7];
    response.bootloader_version = (payload[8] << 8) | payload[9];
    
    return Result<TerminalAliveResponse>::success(response);
}

Result<std::vector<uint8_t>> Terminal::send_command(TerminalAddress addr, uint8_t service, const uint8_t* data, size_t len)
{
    std::vector<uint8_t> cmd;
    cmd.push_back(make_request_addr(addr));
    cmd.push_back(service);
    if (data && len > 0) {
        cmd.insert(cmd.end(), data, data + len);
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
    

    auto& raw = read_result.value();
    
    size_t second_frame_start = 0;
    for (size_t i = 2; i < raw.size() - 4; i++) {
        if (raw[i] == 0x10 && raw[i+1] == 0x03) {
            second_frame_start = i + 4;
            break;
        }
    }
    
    if (second_frame_start > 0 && second_frame_start < raw.size()) {
        return EpdiFrame::decode(raw.data() + second_frame_start, raw.size() - second_frame_start);
    }
    
    return EpdiFrame::decode(raw.data(), raw.size());
}

Result<bool> Terminal::beep(TerminalAddress addr)
{
    auto result = send_command(addr, Protocol::Service::BEEP);
    
    if (!result.ok()) {
        return Result<bool>::failure(result.error());
    }
    
    return Result<bool>::success(true);
}
#include "common/helpers.hpp"
#include <sstream>
#include <iomanip>

std::string bytesToHex(const std::vector<uint8_t>& data)
{
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (uint8_t byte : data) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::optional<CardInfo> parseCardInfo(const std::vector<uint8_t>& frame)
{
    if (frame.size() < 10) {
        return std::nullopt;
    }

    size_t start = 0;
    while (start + 1 < frame.size()) {
        if (frame[start] == 0x10 && frame[start + 1] == 0x16) {
            break;
        }
        ++start;
    }
    if (start + 1 >= frame.size()) {
        return std::nullopt;
    }

    size_t dataStart = start + 2;
    size_t etxPos = frame.size();
    bool etxFound = false;
    for (size_t i = dataStart; i + 1 < frame.size(); ++i) {
        if (frame[i] == 0x10 && frame[i + 1] == 0x03) {
            etxPos = i;
            etxFound = true;
            break;
        }
    }
    if (!etxFound || etxPos + 4 > frame.size()) {
        return std::nullopt;
    }

    std::vector<uint8_t> payload;
    payload.reserve(etxPos - dataStart);
    for (size_t i = dataStart; i < etxPos; ++i) {
        uint8_t byte = frame[i];
        if (byte == 0x10) {
            if (i + 1 >= etxPos || frame[i + 1] != 0x10) {
                return std::nullopt;
            }
            payload.push_back(0x10);
            ++i;
            continue;
        }
        payload.push_back(byte);
    }

    if (payload.size() < 6) {
        return std::nullopt;
    }

    CardInfo info;
    size_t pos = 0;
    info.destAddr = payload[pos++];
    info.service = payload[pos++];
    info.counter = payload[pos++];
    info.sourceAddr = payload[pos++];
    info.ack = payload[pos++];

    if (payload.size() - pos < 13) {
        return std::nullopt;
    }

    info.atqa = static_cast<uint16_t>((static_cast<uint16_t>(payload[pos]) << 8) | payload[pos + 1]);
    pos += 2;
    
    info.ct = payload[pos++];

    std::vector<uint8_t> uid;
    uid.insert(uid.end(), payload.begin() + pos, payload.begin() + pos + 3);
    pos += 3;
    info.bcc1 = payload[pos++];
    uid.insert(uid.end(), payload.begin() + pos, payload.begin() + pos + 4);
    pos += 4;
    info.bcc2 = payload[pos++];
    info.sak = payload[pos++];
    info.uidHex = bytesToHex(uid);

    if (payload.size() > pos) {
        info.extraBytes.assign(payload.begin() + pos, payload.end());
    }

    return info;
}
#pragma once

#include <vector>
#include <optional>
#include "response.hpp"

std::string bytesToHex(const std::vector<uint8_t>& data);
std::optional<CardInfo> parseCardInfo(const std::vector<uint8_t>& frame);

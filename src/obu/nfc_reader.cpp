#include "obu/corvus_nfc_reader.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace obu {

namespace {

constexpr uint8_t STX = 0x02;
constexpr uint8_t ETX = 0x03;
constexpr uint8_t FS = 0x1C;

constexpr const char* MSG_TYPE_LOGON = "0100";
constexpr const char* MSG_TYPE_ADMIN = "0200";
constexpr const char* MSG_TYPE_LOGON_RESP = "0110";
constexpr const char* MSG_TYPE_ADMIN_RESP = "0210";

constexpr const char* PROC_CODE_LOGON = "900000";
constexpr const char* PROC_CODE_READ_UID = "900100";
constexpr const char* PROC_CODE_READ_CARD = "900200";
constexpr const char* PROC_CODE_OPERATIONAL = "900300";

uint8_t calculate_lrc(const uint8_t* data, size_t len) {
    uint8_t lrc = 0;
    for (size_t i = 0; i < len; ++i) {
        lrc ^= data[i];
    }
    return lrc;
}

std::string to_hex_string(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (uint8_t b : data) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

} // anonymous namespace

CorvusNfcReader::CorvusNfcReader(const char* host, int port)
    : host_(host), port_(port) {}

CorvusNfcReader::~CorvusNfcReader()
{
    stop_reading();
    disconnect();
}

uint16_t CorvusNfcReader::next_counter()
{
    counter_ = (counter_ + 1) % 10000;
    return counter_;
}

Result<bool> CorvusNfcReader::connect()
{
    if (socket_fd_ >= 0) {
        return Result<bool>::success(true);
    }
    
    socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        last_error_ = "Failed to create socket";
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    
    if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        last_error_ = "Invalid address";
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    if (::connect(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
        last_error_ = "Connection failed";
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    return Result<bool>::success(true);
}

void CorvusNfcReader::disconnect()
{
    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
}

Result<bool> CorvusNfcReader::send_message(const std::vector<uint8_t>& msg)
{
    if (socket_fd_ < 0) {
        last_error_ = "Not connected";
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    ssize_t sent = ::send(socket_fd_, msg.data(), msg.size(), 0);
    if (sent != static_cast<ssize_t>(msg.size())) {
        last_error_ = "Send failed";
        return Result<bool>::failure(Error::WRITE_ERROR);
    }
    
    return Result<bool>::success(true);
}

Result<std::vector<uint8_t>> CorvusNfcReader::receive_message(int timeout_sec)
{
    if (socket_fd_ < 0) {
        last_error_ = "Not connected";
        return Result<std::vector<uint8_t>>::failure(Error::PORT_ERROR);
    }
    
    struct pollfd pfd;
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;
    
    int ret = poll(&pfd, 1, timeout_sec * 1000);
    if (ret <= 0) {
        last_error_ = (ret == 0) ? "Timeout" : "Poll error";
        return Result<std::vector<uint8_t>>::failure(Error::TIMEOUT);
    }
    
    std::vector<uint8_t> buffer(4096);
    ssize_t received = ::recv(socket_fd_, buffer.data(), buffer.size(), 0);
    
    if (received <= 0) {
        last_error_ = "Receive failed";
        return Result<std::vector<uint8_t>>::failure(Error::READ_ERROR);
    }
    
    buffer.resize(received);
    return Result<std::vector<uint8_t>>::success(std::move(buffer));
}

std::vector<uint8_t> CorvusNfcReader::build_logon_msg(uint16_t counter, 
                                                       const std::string& op_id, 
                                                       const std::string& pwd)
{
    std::ostringstream oss;
    oss << MSG_TYPE_LOGON;
    oss << PROC_CODE_LOGON;
    oss << std::setw(4) << std::setfill('0') << counter;
    oss << FS << "01" << op_id;
    oss << FS << "02" << pwd;
    
    std::string payload = oss.str();
    
    std::vector<uint8_t> msg;
    msg.push_back(STX);
    msg.insert(msg.end(), payload.begin(), payload.end());
    msg.push_back(ETX);
    msg.push_back(calculate_lrc(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.size()));
    
    return msg;
}

std::vector<uint8_t> CorvusNfcReader::build_read_uid_msg(uint16_t counter)
{
    std::ostringstream oss;
    oss << MSG_TYPE_ADMIN;
    oss << PROC_CODE_READ_UID;
    oss << std::setw(4) << std::setfill('0') << counter;
    
    std::string payload = oss.str();
    
    std::vector<uint8_t> msg;
    msg.push_back(STX);
    msg.insert(msg.end(), payload.begin(), payload.end());
    msg.push_back(ETX);
    msg.push_back(calculate_lrc(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.size()));
    
    return msg;
}

std::vector<uint8_t> CorvusNfcReader::build_read_card_msg(uint16_t counter)
{
    std::ostringstream oss;
    oss << MSG_TYPE_ADMIN;
    oss << PROC_CODE_READ_CARD;
    oss << std::setw(4) << std::setfill('0') << counter;
    
    std::string payload = oss.str();
    
    std::vector<uint8_t> msg;
    msg.push_back(STX);
    msg.insert(msg.end(), payload.begin(), payload.end());
    msg.push_back(ETX);
    msg.push_back(calculate_lrc(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.size()));
    
    return msg;
}

std::vector<uint8_t> CorvusNfcReader::build_operational_msg(uint16_t counter)
{
    std::ostringstream oss;
    oss << MSG_TYPE_ADMIN;
    oss << PROC_CODE_OPERATIONAL;
    oss << std::setw(4) << std::setfill('0') << counter;
    
    std::string payload = oss.str();
    
    std::vector<uint8_t> msg;
    msg.push_back(STX);
    msg.insert(msg.end(), payload.begin(), payload.end());
    msg.push_back(ETX);
    msg.push_back(calculate_lrc(reinterpret_cast<const uint8_t*>(payload.c_str()), payload.size()));
    
    return msg;
}

std::string CorvusNfcReader::parse_uid_response(const std::vector<uint8_t>& response)
{
    if (response.size() < 15) {
        return "";
    }
    
    auto it = std::find(response.begin(), response.end(), FS);
    while (it != response.end()) {
        ++it;
        if (it != response.end() && *it == '6' && (it + 1) != response.end() && *(it + 1) == '3') {
            it += 2;
            auto end = std::find(it, response.end(), FS);
            if (end == response.end()) {
                end = std::find(it, response.end(), ETX);
            }
            return std::string(it, end);
        }
        it = std::find(it, response.end(), FS);
    }
    
    return "";
}

std::string CorvusNfcReader::parse_card_response(const std::vector<uint8_t>& response)
{
    if (response.size() < 15) {
        return "";
    }
    
    auto it = std::find(response.begin(), response.end(), FS);
    while (it != response.end()) {
        ++it;
        if (it != response.end() && *it == '3' && (it + 1) != response.end() && *(it + 1) == '5') {
            it += 2;
            auto end = std::find(it, response.end(), FS);
            if (end == response.end()) {
                end = std::find(it, response.end(), ETX);
            }
            return std::string(it, end);
        }
        it = std::find(it, response.end(), FS);
    }
    
    return "";
}

Result<bool> CorvusNfcReader::logon(const std::string& operator_id, const std::string& password)
{
    auto conn_result = connect();
    if (!conn_result.ok()) {
        return conn_result;
    }
    
    auto msg = build_logon_msg(next_counter(), operator_id, password);
    auto send_result = send_message(msg);
    if (!send_result.ok()) {
        return send_result;
    }
    
    auto recv_result = receive_message(30);
    if (!recv_result.ok()) {
        return Result<bool>::failure(recv_result.error());
    }
    
    return Result<bool>::success(true);
}

Result<bool> CorvusNfcReader::is_terminal_operational()
{
    auto conn_result = connect();
    if (!conn_result.ok()) {
        return conn_result;
    }
    
    auto msg = build_operational_msg(next_counter());
    auto send_result = send_message(msg);
    if (!send_result.ok()) {
        return send_result;
    }
    
    auto recv_result = receive_message(10);
    if (!recv_result.ok()) {
        return Result<bool>::failure(recv_result.error());
    }
    
    return Result<bool>::success(true);
}

Result<std::string> CorvusNfcReader::read_nfc_uid(int timeout_sec)
{
    auto conn_result = connect();
    if (!conn_result.ok()) {
        return Result<std::string>::failure(conn_result.error());
    }
    
    auto msg = build_read_uid_msg(next_counter());
    auto send_result = send_message(msg);
    if (!send_result.ok()) {
        return Result<std::string>::failure(send_result.error());
    }
    
    auto recv_result = receive_message(timeout_sec);
    if (!recv_result.ok()) {
        return Result<std::string>::failure(recv_result.error());
    }
    
    std::string uid = parse_uid_response(recv_result.value());
    if (uid.empty()) {
        last_error_ = "No UID in response";
        return Result<std::string>::failure(Error::PARSE_ERROR);
    }
    
    return Result<std::string>::success(uid);
}

Result<std::string> CorvusNfcReader::read_card_data(int timeout_sec)
{
    auto conn_result = connect();
    if (!conn_result.ok()) {
        return Result<std::string>::failure(conn_result.error());
    }
    
    auto msg = build_read_card_msg(next_counter());
    auto send_result = send_message(msg);
    if (!send_result.ok()) {
        return Result<std::string>::failure(send_result.error());
    }
    
    auto recv_result = receive_message(timeout_sec);
    if (!recv_result.ok()) {
        return Result<std::string>::failure(recv_result.error());
    }
    
    std::string pan = parse_card_response(recv_result.value());
    if (pan.empty()) {
        last_error_ = "No card data in response";
        return Result<std::string>::failure(Error::PARSE_ERROR);
    }
    
    return Result<std::string>::success(pan);
}

void CorvusNfcReader::start_reading(UidCallback callback)
{
    running_.store(true);
    
    while (running_.load()) {
        auto result = read_nfc_uid(5);
        if (result.ok() && callback) {
            callback(result.value());
        }
    }
}

} 
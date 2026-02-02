#include "obu/devices/corvus_nfc_reader.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>

// For SHA1
#include <openssl/sha.h>

namespace obu {

namespace {

// Convert password to SHA1 hash (password padded to 9 bytes with zeros)
std::string sha1_hex(const std::string& password) {
    unsigned char padded[9] = {0};
    size_t len = std::min(password.size(), size_t(9));
    memcpy(padded, password.c_str(), len);
    
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(padded, 9, hash);
    
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        oss << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

// Set socket to non-blocking mode
bool set_nonblocking(int fd, bool nonblocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(fd, F_SETFL, flags) >= 0;
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
        last_error_ = "Connection failed to ECRProxy";
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

// Send message with 2-byte length prefix (big endian)
Result<bool> CorvusNfcReader::send_message(const std::vector<uint8_t>& msg)
{
    if (socket_fd_ < 0) {
        last_error_ = "Not connected";
        return Result<bool>::failure(Error::PORT_ERROR);
    }
    
    // Build packet: 2-byte length (big endian) + payload
    std::vector<uint8_t> packet;
    uint16_t len = msg.size();
    packet.push_back((len >> 8) & 0xFF);
    packet.push_back(len & 0xFF);
    packet.insert(packet.end(), msg.begin(), msg.end());
    
    ssize_t sent = ::send(socket_fd_, packet.data(), packet.size(), 0);
    if (sent != static_cast<ssize_t>(packet.size())) {
        last_error_ = "Send failed";
        return Result<bool>::failure(Error::WRITE_ERROR);
    }
    
    return Result<bool>::success(true);
}

// Send keepalive (2 zero bytes)
bool CorvusNfcReader::send_keepalive()
{
    if (socket_fd_ < 0) return false;
    uint8_t keepalive[2] = {0, 0};
    return ::send(socket_fd_, keepalive, 2, MSG_NOSIGNAL) == 2;
}

// Receive message - reads 2-byte length then payload
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
    
    // Read length (2 bytes)
    uint8_t len_buf[2];
    ssize_t received = ::recv(socket_fd_, len_buf, 2, 0);
    if (received != 2) {
        last_error_ = "Failed to read length";
        return Result<std::vector<uint8_t>>::failure(Error::READ_ERROR);
    }
    
    uint16_t len = (len_buf[0] << 8) | len_buf[1];
    if (len == 0 || len > 4096) {
        last_error_ = "Invalid message length";
        return Result<std::vector<uint8_t>>::failure(Error::PARSE_ERROR);
    }
    
    // Read payload
    std::vector<uint8_t> buffer(len);
    received = ::recv(socket_fd_, buffer.data(), len, MSG_WAITALL);
    if (received != len) {
        last_error_ = "Failed to read payload";
        return Result<std::vector<uint8_t>>::failure(Error::READ_ERROR);
    }
    
    return Result<std::vector<uint8_t>>::success(std::move(buffer));
}

// Wait for response with keepalive
Result<std::vector<uint8_t>> CorvusNfcReader::wait_for_response_with_keepalive(int timeout_sec)
{
    if (socket_fd_ < 0) {
        last_error_ = "Not connected";
        return Result<std::vector<uint8_t>>::failure(Error::PORT_ERROR);
    }
    
    set_nonblocking(socket_fd_, true);
    
    auto start = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(timeout_sec);
    
    std::vector<uint8_t> buffer;
    
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (running_.load() == false) {
            // Cancelled
            set_nonblocking(socket_fd_, false);
            last_error_ = "Cancelled";
            return Result<std::vector<uint8_t>>::failure(Error::TIMEOUT);
        }
        
        // Try to receive
        uint8_t tmp[1024];
        ssize_t n = ::recv(socket_fd_, tmp, sizeof(tmp), 0);
        
        if (n > 0) {
            buffer.insert(buffer.end(), tmp, tmp + n);
            
            // Check if we have a complete message
            if (buffer.size() >= 2) {
                uint16_t expected_len = (buffer[0] << 8) | buffer[1];
                if (buffer.size() >= 2 + expected_len) {
                    // Complete message received
                    std::vector<uint8_t> payload(buffer.begin() + 2, buffer.begin() + 2 + expected_len);
                    set_nonblocking(socket_fd_, false);
                    return Result<std::vector<uint8_t>>::success(std::move(payload));
                }
            }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            set_nonblocking(socket_fd_, false);
            last_error_ = "Receive error";
            return Result<std::vector<uint8_t>>::failure(Error::READ_ERROR);
        }
        
        // Send keepalive every 250ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        static int keepalive_counter = 0;
        if (++keepalive_counter % 2 == 0) {  // Every 200ms
            if (!send_keepalive()) {
                set_nonblocking(socket_fd_, false);
                last_error_ = "Keepalive failed";
                return Result<std::vector<uint8_t>>::failure(Error::WRITE_ERROR);
            }
        }
    }
    
    set_nonblocking(socket_fd_, false);
    last_error_ = "Timeout waiting for response";
    return Result<std::vector<uint8_t>>::failure(Error::TIMEOUT);
}

// Build messages according to LIIngenicoECR protocol

std::vector<uint8_t> CorvusNfcReader::build_operational_msg(uint16_t counter)
{
    // Format: "300000" + seq(4) + "01"
    std::ostringstream oss;
    oss << "300000" << std::setw(4) << std::setfill('0') << counter << "01";
    std::string msg = oss.str();
    return std::vector<uint8_t>(msg.begin(), msg.end());
}

std::vector<uint8_t> CorvusNfcReader::build_logon_msg(uint16_t counter, 
                                                       const std::string& op_id, 
                                                       const std::string& pwd)
{
    // Format: "010000" + seq(4) + "01" + "L" + operatorId + ";P" + SHA1(password)
    std::string password_hash = sha1_hex(pwd);
    
    std::ostringstream oss;
    oss << "010000" << std::setw(4) << std::setfill('0') << counter << "01";
    oss << "L" << op_id << ";P" << password_hash;
    
    std::string msg = oss.str();
    return std::vector<uint8_t>(msg.begin(), msg.end());
}

std::vector<uint8_t> CorvusNfcReader::build_read_uid_msg(uint16_t counter)
{
    // Format: "010000" + seq(4) + "95"
    std::ostringstream oss;
    oss << "010000" << std::setw(4) << std::setfill('0') << counter << "95";
    std::string msg = oss.str();
    return std::vector<uint8_t>(msg.begin(), msg.end());
}

std::vector<uint8_t> CorvusNfcReader::build_read_card_msg(uint16_t counter)
{
    // Format: "010000" + seq(4) + "90"
    std::ostringstream oss;
    oss << "010000" << std::setw(4) << std::setfill('0') << counter << "90";
    std::string msg = oss.str();
    return std::vector<uint8_t>(msg.begin(), msg.end());
}

// Parse UID from response
// Response format: "110000" + seq(4) + "95" + "000" + UID
std::string CorvusNfcReader::parse_uid_response(const std::vector<uint8_t>& response)
{
    std::string resp(response.begin(), response.end());
    
    // Find "95000" which indicates success
    size_t pos = resp.find("95000");
    if (pos != std::string::npos && pos + 5 < resp.size()) {
        return resp.substr(pos + 5);  // Everything after "95000" is UID
    }
    
    return "";
}

std::string CorvusNfcReader::parse_card_response(const std::vector<uint8_t>& response)
{
    std::string resp(response.begin(), response.end());
    
    // Find "90000" which indicates success
    size_t pos = resp.find("90000");
    if (pos != std::string::npos && pos + 5 < resp.size()) {
        // Card data is after "90000", split by "="
        std::string data = resp.substr(pos + 5);
        size_t eq = data.find('=');
        if (eq != std::string::npos) {
            return data.substr(0, eq);  // PAN is before "="
        }
        return data;
    }
    
    return "";
}

// Check if response indicates success ("000" at position 12-15)
bool CorvusNfcReader::is_success_response(const std::vector<uint8_t>& response)
{
    if (response.size() < 15) return false;
    return response[12] == '0' && response[13] == '0' && response[14] == '0';
}

// Public API implementations

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
    
    auto recv_result = wait_for_response_with_keepalive(5);
    if (!recv_result.ok()) {
        return Result<bool>::failure(recv_result.error());
    }
    
    // Check for "000" success code
    if (!is_success_response(recv_result.value())) {
        last_error_ = "Terminal not operational";
        return Result<bool>::failure(Error::DEVICE_ERROR);
    }
    
    return Result<bool>::success(true);
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
    
    auto recv_result = wait_for_response_with_keepalive(10);
    if (!recv_result.ok()) {
        return Result<bool>::failure(recv_result.error());
    }
    
    // Check for "000" success code
    if (!is_success_response(recv_result.value())) {
        last_error_ = "Logon failed";
        return Result<bool>::failure(Error::DEVICE_ERROR);
    }
    
    return Result<bool>::success(true);
}

Result<std::string> CorvusNfcReader::read_nfc_uid(int timeout_sec)
{
    auto conn_result = connect();
    if (!conn_result.ok()) {
        return Result<std::string>::failure(conn_result.error());
    }
    
    running_.store(true);
    
    // 1. Check terminal operational
    auto check_msg = build_operational_msg(next_counter());
    auto check_send = send_message(check_msg);
    if (!check_send.ok()) {
        running_.store(false);
        return Result<std::string>::failure(check_send.error());
    }
    
    auto check_recv = wait_for_response_with_keepalive(5);
    if (!check_recv.ok() || !is_success_response(check_recv.value())) {
        running_.store(false);
        last_error_ = "Terminal not operational";
        return Result<std::string>::failure(Error::DEVICE_ERROR);
    }
    
    // 2. Logon
    auto logon_msg = build_logon_msg(next_counter(), "1", "23646");
    auto logon_send = send_message(logon_msg);
    if (!logon_send.ok()) {
        running_.store(false);
        return Result<std::string>::failure(logon_send.error());
    }
    
    auto logon_recv = wait_for_response_with_keepalive(10);
    // Logon may not return response immediately, continue anyway
    
    // 3. Read NFC UID
    auto read_msg = build_read_uid_msg(next_counter());
    auto read_send = send_message(read_msg);
    if (!read_send.ok()) {
        running_.store(false);
        return Result<std::string>::failure(read_send.error());
    }
    
    // Wait for card with keepalive
    auto read_recv = wait_for_response_with_keepalive(timeout_sec);
    running_.store(false);
    
    if (!read_recv.ok()) {
        return Result<std::string>::failure(read_recv.error());
    }
    
    std::string uid = parse_uid_response(read_recv.value());
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
    
    running_.store(true);
    
    // 1. Check terminal
    auto check_msg = build_operational_msg(next_counter());
    send_message(check_msg);
    wait_for_response_with_keepalive(5);
    
    // 2. Logon
    auto logon_msg = build_logon_msg(next_counter(), "1", "23646");
    send_message(logon_msg);
    wait_for_response_with_keepalive(10);
    
    // 3. Read card data
    auto read_msg = build_read_card_msg(next_counter());
    auto read_send = send_message(read_msg);
    if (!read_send.ok()) {
        running_.store(false);
        return Result<std::string>::failure(read_send.error());
    }
    
    auto read_recv = wait_for_response_with_keepalive(timeout_sec);
    running_.store(false);
    
    if (!read_recv.ok()) {
        return Result<std::string>::failure(read_recv.error());
    }
    
    std::string pan = parse_card_response(read_recv.value());
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
        
        // Small delay between reads
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace obu

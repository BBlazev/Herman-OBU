#pragma once

#include "common/types.hpp"
#include <string>
#include <functional>
#include <atomic>
#include <cstdint>
#include <vector>

namespace obu {

class CorvusNfcReader
{
public:
    static constexpr const char* DEFAULT_HOST = "127.0.0.1";
    static constexpr int DEFAULT_PORT = 4543;
    static constexpr int DEFAULT_TIMEOUT_SEC = 20;
    
    using UidCallback = std::function<void(const std::string& uid)>;
    
    explicit CorvusNfcReader(const char* host = DEFAULT_HOST, int port = DEFAULT_PORT);
    ~CorvusNfcReader();
    
    CorvusNfcReader(const CorvusNfcReader&) = delete;
    CorvusNfcReader& operator=(const CorvusNfcReader&) = delete;
    
    Result<bool> connect();
    void disconnect();
    bool is_connected() const { return socket_fd_ >= 0; }
    
    Result<bool> logon(const std::string& operator_id = "1", const std::string& password = "23646");
    Result<bool> is_terminal_operational();
    
    Result<std::string> read_nfc_uid(int timeout_sec = DEFAULT_TIMEOUT_SEC);
    Result<std::string> read_card_data(int timeout_sec = DEFAULT_TIMEOUT_SEC);
    
    void start_reading(UidCallback callback);
    void stop_reading() { running_.store(false); }
    bool is_running() const { return running_.load(); }
    
    std::string get_last_error() const { return last_error_; }

private:
    std::string host_;
    int port_;
    int socket_fd_{-1};
    uint16_t counter_{0};
    std::atomic<bool> running_{false};
    std::string last_error_;
    
    uint16_t next_counter();
    
    Result<bool> send_message(const std::vector<uint8_t>& msg);
    Result<std::vector<uint8_t>> receive_message(int timeout_sec);
    Result<std::vector<uint8_t>> wait_for_response_with_keepalive(int timeout_sec);
    bool send_keepalive();
    bool is_success_response(const std::vector<uint8_t>& response);
    
    std::vector<uint8_t> build_logon_msg(uint16_t counter, const std::string& op_id, const std::string& pwd);
    std::vector<uint8_t> build_read_uid_msg(uint16_t counter);
    std::vector<uint8_t> build_read_card_msg(uint16_t counter);
    std::vector<uint8_t> build_operational_msg(uint16_t counter);
    
    std::string parse_uid_response(const std::vector<uint8_t>& response);
    std::string parse_card_response(const std::vector<uint8_t>& response);
};

} // namespace obu

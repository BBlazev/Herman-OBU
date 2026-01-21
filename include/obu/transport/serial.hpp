#pragma once

#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>


#include "common/types.hpp"

class SerialPort
{
public:

    SerialPort() : fd_(-1), baud_(115200), open_(false), timeout_ms_(1000) {}
    ~SerialPort()
    {
        close();
    }

    Result<bool> open(const std::string& port);
    Result<bool> close();
    Result<size_t> write(const unsigned char* data, size_t len);
    Result<std::vector<unsigned char>> read(volatile bool& g_running);

    bool is_open();
    std::string get_port() const;
    int get_baud() const;
    void set_timeout_ms(int timeout_ms);
    void set_8N1(termios &tty);
private:

    int fd_;
    std::string port_;
    int baud_;
    struct termios original_tty_;
    bool open_;
    int timeout_ms_;

};
#include "transport/serial.hpp"
#include "common/protocol.hpp"
#include <poll.h>

Result<bool> SerialPort::open(const std::string& port)
{
    port_ = port;
    
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY);
    if(fd_ < 0)
    {
        std::cerr << "Error opening " << port << ": " << strerror(errno) << "\n";
        return Result<bool>::failure(Error::PORT_ERROR);
    }

    if(tcgetattr(fd_, &original_tty_) != 0)
    {
        std::cerr << "Error getting port attributes: " << strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return Result<bool>::failure(Error::PORT_ERROR);
    }

    struct termios tty;
    tcgetattr(fd_, &tty);  
    
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    cfmakeraw(&tty);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag |= CREAD | CLOCAL;
    
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 5;
    
    if(tcsetattr(fd_, TCSANOW, &tty) != 0)
    {
        std::cerr << "Error setting port attributes: " << strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return Result<bool>::failure(Error::PORT_ERROR);
    }

    tcflush(fd_, TCIOFLUSH);
    open_ = true;
    return Result<bool>::success(true);
}

Result<bool> SerialPort::close()
{
    if(fd_ >= 0)
    {
        tcsetattr(fd_, TCSANOW, &original_tty_);
        ::close(fd_);
        fd_ = -1;
        open_ = false;
        return Result<bool>::success(true);
    }
    return Result<bool>::success(false);
}

Result<size_t> SerialPort::write(const unsigned char* data, size_t len)
{
    if(fd_ < 0)
        return Result<size_t>::failure(Error::PORT_ERROR);

    ssize_t written = ::write(fd_, data, len);
    if(written < 0)
        return Result<size_t>::failure(Error::PORT_ERROR);

    tcdrain(fd_);
    return Result<size_t>::success(static_cast<size_t>(written));
}

Result<std::vector<unsigned char>> SerialPort::read(volatile bool& g_running)
{
    std::vector<unsigned char> buffer;
    
    if (fd_ < 0) 
        return Result<std::vector<unsigned char>>::failure(Error::PORT_ERROR);

    unsigned char temp[256];
    int elapsed = 0;
    int last_data = 0;

    while (elapsed < timeout_ms_ && g_running)
    {
        struct pollfd pfd = {fd_, POLLIN, 0};
        int ret = poll(&pfd, 1, 50);
        
        if (ret > 0)
        {
            ssize_t n = ::read(fd_, temp, sizeof(temp));
            if (n > 0)
            {
                buffer.insert(buffer.end(), temp, temp + n);
                last_data = 0;
            }
        }
        else if (ret < 0) {
            return Result<std::vector<unsigned char>>::failure(Error::PORT_ERROR);
        }

        elapsed += 50;
        last_data += 50;

        if (!buffer.empty() && last_data >= 100)
            break;
    }

    if (buffer.empty()) {
        return Result<std::vector<unsigned char>>::failure(Error::TIMEOUT);
    }

    return Result<std::vector<unsigned char>>::success(std::move(buffer));
}

bool SerialPort::is_open()
{
    return open_;
}

std::string SerialPort::get_port() const
{
    return port_;
}

int SerialPort::get_baud() const
{
    return baud_;
}

void SerialPort::set_timeout_ms(int timeout_ms)
{
    timeout_ms_ = timeout_ms;
}

void SerialPort::set_8N1(termios &tty)
{
}
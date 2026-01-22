#include "devices/nfc_reader.hpp"
#include <iomanip>


Nfc_reader::Nfc_reader() :fd_(-1), counter(0)
{
    auto run = init();
    if(!run.ok())
        std::cerr << "Failed to init NFC reader" << ": " << strerror(errno);

}

Result<bool>Nfc_reader::init()
{
    fd_ = ::open("/dev/ttymxc1", O_RDWR | O_NOCTTY);
    if (fd_ < 0)
        return Result<bool>::failure(Error::NFC_INIT);
    
    struct termios tty;

    tcgetattr(fd_, &tty);  
    
    cfsetispeed(&tty, B921600);
    cfsetospeed(&tty, B921600);
    cfmakeraw(&tty);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    if(tcsetattr(fd_, TCSANOW, &tty) != 0)
    {
        std::cerr << "Error setting port attributes: " << strerror(errno) << "\n";
        ::close(fd_);
        fd_ = -1;
        return Result<bool>::failure(Error::PORT_ERROR);
    }

    tcflush(fd_, TCIOFLUSH);
    initialized = auth();
    return Result<bool>::success(true);
}
Result<bool>Nfc_reader::send_command(uint8_t cmd, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> frame;
    frame.push_back(ADDR_REQ);
    frame.push_back(cmd);
    frame.push_back(counter++);

    frame.insert(frame.end(), data.begin(), data.end());

    ssize_t nw = write(fd_, frame.data(), frame.size());

    if(nw < 0)
        return Result<bool>::failure(Error::CMD_FAILURE);

    return Result<bool>::success(true);


}
Result<std::vector<uint8_t>>Nfc_reader::read_response(size_t len)
{
    std::vector<uint8_t> buffer(len);
    int n = read(fd_, buffer.data(), buffer.size());
    if (n > 0) buffer.resize(n);
    else buffer.clear();
    return Result<std::vector<uint8_t>>::success(buffer);
}


Result<bool>Nfc_reader::auth()
{
    send_command(AUTH_A);
    auto result = read_response(16);

    std::vector<uint8_t> key;
    if(result.value().size() < 4)
        std::cout << "[NFCController] No valid Auth Key received, continuing with empty key\n";

    else
        key.assign(result.value().begin() + 4, result.value().end());

    std::cout << "[NFCController] AUTH PART B" << std::endl;
    send_command(AUTH_B, key);
    auto result2 = read_response(64);

    if(result.value().empty() && !result2.value().empty())
        return Result<bool>::failure(Error::NFC_AUTH);
    return Result<bool>::success(true);
}

void Nfc_reader::start_NFC()
{
    while(initialized.ok())
    {
        std::cout << "[NFCController] ENABLE CARD READING" << std::endl;
        send_command(ENABLE);

        std::vector<uint8_t> frameBuffer;

        while (true) {
            uint8_t b = 0;
            ssize_t bytesRead = read(fd_, &b, 1);
            if (bytesRead > 0) {
                frameBuffer.push_back(b);
                std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b) << " " << std::flush;

                auto parsed = parseCardInfo(frameBuffer);
                if (parsed && parsed->service == READ_CARD && parsed->ack == 0) {
                    std::cout << std::endl;
                    std::cout << std::uppercase << std::hex
                            << "[NFCController]"
                            << " Scanned Card UID=" << parsed->uidHex
                            << " ATQA=0x" << std::setw(4) << std::setfill('0') << parsed->atqa
                            << " CT=0x" << std::setw(2) << static_cast<int>(parsed->ct)
                            << " SAK=0x" << std::setw(2) << static_cast<int>(parsed->sak);
                    if (!parsed->extraBytes.empty()) {
                        std::cout << " EXTRA=0x";
                        for (uint8_t extra : parsed->extraBytes) {
                            std::cout << std::setw(2) << static_cast<int>(extra);
                        }
                    }
                    std::cout << std::dec << std::nouppercase << std::setfill(' ') << std::endl;
                    break;
                }
            } else if (bytesRead < 0) {
                perror("[ERROR] Read failed");
                break;
            }
        }

        std::cout << std::dec << std::nouppercase << std::setfill(' ');
    }

}

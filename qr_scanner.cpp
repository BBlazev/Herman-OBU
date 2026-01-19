/*
 * QR Scanner Serial Communication Tool
 * For OBU (On-Board Unit) QR Reader
 * 
 * Protocol reverse-engineered from Java OBU application.
 * 
 * Build: g++ -o qr_scanner qr_scanner.cpp -lpthread -static
 * 
 * Usage:
 *   ./qr_scanner --help                   # Show help
 *   ./qr_scanner --test                   # Test all baud rates
 *   ./qr_scanner /dev/ttyUSB0 9600 --scan # Trigger a scan
 *   ./qr_scanner /dev/ttyUSB0 --listen    # Listen for data
 * 
 * Protocol Summary:
 *   - Serial: 8N1 (8 data bits, No parity, 1 stop bit)
 *   - Trigger ON:  0x16 0x54 0x0D
 *   - Trigger OFF: 0x16 0x55 0x0D
 *   - No Read response: "NR"
 *   - OK: second-to-last byte = 0x06
 *   - Error: second-to-last byte = 0x15
 *   - Frame ends with '.' (0x2E) or '!' (0x21)
 */

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
#include <signal.h>

// ============================================================================
// Protocol Constants (from reverse-engineered Java code)
// ============================================================================

namespace Protocol {
    // Commands
    const unsigned char TRIGGER_ON[]  = {0x16, 0x54, 0x0D};
    const unsigned char TRIGGER_OFF[] = {0x16, 0x55, 0x0D};
    const unsigned char PREFIX[]      = {0x16, 0x4D, 0x0D};
    
    // Response codes
    const unsigned char ACK = 0x06;
    const unsigned char NAK = 0x15;
    
    // Frame end markers
    const unsigned char END_DOT  = 0x2E;  // '.'
    const unsigned char END_EXCL = 0x21;  // '!'
    
    // Timing
    const int FRAME_TIMEOUT_MS = 200;
}

// ============================================================================
// Global for signal handling
// ============================================================================

volatile bool g_running = true;

void signalHandler(int) {
    g_running = false;
    std::cout << "\nInterrupted.\n";
}

// ============================================================================
// Serial Port Class
// ============================================================================

class SerialPort {
private:
    int fd_;
    std::string port_;
    int baud_;
    struct termios originalTty_;
    bool open_;

    speed_t baudToConstant(int baud) {
        switch(baud) {
            case 1200:   return B1200;
            case 2400:   return B2400;
            case 4800:   return B4800;
            case 9600:   return B9600;
            case 19200:  return B19200;
            case 38400:  return B38400;
            case 57600:  return B57600;
            case 115200: return B115200;
            case 230400: return B230400;
            default:     return B9600;
        }
    }

public:
    SerialPort() : fd_(-1), baud_(9600), open_(false) {}
    
    ~SerialPort() { close(); }

    bool open(const std::string& port, int baud = 9600) {
        port_ = port;
        baud_ = baud;
        
        fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) {
            std::cerr << "Error opening " << port << ": " << strerror(errno) << "\n";
            return false;
        }

        if (tcgetattr(fd_, &originalTty_) != 0) {
            std::cerr << "Error getting port attributes: " << strerror(errno) << "\n";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        struct termios tty;
        memset(&tty, 0, sizeof(tty));
        
        speed_t baudConst = baudToConstant(baud);
        cfsetospeed(&tty, baudConst);
        cfsetispeed(&tty, baudConst);

        // 8N1: 8 data bits, No parity, 1 stop bit
        tty.c_cflag &= ~PARENB;
        tty.c_cflag &= ~CSTOPB;
        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8;
        tty.c_cflag &= ~CRTSCTS;
        tty.c_cflag |= CREAD | CLOCAL;

        // Raw mode
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_oflag &= ~OPOST;
        tty.c_iflag &= ~(IXON | IXOFF | IXANY);
        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 1;

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            std::cerr << "Error setting port attributes: " << strerror(errno) << "\n";
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        tcflush(fd_, TCIOFLUSH);
        open_ = true;
        return true;
    }

    void close() {
        if (fd_ >= 0) {
            tcsetattr(fd_, TCSANOW, &originalTty_);
            ::close(fd_);
            fd_ = -1;
            open_ = false;
        }
    }

    bool write(const unsigned char* data, size_t len) {
        if (fd_ < 0) return false;
        ssize_t written = ::write(fd_, data, len);
        tcdrain(fd_);
        return (written >= 0 && (size_t)written == len);
    }

    std::vector<unsigned char> read(int timeoutMs = 1000) {
        std::vector<unsigned char> buffer;
        if (fd_ < 0) return buffer;

        unsigned char temp[256];
        int elapsed = 0;
        int lastData = 0;
        
        while (elapsed < timeoutMs && g_running) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd_, &fds);
            
            struct timeval tv = {0, 50000};  // 50ms
            
            if (select(fd_ + 1, &fds, NULL, NULL, &tv) > 0) {
                ssize_t n = ::read(fd_, temp, sizeof(temp));
                if (n > 0) {
                    buffer.insert(buffer.end(), temp, temp + n);
                    lastData = 0;
                }
            }
            
            elapsed += 50;
            lastData += 50;
            
            if (!buffer.empty() && lastData >= Protocol::FRAME_TIMEOUT_MS) {
                break;
            }
        }
        return buffer;
    }

    bool isOpen() const { return open_; }
    std::string getPort() const { return port_; }
    int getBaud() const { return baud_; }
};

// ============================================================================
// Helper Functions
// ============================================================================

void printHex(const std::vector<unsigned char>& data, const std::string& prefix = "") {
    std::cout << prefix << "[" << data.size() << " bytes] ";
    for (auto b : data) printf("%02X ", b);
    std::cout << "\n";
    
    // Print ASCII if printable
    bool printable = true;
    for (auto b : data) {
        if (b < 32 && b != '\r' && b != '\n' && b != '\t') {
            printable = false;
            break;
        }
    }
    if (printable && !data.empty()) {
        std::cout << prefix << "ASCII: \"";
        for (auto b : data) {
            if (b >= 32 && b < 127) std::cout << (char)b;
            else if (b == '\r') std::cout << "\\r";
            else if (b == '\n') std::cout << "\\n";
        }
        std::cout << "\"\n";
    }
}

std::string parseResponse(const std::vector<unsigned char>& data) {
    if (data.empty()) return "NO_RESPONSE";
    if (data.size() == 2 && data[0] == 'N' && data[1] == 'R') return "NO_READ";
    
    if (data.size() >= 2) {
        unsigned char status = data[data.size() - 2];
        if (status == Protocol::ACK) return "OK: " + std::string(data.begin(), data.end() - 2);
        if (status == Protocol::NAK) return "ERROR";
    }
    
    return "DATA: " + std::string(data.begin(), data.end());
}

std::vector<std::string> findPorts() {
    std::vector<std::string> ports;
    const char* paths[] = {
        "/dev/ttyQrReader", "/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyUSB2",
        "/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyS0", "/dev/ttyS1", NULL
    };
    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], F_OK) == 0) ports.push_back(paths[i]);
    }
    return ports;
}

void printHelp(const char* name) {
    std::cout << R"(
QR Scanner Serial Tool for OBU
==============================

Usage: )" << name << R"( [port] [baud] [command]

Commands:
  --scan      Trigger scan and read QR code (default)
  --listen    Continuously listen for incoming data
  --test      Test all common baud rates
  --info      Request device info (REVINF)
  --help      Show this help

Examples:
  )" << name << R"( /dev/ttyUSB0 9600 --scan
  )" << name << R"( /dev/ttyQrReader --test
  )" << name << R"( --listen

Common baud rates: 9600, 19200, 38400, 57600, 115200

Protocol (reverse-engineered):
  Serial: 8N1 (8 data bits, No parity, 1 stop bit)
  Trigger ON:  0x16 0x54 0x0D
  Trigger OFF: 0x16 0x55 0x0D
)";
}

// ============================================================================
// Main Commands
// ============================================================================

bool testBaud(const std::string& port, int baud) {
    std::cout << "Testing " << baud << " baud... ";
    std::cout.flush();
    
    SerialPort serial;
    if (!serial.open(port, baud)) {
        std::cout << "FAILED (can't open)\n";
        return false;
    }
    
    serial.write(Protocol::TRIGGER_ON, sizeof(Protocol::TRIGGER_ON));
    usleep(100000);
    
    auto response = serial.read(500);
    serial.write(Protocol::TRIGGER_OFF, sizeof(Protocol::TRIGGER_OFF));
    serial.close();
    
    if (response.empty()) {
        std::cout << "no response\n";
        return false;
    }
    
    std::cout << "GOT RESPONSE: ";
    for (auto b : response) printf("%02X ", b);
    std::cout << "\n";
    return true;
}

void doTest(const std::string& port) {
    std::cout << "Testing baud rates on " << port << "...\n\n";
    int bauds[] = {9600, 19200, 38400, 57600, 115200};
    for (int b : bauds) {
        testBaud(port, b);
        usleep(200000);
    }
}

void doScan(SerialPort& serial) {
    std::cout << "Sending TRIGGER ON...\n";
    serial.write(Protocol::TRIGGER_ON, sizeof(Protocol::TRIGGER_ON));
    
    std::cout << "Waiting for scan (5 sec timeout, Ctrl+C to cancel)...\n\n";
    
    auto response = serial.read(5000);
    
    if (!response.empty()) {
        printHex(response, "RX: ");
        std::cout << "Parsed: " << parseResponse(response) << "\n";
    } else {
        std::cout << "No response.\n";
    }
    
    std::cout << "\nSending TRIGGER OFF...\n";
    serial.write(Protocol::TRIGGER_OFF, sizeof(Protocol::TRIGGER_OFF));
}

void doListen(SerialPort& serial) {
    std::cout << "Listening... (Ctrl+C to stop)\n\n";
    
    while (g_running) {
        auto data = serial.read(100);
        if (!data.empty()) {
            printHex(data, "RX: ");
            std::cout << "Parsed: " << parseResponse(data) << "\n\n";
        }
    }
}

void doInfo(SerialPort& serial) {
    std::cout << "Sending REVINF command...\n";
    
    unsigned char cmd[9];
    memcpy(cmd, Protocol::PREFIX, 3);
    memcpy(cmd + 3, "REVINF", 6);
    
    serial.write(cmd, sizeof(cmd));
    
    auto response = serial.read(2000);
    if (!response.empty()) {
        printHex(response, "RX: ");
    } else {
        std::cout << "No response.\n";
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::string port;
    int baud = 9600;
    enum { CMD_SCAN, CMD_LISTEN, CMD_TEST, CMD_INFO, CMD_HELP } cmd = CMD_SCAN;
    
    // Parse args
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") cmd = CMD_HELP;
        else if (arg == "--scan") cmd = CMD_SCAN;
        else if (arg == "--listen") cmd = CMD_LISTEN;
        else if (arg == "--test") cmd = CMD_TEST;
        else if (arg == "--info") cmd = CMD_INFO;
        else if (arg[0] == '/') port = arg;
        else if (isdigit(arg[0])) baud = std::stoi(arg);
    }
    
    if (cmd == CMD_HELP) {
        printHelp(argv[0]);
        return 0;
    }
    
    // Find port if not specified
    auto ports = findPorts();
    
    std::cout << "=== QR Scanner Tool ===\n\n";
    std::cout << "Available ports: ";
    if (ports.empty()) std::cout << "(none found)";
    for (auto& p : ports) std::cout << p << " ";
    std::cout << "\n\n";
    
    if (port.empty()) {
        if (ports.empty()) {
            std::cerr << "No serial ports found. Specify manually.\n";
            return 1;
        }
        port = ports[0];
        std::cout << "Auto-selected: " << port << "\n";
    }
    
    // Test mode doesn't need persistent connection
    if (cmd == CMD_TEST) {
        doTest(port);
        return 0;
    }
    
    // Open port
    std::cout << "Opening " << port << " @ " << baud << " baud (8N1)...\n";
    
    SerialPort serial;
    if (!serial.open(port, baud)) {
        return 1;
    }
    std::cout << "Port opened.\n\n";
    
    // Execute command
    switch (cmd) {
        case CMD_SCAN:   doScan(serial); break;
        case CMD_LISTEN: doListen(serial); break;
        case CMD_INFO:   doInfo(serial); break;
        default: break;
    }
    
    serial.close();
    std::cout << "\nDone.\n";
    return 0;
}
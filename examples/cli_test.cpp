#include <iostream>
#include <signal.h>
#include "devices/mboard.hpp"
#include "devices/terminal.hpp"
#include "devices/qr_scanner.hpp"

volatile bool running = true;
void sig_handler(int) { running = false; }

int main()
{
    signal(SIGINT, sig_handler);
    
    std::cout << "=== OBU SDK Full Test ===" << std::endl;
    
    // Mboard
    SerialPort mboard_serial;
    if (mboard_serial.open("/dev/ttyS0").ok()) {
        Mboard mboard(mboard_serial);
        auto r = mboard.alive();
        if (r.ok()) {
            std::cout << "[OK] Mboard - Uptime: " << r.value().uptime_seconds << "s\n";
        } else {
            std::cout << "[FAIL] Mboard ALIVE\n";
        }
        mboard_serial.close();
    } else {
        std::cout << "[SKIP] Mboard port not available\n";
    }
    
    // Terminal
    SerialPort term_serial;
    if (term_serial.open("/dev/ttyUSB1").ok()) {
        Terminal terminal(term_serial);
        auto r = terminal.alive();
        if (r.ok()) {
            std::cout << "[OK] Terminal - HW: 0x" << std::hex << r.value().hw_version << std::dec << "\n";
        } else {
            std::cout << "[FAIL] Terminal ALIVE\n";
        }
        
        std::cout << "Beeping...\n";
        terminal.beep();
        term_serial.close();
    } else {
        std::cout << "[SKIP] Terminal port not available\n";
    }
    
    // QR Scanner
    SerialPort qr_serial;
    if (qr_serial.open("/dev/ttyACM0").ok()) {
        qr_serial.set_timeout_ms(5000);
        QrScanner qr(qr_serial);
        
        std::cout << "\nScan a QR code (5 sec timeout)...\n";
        qr.trigger_on();
        auto r = qr.read_code();
        qr.trigger_off();
        
        if (r.ok() && !r.value().empty()) {
            std::cout << "[OK] QR Code: " << r.value() << "\n";
        } else {
            std::cout << "[TIMEOUT] No QR code scanned\n";
        }
        qr_serial.close();
    } else {
        std::cout << "[SKIP] QR Scanner not found\n";
    }
    
    std::cout << "\n=== Done ===" << std::endl;
    return 0;
}
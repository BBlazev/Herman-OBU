#include <iostream>
#include "devices/mboard.hpp"
#include "devices/terminal.hpp"

int main()
{
    std::cout << "=== OBU SDK Test ===" << std::endl;
    
    // Mboard test
    SerialPort mboard_serial;
    auto open_result = mboard_serial.open("/dev/ttyS0");
    
    if (!open_result.ok()) {
        std::cerr << "Failed to open Mboard port" << std::endl;
        return 1;
    }
    
    std::cout << "Mboard port opened" << std::endl;
    
    Mboard mboard(mboard_serial);
    auto alive_result = mboard.alive();
    
    if (alive_result.ok()) {
        auto& resp = alive_result.value();
        std::cout << "Mboard ALIVE OK!" << std::endl;
        std::cout << "  Status: 0x" << std::hex << resp.status << std::dec << std::endl;
        std::cout << "  HW Ver: 0x" << std::hex << resp.hw_version << std::dec << std::endl;
        std::cout << "  SW Ver: 0x" << std::hex << resp.sw_version << std::dec << std::endl;
        std::cout << "  Uptime: " << resp.uptime_seconds << " seconds" << std::endl;
    } else {
        std::cerr << "Mboard ALIVE failed" << std::endl;
    }
    
    mboard_serial.close();
    
    // Terminal test
    SerialPort term_serial;
    auto term_open = term_serial.open("/dev/ttyUSB1");
    
    if (!term_open.ok()) {
        std::cerr << "Failed to open Terminal port" << std::endl;
        return 1;
    }
    
    std::cout << "\nTerminal port opened" << std::endl;
    
    Terminal terminal(term_serial);
    auto term_alive = terminal.alive();
    
    if (term_alive.ok()) {
        auto& resp = term_alive.value();
        std::cout << "Terminal ALIVE OK!" << std::endl;
        std::cout << "  Status: 0x" << std::hex << resp.status << std::dec << std::endl;
        std::cout << "  HW Ver: 0x" << std::hex << resp.hw_version << std::dec << std::endl;
        std::cout << "  SW Ver: 0x" << std::hex << resp.sw_version << std::dec << std::endl;
    } else {
        std::cerr << "Terminal ALIVE failed" << std::endl;
    }
    
    // Beep test
    std::cout << "\nSending BEEP..." << std::endl;
    auto beep_result = terminal.beep();
    if (beep_result.ok()) {
        std::cout << "BEEP sent!" << std::endl;
    } else {
        std::cerr << "BEEP failed" << std::endl;
    }
    
    term_serial.close();
    
    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;
}
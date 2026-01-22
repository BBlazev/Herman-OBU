#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QTimer>
#include "devices/mboard.hpp"
#include "devices/terminal.hpp"
#include "devices/qr_scanner.hpp"
#include "devices/nfc_reader.hpp"

class ObuGui : public QWidget
{
public:
    ObuGui() : mboard_(mboard_serial_), terminal_(term_serial_), qr_(qr_serial_)
    {
        setWindowTitle("OBU SDK Test");
        resize(400, 300);
        
        auto* layout = new QVBoxLayout(this);
        
        status_label_ = new QLabel("Status: Not connected");
        nfc_label_ = new QLabel("Status: NFC not working");
        layout->addWidget(status_label_);
        
        auto* btn_mboard = new QPushButton("Mboard ALIVE");
        connect(btn_mboard, &QPushButton::clicked, this, &ObuGui::testMboard);
        layout->addWidget(btn_mboard);
        
        auto* btn_terminal = new QPushButton("Terminal ALIVE");
        connect(btn_terminal, &QPushButton::clicked, this, &ObuGui::testTerminal);
        layout->addWidget(btn_terminal);
        
        auto* btn_beep = new QPushButton("BEEP");
        connect(btn_beep, &QPushButton::clicked, this, &ObuGui::beep);
        layout->addWidget(btn_beep);
        
        auto* btn_qr = new QPushButton("Scan QR");
        connect(btn_qr, &QPushButton::clicked, this, &ObuGui::scanQr);
        layout->addWidget(btn_qr);

        auto* btn_nfc = new QPushButton("NFC card");
        connect(btn_nfc, &QPushButton::clicked,this, &ObuGui::nfc)
        layout->addWidget(btn_nfc);
        
        qr_label_ = new QLabel("QR: -");
        layout->addWidget(qr_label_);
        
        // Open ports
        mboard_serial_.open("/dev/ttyS0");
        term_serial_.open("/dev/ttyUSB1");
        qr_serial_.open("/dev/ttyACM0");
        qr_serial_.set_timeout_ms(3000);
    }
    
private slots:
    void testMboard() {
        auto result = mboard_.alive();
        if (result.ok()) {
            status_label_->setText(QString("Mboard OK - Uptime: %1s").arg(result.value().uptime_seconds));
        } else {
            status_label_->setText("Mboard FAILED");
        }
    }
    
    void testTerminal() {
        auto result = terminal_.alive();
        if (result.ok()) {
            status_label_->setText(QString("Terminal OK - HW: 0x%1").arg(result.value().hw_version, 0, 16));
        } else {
            status_label_->setText("Terminal FAILED");
        }
    }
    
    void beep() {
        auto result = terminal_.beep();
        status_label_->setText(result.ok() ? "BEEP sent!" : "BEEP failed");
    }
    
    void scanQr() {
        status_label_->setText("Scanning...");
        qr_.trigger_on();
        
        auto result = qr_.read_code();
        qr_.trigger_off();
        
        if (result.ok()) {
            qr_label_->setText(QString("QR: %1").arg(QString::fromStdString(result.value())));
        } else {
            qr_label_->setText("QR: No code");
        }
        status_label_->setText("Ready");
    }

    void nfc()
    {
        if(nfc_.initialized.ok())
            nfc_label_->setText("NFC WORKING...");
        
    }

private:
    SerialPort mboard_serial_, term_serial_, qr_serial_;
    Mboard mboard_;
    Terminal terminal_;
    QrScanner qr_;
    Nfc_reader nfc_;
    QLabel* status_label_;
    QLabel* qr_label_;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ObuGui gui;
    gui.show();
    return app.exec();
}
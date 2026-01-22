#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QMetaObject>
#include <thread>
#include <atomic>

#include "devices/mboard.hpp"
#include "devices/terminal.hpp"
#include "devices/qr_scanner.hpp"
#include "devices/nfc_reader.hpp"

class ObuGui : public QWidget
{
    Q_OBJECT

public:
    ObuGui() : mboard_(mboard_serial_), terminal_(term_serial_), qr_(qr_serial_)
    {
        setWindowTitle("OBU SDK Test");
        resize(600, 400);
        
        auto* layout = new QVBoxLayout(this);
        
        status_label_ = new QLabel("Status: Not connected");
        layout->addWidget(status_label_);
        
        qr_label_ = new QLabel("QR: -");
        layout->addWidget(qr_label_);
        
        nfc_label_ = new QLabel("NFC: -");
        layout->addWidget(nfc_label_);
        
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

        auto* btn_nfc_start = new QPushButton("Start NFC");
        connect(btn_nfc_start, &QPushButton::clicked, this, &ObuGui::startNfc);
        layout->addWidget(btn_nfc_start);

        auto* btn_nfc_stop = new QPushButton("Stop NFC");
        connect(btn_nfc_stop, &QPushButton::clicked, this, &ObuGui::stopNfc);
        layout->addWidget(btn_nfc_stop);
        
        if (!mboard_serial_.open("/dev/ttyS0")) {
            status_label_->setText("Failed to open mboard port");
        }
        if (!term_serial_.open("/dev/ttyUSB1")) {
            status_label_->setText("Failed to open terminal port");
        }
        if (!qr_serial_.open("/dev/ttyACM0")) {
            status_label_->setText("Failed to open QR port");
        }
        qr_serial_.set_timeout_ms(3000);
    }
    
    ~ObuGui()
    {
        stopNfc();
    }

private slots:
    void testMboard() 
    {
        auto result = mboard_.alive();
        if (result.ok()) {
            status_label_->setText(
                QString("Mboard OK - Uptime: %1s, HW: 0x%2")
                    .arg(result.value().uptime_seconds)
                    .arg(result.value().hw_version, 4, 16, QChar('0'))
            );
        } else {
            status_label_->setText("Mboard FAILED");
        }
    }
    
    void testTerminal() 
    {
        auto result = terminal_.alive();
        if (result.ok()) {
            status_label_->setText(
                QString("Terminal OK - HW: 0x%1")
                    .arg(result.value().hw_version, 4, 16, QChar('0'))
            );
        } else {
            status_label_->setText("Terminal FAILED");
        }
    }
    
    void beep() 
    {
        auto result = terminal_.beep();
        status_label_->setText(result.ok() ? "BEEP sent!" : "BEEP failed");
    }
    
    void scanQr() 
    {
        status_label_->setText("Scanning...");
        QApplication::processEvents();  
        
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

    void startNfc()
    {
        if (nfc_running_.load()) {
            status_label_->setText("NFC already running");
            return;
        }
        
        if (!nfc_.is_initialized()) {
            nfc_label_->setText("NFC: Init failed");
            return;
        }
        
        nfc_.set_card_callback([this](const CardInfo& card) {
            QMetaObject::invokeMethod(this, [this, uid = card.uidHex]() {
                nfc_label_->setText(QString("NFC: %1").arg(QString::fromStdString(uid)));
            }, Qt::QueuedConnection);
        });
        
        nfc_running_.store(true);
        nfc_thread_ = std::thread([this]() { 
            nfc_.start(); 
            nfc_running_.store(false);
        });
        
        status_label_->setText("NFC scanning started");
        nfc_label_->setText("NFC: Waiting for card...");
    }
    
    void stopNfc()
    {
        if (!nfc_running_.load()) return;
        
        nfc_.stop();
        if (nfc_thread_.joinable()) {
            nfc_thread_.join();
        }
        status_label_->setText("NFC stopped");
        nfc_label_->setText("NFC: Stopped");
    }

private:
    SerialPort mboard_serial_, term_serial_, qr_serial_;
    Mboard mboard_;
    Terminal terminal_;
    QrScanner qr_;
    Nfc_reader nfc_;
    
    std::thread nfc_thread_;
    std::atomic<bool> nfc_running_{false};
    
    QLabel* status_label_;
    QLabel* qr_label_;
    QLabel* nfc_label_;
};

#include "qt_gui.moc"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ObuGui gui;
    gui.show();
    return app.exec();
}
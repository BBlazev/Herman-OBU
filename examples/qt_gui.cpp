#include <QApplication>
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMetaObject>
#include <QDateTime>
#include <QScrollBar>
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
        resize(500, 600);
        
        auto* layout = new QVBoxLayout(this);
        
        status_label_ = new QLabel("Status: Not connected");
        status_label_->setStyleSheet("font-weight: bold; padding: 5px;");
        layout->addWidget(status_label_);
        
        qr_label_ = new QLabel("QR: -");
        layout->addWidget(qr_label_);
        
        nfc_label_ = new QLabel("NFC: -");
        nfc_label_->setStyleSheet("font-size: 14px; color: #0066cc;");
        layout->addWidget(nfc_label_);
        
        auto* btn_layout = new QHBoxLayout();
        
        auto* btn_mboard = new QPushButton("Mboard ALIVE");
        connect(btn_mboard, &QPushButton::clicked, this, &ObuGui::testMboard);
        btn_layout->addWidget(btn_mboard);
        
        auto* btn_terminal = new QPushButton("Terminal ALIVE");
        connect(btn_terminal, &QPushButton::clicked, this, &ObuGui::testTerminal);
        btn_layout->addWidget(btn_terminal);
        
        layout->addLayout(btn_layout);
        
        auto* btn_layout2 = new QHBoxLayout();
        
        auto* btn_beep = new QPushButton("BEEP");
        connect(btn_beep, &QPushButton::clicked, this, &ObuGui::beep);
        btn_layout2->addWidget(btn_beep);
        
        auto* btn_qr = new QPushButton("Scan QR");
        connect(btn_qr, &QPushButton::clicked, this, &ObuGui::scanQr);
        btn_layout2->addWidget(btn_qr);
        
        layout->addLayout(btn_layout2);

        auto* btn_layout3 = new QHBoxLayout();
        
        btn_nfc_start_ = new QPushButton("Start NFC");
        connect(btn_nfc_start_, &QPushButton::clicked, this, &ObuGui::startNfc);
        btn_layout3->addWidget(btn_nfc_start_);

        btn_nfc_stop_ = new QPushButton("Stop NFC");
        btn_nfc_stop_->setEnabled(false);
        connect(btn_nfc_stop_, &QPushButton::clicked, this, &ObuGui::stopNfc);
        btn_layout3->addWidget(btn_nfc_stop_);
        
        layout->addLayout(btn_layout3);
        
        auto* log_label = new QLabel("Log:");
        layout->addWidget(log_label);
        
        log_text_ = new QTextEdit();
        log_text_->setReadOnly(true);
        log_text_->setStyleSheet("font-family: monospace; font-size: 11px;");
        layout->addWidget(log_text_);
        
        auto* btn_clear = new QPushButton("Clear Log");
        connect(btn_clear, &QPushButton::clicked, log_text_, &QTextEdit::clear);
        layout->addWidget(btn_clear);
        
        logMessage("[INIT] Opening ports...");
        
        if (mboard_serial_.open("/dev/ttyS0")) {
            logMessage("[INIT] Mboard port OK");
        } else {
            logMessage("[INIT] Mboard port FAILED");
        }
        
        if (term_serial_.open("/dev/ttyUSB1")) {
            logMessage("[INIT] Terminal port OK");
        } else {
            logMessage("[INIT] Terminal port FAILED");
        }
        
        if (qr_serial_.open("/dev/ttyACM0")) {
            logMessage("[INIT] QR port OK");
        } else {
            logMessage("[INIT] QR port FAILED");
        }
        qr_serial_.set_timeout_ms(3000);
        
        nfc_.set_log_callback([this](const std::string& msg) {
            QMetaObject::invokeMethod(this, [this, msg]() {
                logMessage(QString::fromStdString(msg));
            }, Qt::QueuedConnection);
        });
        
        if (nfc_.is_initialized()) {
            logMessage("[INIT] NFC initialized OK");
        } else {
            logMessage("[INIT] NFC init FAILED");
        }
        
        status_label_->setText("Status: Ready");
    }
    
    ~ObuGui()
    {
        stopNfc();
    }

private slots:
    void testMboard() 
    {
        logMessage("[MBOARD] Sending ALIVE...");
        auto result = mboard_.alive();
        if (result.ok()) {
            QString msg = QString("Mboard OK - Uptime: %1s, HW: 0x%2, SW: 0x%3")
                .arg(result.value().uptime_seconds)
                .arg(result.value().hw_version, 4, 16, QChar('0'))
                .arg(result.value().sw_version, 4, 16, QChar('0'));
            status_label_->setText(msg);
            logMessage("[MBOARD] " + msg);
        } else {
            status_label_->setText("Mboard FAILED");
            logMessage("[MBOARD] ALIVE failed");
        }
    }
    
    void testTerminal() 
    {
        logMessage("[TERMINAL] Sending ALIVE...");
        auto result = terminal_.alive();
        if (result.ok()) {
            QString msg = QString("Terminal OK - HW: 0x%1")
                .arg(result.value().hw_version, 4, 16, QChar('0'));
            status_label_->setText(msg);
            logMessage("[TERMINAL] " + msg);
        } else {
            status_label_->setText("Terminal FAILED");
            logMessage("[TERMINAL] ALIVE failed");
        }
    }
    
    void beep() 
    {
        logMessage("[TERMINAL] Sending BEEP...");
        auto result = terminal_.beep();
        if (result.ok()) {
            status_label_->setText("BEEP sent!");
            logMessage("[TERMINAL] BEEP OK");
        } else {
            status_label_->setText("BEEP failed");
            logMessage("[TERMINAL] BEEP failed");
        }
    }
    
    void scanQr() 
    {
        status_label_->setText("Scanning QR...");
        logMessage("[QR] Trigger ON, scanning...");
        QApplication::processEvents();
        
        qr_.trigger_on();
        auto result = qr_.read_code();
        qr_.trigger_off();
        
        if (result.ok()) {
            QString code = QString::fromStdString(result.value());
            qr_label_->setText(QString("QR: %1").arg(code));
            logMessage("[QR] Code: " + code);
        } else {
            qr_label_->setText("QR: No code");
            logMessage("[QR] No code detected");
        }
        status_label_->setText("Ready");
    }

    void startNfc()
    {
        if (nfc_running_.load()) {
            logMessage("[NFC] Already running");
            return;
        }
        
        if (!nfc_.is_initialized()) {
            nfc_label_->setText("NFC: Init failed");
            logMessage("[NFC] Cannot start - not initialized");
            return;
        }
        
        nfc_.set_card_callback([this](const CardInfo& card) {
            QMetaObject::invokeMethod(this, [this, card]() {
                QString uid = QString::fromStdString(card.uidHex);
                nfc_label_->setText(QString("NFC: %1").arg(uid));
                nfc_label_->setStyleSheet("font-size: 14px; color: #00aa00; font-weight: bold;");
            }, Qt::QueuedConnection);
        });
        
        nfc_running_.store(true);
        nfc_thread_ = std::thread([this]() { 
            nfc_.start(); 
            nfc_running_.store(false);
            
            QMetaObject::invokeMethod(this, [this]() {
                btn_nfc_start_->setEnabled(true);
                btn_nfc_stop_->setEnabled(false);
            }, Qt::QueuedConnection);
        });
        
        btn_nfc_start_->setEnabled(false);
        btn_nfc_stop_->setEnabled(true);
        status_label_->setText("NFC scanning...");
        nfc_label_->setText("NFC: Waiting for card...");
        nfc_label_->setStyleSheet("font-size: 14px; color: #0066cc;");
    }
    
    void stopNfc()
    {
        if (!nfc_running_.load()) return;
        
        nfc_.stop();
        if (nfc_thread_.joinable()) {
            nfc_thread_.join();
        }
        
        btn_nfc_start_->setEnabled(true);
        btn_nfc_stop_->setEnabled(false);
        status_label_->setText("Ready");
        nfc_label_->setText("NFC: Stopped");
        nfc_label_->setStyleSheet("font-size: 14px; color: #666666;");
    }

private:
    void logMessage(const QString& msg)
    {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        log_text_->append(QString("[%1] %2").arg(timestamp, msg));
        
        // Auto-scroll to bottom
        QScrollBar* sb = log_text_->verticalScrollBar();
        sb->setValue(sb->maximum());
    }

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
    QTextEdit* log_text_;
    QPushButton* btn_nfc_start_;
    QPushButton* btn_nfc_stop_;
};

#include "qt_gui.moc"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    ObuGui gui;
    gui.show();
    return app.exec();
}
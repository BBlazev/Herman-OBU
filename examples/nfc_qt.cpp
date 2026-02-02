#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QListWidget>
#include <QTcpSocket>
#include <QTimer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFont>
#include <QFrame>

class CorvusNfcGui : public QMainWindow
{
    Q_OBJECT

public:
    explicit CorvusNfcGui(QWidget* parent = nullptr) : QMainWindow(parent)
    {
        setWindowTitle("Corvus NFC Reader");
        setMinimumSize(480, 320);

        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* layout = new QVBoxLayout(central);
        layout->setSpacing(10);
        layout->setContentsMargins(15, 15, 15, 15);

        statusLabel_ = new QLabel("Disconnected");
        statusLabel_->setStyleSheet("font-size: 14px; color: #666;");
        layout->addWidget(statusLabel_);

        auto* uidFrame = new QFrame();
        uidFrame->setFrameStyle(QFrame::StyledPanel);
        uidFrame->setStyleSheet("background: #f0f0f0; border-radius: 8px;");
        auto* uidLayout = new QVBoxLayout(uidFrame);
        
        auto* uidTitle = new QLabel("Last UID:");
        uidTitle->setStyleSheet("font-size: 12px; color: #666;");
        uidLayout->addWidget(uidTitle);
        
        uidLabel_ = new QLabel("---");
        uidLabel_->setStyleSheet("font-size: 28px; font-weight: bold; color: #333;");
        uidLabel_->setAlignment(Qt::AlignCenter);
        uidLayout->addWidget(uidLabel_);
        
        layout->addWidget(uidFrame);

        auto* btnLayout = new QHBoxLayout();
        
        connectBtn_ = new QPushButton("Connect");
        connectBtn_->setStyleSheet("padding: 10px 20px; font-size: 14px;");
        connect(connectBtn_, &QPushButton::clicked, this, &CorvusNfcGui::onConnectClicked);
        btnLayout->addWidget(connectBtn_);
        
        readBtn_ = new QPushButton("Read NFC");
        readBtn_->setEnabled(false);
        readBtn_->setStyleSheet("padding: 10px 20px; font-size: 14px;");
        connect(readBtn_, &QPushButton::clicked, this, &CorvusNfcGui::startReading);
        btnLayout->addWidget(readBtn_);
        
        layout->addLayout(btnLayout);

        historyList_ = new QListWidget();
        historyList_->setMaximumHeight(120);
        layout->addWidget(historyList_);

        socket_ = new QTcpSocket(this);
        connect(socket_, &QTcpSocket::connected, this, &CorvusNfcGui::onConnected);
        connect(socket_, &QTcpSocket::disconnected, this, &CorvusNfcGui::onDisconnected);
        connect(socket_, &QTcpSocket::readyRead, this, &CorvusNfcGui::onReadyRead);

        keepAliveTimer_ = new QTimer(this);
        connect(keepAliveTimer_, &QTimer::timeout, this, &CorvusNfcGui::sendKeepAlive);
    }

private slots:
    void onConnectClicked()
    {
        if (socket_->state() == QAbstractSocket::ConnectedState) {
            socket_->disconnectFromHost();
        } else {
            statusLabel_->setText("Connecting...");
            socket_->connectToHost("127.0.0.1", 4543);
        }
    }

    void onConnected()
    {
        statusLabel_->setText("Connected - Checking terminal...");
        connectBtn_->setText("Disconnect");
        checkTerminal();
    }

    void onDisconnected()
    {
        statusLabel_->setText("Disconnected");
        connectBtn_->setText("Connect");
        readBtn_->setEnabled(false);
        keepAliveTimer_->stop();
    }

    void onReadyRead()
    {
        buffer_.append(socket_->readAll());

        while (buffer_.size() >= 2) {
            if (expectedLen_ == 0) {
                expectedLen_ = ((unsigned char)buffer_[0] << 8) | (unsigned char)buffer_[1];
                buffer_.remove(0, 2);
                if (expectedLen_ == 0) continue;
            }

            if (buffer_.size() < expectedLen_) return;

            QByteArray data = buffer_.left(expectedLen_);
            buffer_.remove(0, expectedLen_);
            expectedLen_ = 0;

            processResponse(data);
        }
    }

    void processResponse(const QByteArray& data)
    {
        QString resp = QString::fromLatin1(data);
        bool success = (data.size() >= 15 && data.mid(12, 3) == "000");

        if (pendingOp_ == "check") {
            if (success) {
                statusLabel_->setText("Terminal OK - Logging in...");
                logon();
            } else {
                statusLabel_->setText("Terminal error");
                keepAliveTimer_->stop();
            }
        }
        else if (pendingOp_ == "logon") {
            keepAliveTimer_->stop();
            if (success) {
                statusLabel_->setText("Ready - Press Read NFC");
                readBtn_->setEnabled(true);
            } else {
                statusLabel_->setText("Logon failed");
            }
        }
        else if (pendingOp_ == "read_uid") {
            int pos = resp.indexOf("95000");
            if (pos >= 0 && pos + 5 < resp.size()) {
                keepAliveTimer_->stop();
                QString uid = resp.mid(pos + 5);
                uidLabel_->setText(uid);
                uidLabel_->setStyleSheet("font-size: 28px; font-weight: bold; color: #2e7d32;");
                
                QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
                historyList_->insertItem(0, timestamp + " - " + uid);
                if (historyList_->count() > 10) {
                    delete historyList_->takeItem(10);
                }
                
                statusLabel_->setText("UID Read - Ready");
                readBtn_->setEnabled(true);
            }
        }
    }

    void checkTerminal()
    {
        pendingOp_ = "check";
        QString msg = "300000" + QString("%1").arg(counter_++, 4, 10, QChar('0')) + "01";
        sendMessage(msg.toLatin1());
        keepAliveTimer_->start(250);
    }

    void logon()
    {
        pendingOp_ = "logon";
        QByteArray paddedPwd(9, '\0');
        QByteArray pwd = "23646";
        memcpy(paddedPwd.data(), pwd.constData(), pwd.size());
        QString hash = QCryptographicHash::hash(paddedPwd, QCryptographicHash::Sha1).toHex().toUpper();
        
        QString msg = "010000" + QString("%1").arg(counter_++, 4, 10, QChar('0')) + "01L1;P" + hash;
        sendMessage(msg.toLatin1());
        keepAliveTimer_->start(250);
    }

    void startReading()
    {
        pendingOp_ = "read_uid";
        readBtn_->setEnabled(false);
        statusLabel_->setText("Place card on Ingenico...");
        uidLabel_->setStyleSheet("font-size: 28px; font-weight: bold; color: #1565c0;");
        uidLabel_->setText("Waiting...");
        
        QString msg = "010000" + QString("%1").arg(counter_++, 4, 10, QChar('0')) + "95";
        sendMessage(msg.toLatin1());
        keepAliveTimer_->start(250);
    }

    void sendKeepAlive()
    {
        char ka[2] = {0, 0};
        socket_->write(ka, 2);
        socket_->flush();
    }

    void sendMessage(const QByteArray& msg)
    {
        char len[2] = {(char)((msg.size() >> 8) & 0xFF), (char)(msg.size() & 0xFF)};
        socket_->write(len, 2);
        socket_->write(msg);
        socket_->flush();
    }

private:
    QTcpSocket* socket_;
    QTimer* keepAliveTimer_;
    QLabel* statusLabel_;
    QLabel* uidLabel_;
    QPushButton* connectBtn_;
    QPushButton* readBtn_;
    QListWidget* historyList_;
    QByteArray buffer_;
    QString pendingOp_;
    int expectedLen_ = 0;
    int counter_ = 0;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    CorvusNfcGui window;
    window.show();
    return app.exec();
}

#include "nfc_qt.moc"
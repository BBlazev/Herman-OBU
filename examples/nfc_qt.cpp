#include <QCoreApplication>
#include <QTcpSocket>
#include <QTimer>
#include <QDebug>
#include <QByteArray>
#include <QCryptographicHash>

/**
 * Corvus NFC Reader - Qt Example
 * 
 * Reads NFC card UIDs via Ingenico iUC160B terminal through ECRProxy.
 * 
 * Protocol (based on LIIngenicoECR.java):
 * - Messages: 2-byte length (big endian) + ASCII payload
 * - Check terminal: "300000" + seq(4) + "01" -> "32...000" = OK
 * - Logon: "010000" + seq(4) + "01" + "L1;P" + SHA1(password) -> "11...000" = OK  
 * - Read NFC UID: "010000" + seq(4) + "95" -> keepalive -> "11...95000" + UID
 * - Keepalive: send 0x00 0x00 every 250ms while waiting
 */
class CorvusNfcQt : public QObject
{
    Q_OBJECT
    
public:
    explicit CorvusNfcQt(QObject* parent = nullptr)
        : QObject(parent)
        , socket_(new QTcpSocket(this))
        , counter_(0)
        , expectedLen_(0)
    {
        connect(socket_, &QTcpSocket::connected, this, &CorvusNfcQt::onConnected);
        connect(socket_, &QTcpSocket::disconnected, this, &CorvusNfcQt::onDisconnected);
        connect(socket_, &QTcpSocket::readyRead, this, &CorvusNfcQt::onReadyRead);
        connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error),
                this, &CorvusNfcQt::onError);
                
        keepAliveTimer_ = new QTimer(this);
        connect(keepAliveTimer_, &QTimer::timeout, this, &CorvusNfcQt::sendKeepAlive);
    }
    
    void connectToTerminal(const QString& host = "127.0.0.1", int port = 4543)
    {
        qDebug() << "Connecting to ECRProxy at" << host << ":" << port;
        socket_->connectToHost(host, port);
    }
    
    void checkTerminal()
    {
        qDebug() << "Checking terminal...";
        pendingOp_ = "check";
        
        // Format: "300000" + seq(4) + "01"
        QString seqNum = QString("%1").arg(nextCounter(), 4, 10, QChar('0'));
        QString msg = "300000" + seqNum + "01";
        
        sendMessage(msg.toLatin1());
        keepAliveTimer_->start(250);
    }
    
    void logon(const QString& operatorId = "1", const QString& password = "23646")
    {
        qDebug() << "Logging in...";
        pendingOp_ = "logon";
        
        // Password hash: SHA1 of password padded to 9 bytes
        QByteArray paddedPwd(9, '\0');
        QByteArray pwdBytes = password.toLatin1();
        memcpy(paddedPwd.data(), pwdBytes.constData(), qMin(pwdBytes.size(), 9));
        QString hash = QCryptographicHash::hash(paddedPwd, QCryptographicHash::Sha1).toHex().toUpper();
        
        // Format: "010000" + seq(4) + "01" + "L" + operatorId + ";P" + hash
        QString seqNum = QString("%1").arg(nextCounter(), 4, 10, QChar('0'));
        QString msg = "010000" + seqNum + "01L" + operatorId + ";P" + hash;
        
        sendMessage(msg.toLatin1());
        keepAliveTimer_->start(250);
    }
    
    void readNfcUid()
    {
        qDebug() << "Reading NFC UID... Place card on Ingenico!";
        pendingOp_ = "read_uid";
        
        // Format: "010000" + seq(4) + "95"
        QString seqNum = QString("%1").arg(nextCounter(), 4, 10, QChar('0'));
        QString msg = "010000" + seqNum + "95";
        
        sendMessage(msg.toLatin1());
        keepAliveTimer_->start(250);
    }

signals:
    void connected();
    void disconnected();
    void terminalReady();
    void logonComplete();
    void uidRead(const QString& uid);
    void error(const QString& message);

private slots:
    void onConnected()
    {
        qDebug() << "Connected to ECRProxy";
        emit connected();
    }
    
    void onDisconnected()
    {
        qDebug() << "Disconnected";
        keepAliveTimer_->stop();
        emit disconnected();
    }
    
    void onReadyRead()
    {
        buffer_.append(socket_->readAll());
        
        while (buffer_.size() >= 2) {
            if (expectedLen_ == 0) {
                expectedLen_ = ((unsigned char)buffer_[0] << 8) | (unsigned char)buffer_[1];
                buffer_.remove(0, 2);
                
                if (expectedLen_ == 0) {
                    continue;  // keepalive response
                }
            }
            
            if (buffer_.size() < expectedLen_) {
                return;  // wait for more data
            }
            
            QByteArray data = buffer_.left(expectedLen_);
            buffer_.remove(0, expectedLen_);
            expectedLen_ = 0;
            
            qDebug() << "Received:" << data;
            processResponse(data);
        }
    }
    
    void processResponse(const QByteArray& data)
    {
        QString resp = QString::fromLatin1(data);
        
        // Check for success code "000" at position 12-15
        bool success = (data.size() >= 15 && data.mid(12, 3) == "000");
        
        if (pendingOp_ == "check") {
            keepAliveTimer_->stop();
            if (success) {
                qDebug() << "Terminal is operational";
                emit terminalReady();
            } else {
                emit error("Terminal not operational");
            }
        }
        else if (pendingOp_ == "logon") {
            keepAliveTimer_->stop();
            if (success) {
                qDebug() << "Logon successful";
                emit logonComplete();
            } else {
                emit error("Logon failed");
            }
        }
        else if (pendingOp_ == "read_uid") {
            // Look for "95000" followed by UID
            int pos = resp.indexOf("95000");
            if (pos >= 0 && pos + 5 < resp.size()) {
                keepAliveTimer_->stop();
                QString uid = resp.mid(pos + 5);
                qDebug() << "=== NFC UID:" << uid << "===";
                emit uidRead(uid);
            } else if (resp.contains("95001")) {
                // Error 001 = not logged in, retry logon
                qDebug() << "Not logged in, retrying...";
            }
        }
    }
    
    void onError(QAbstractSocket::SocketError err)
    {
        Q_UNUSED(err)
        qDebug() << "Socket error:" << socket_->errorString();
        keepAliveTimer_->stop();
        emit error(socket_->errorString());
    }
    
    void sendKeepAlive()
    {
        char keepalive[2] = {0, 0};
        socket_->write(keepalive, 2);
        socket_->flush();
    }

private:
    uint16_t nextCounter()
    {
        counter_ = (counter_ + 1) % 10000;
        return counter_;
    }
    
    void sendMessage(const QByteArray& msg)
    {
        // 2-byte length (big endian) + payload
        char lenBuf[2];
        lenBuf[0] = (msg.size() >> 8) & 0xFF;
        lenBuf[1] = msg.size() & 0xFF;
        
        qDebug() << "Sending:" << msg;
        
        socket_->write(lenBuf, 2);
        socket_->write(msg);
        socket_->flush();
    }
    
    QTcpSocket* socket_;
    QTimer* keepAliveTimer_;
    QByteArray buffer_;
    uint16_t counter_;
    QString pendingOp_;
    int expectedLen_;
};

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    
    CorvusNfcQt reader;
    
    // State machine: connect -> check -> logon -> read
    QObject::connect(&reader, &CorvusNfcQt::connected, [&reader]() {
        reader.checkTerminal();
    });
    
    QObject::connect(&reader, &CorvusNfcQt::terminalReady, [&reader]() {
        reader.logon("1", "23646");
    });
    
    QObject::connect(&reader, &CorvusNfcQt::logonComplete, [&reader]() {
        reader.readNfcUid();
    });
    
    QObject::connect(&reader, &CorvusNfcQt::uidRead, [](const QString& uid) {
        qDebug() << "\n*** SUCCESS! NFC UID:" << uid << "***\n";
        QCoreApplication::quit();
    });
    
    QObject::connect(&reader, &CorvusNfcQt::error, [](const QString& msg) {
        qDebug() << "=== Error:" << msg << "===";
        QCoreApplication::quit();
    });
    
    QTimer::singleShot(60000, []() {
        qDebug() << "=== Timeout (60 sec) ===";
        QCoreApplication::quit();
    });
    
    reader.connectToTerminal();
    
    return app.exec();
}

#include "nfc_qt.moc"

#include <QCoreApplication>
#include <QTcpSocket>
#include <QTimer>
#include <QDebug>
#include <QByteArray>

class CorvusNfcQt : public QObject
{
    Q_OBJECT
    
public:
    explicit CorvusNfcQt(QObject* parent = nullptr)
        : QObject(parent)
        , socket_(new QTcpSocket(this))
        , counter_(0)
    {
        connect(socket_, &QTcpSocket::connected, this, &CorvusNfcQt::onConnected);
        connect(socket_, &QTcpSocket::disconnected, this, &CorvusNfcQt::onDisconnected);
        connect(socket_, &QTcpSocket::readyRead, this, &CorvusNfcQt::onReadyRead);
        connect(socket_, &QTcpSocket::errorOccurred, this, &CorvusNfcQt::onError);
    }
    
    void connectToTerminal(const QString& host = "127.0.0.1", int port = 4543)
    {
        qDebug() << "Connecting to" << host << ":" << port;
        socket_->connectToHost(host, port);
    }
    
    void logon(const QString& opId = "1", const QString& password = "23646")
    {
        qDebug() << "Sending logon...";
        pendingOp_ = "logon";
        QByteArray msg = buildLogonMsg(nextCounter(), opId, password);
        socket_->write(msg);
    }
    
    void readNfcUid()
    {
        qDebug() << "Reading NFC UID...";
        pendingOp_ = "read_uid";
        QByteArray msg = buildReadUidMsg(nextCounter());
        socket_->write(msg);
    }
    
    void readCardData()
    {
        qDebug() << "Reading card data...";
        pendingOp_ = "read_card";
        QByteArray msg = buildReadCardMsg(nextCounter());
        socket_->write(msg);
    }

signals:
    void connected();
    void disconnected();
    void logonComplete(bool success);
    void uidRead(const QString& uid);
    void cardDataRead(const QString& pan);
    void error(const QString& message);

private slots:
    void onConnected()
    {
        qDebug() << "Connected to terminal";
        emit connected();
    }
    
    void onDisconnected()
    {
        qDebug() << "Disconnected from terminal";
        emit disconnected();
    }
    
    void onReadyRead()
    {
        QByteArray data = socket_->readAll();
        qDebug() << "Received:" << data.toHex();
        
        if (pendingOp_ == "logon") {
            emit logonComplete(true);
        } else if (pendingOp_ == "read_uid") {
            QString uid = parseUidResponse(data);
            if (!uid.isEmpty()) {
                emit uidRead(uid);
            }
        } else if (pendingOp_ == "read_card") {
            QString pan = parseCardResponse(data);
            if (!pan.isEmpty()) {
                emit cardDataRead(pan);
            }
        }
        
        pendingOp_.clear();
    }
    
    void onError(QAbstractSocket::SocketError err)
    {
        Q_UNUSED(err)
        qDebug() << "Socket error:" << socket_->errorString();
        emit error(socket_->errorString());
    }

private:
    static constexpr char STX = 0x02;
    static constexpr char ETX = 0x03;
    static constexpr char FS = 0x1C;
    
    uint16_t nextCounter()
    {
        counter_ = (counter_ + 1) % 10000;
        return counter_;
    }
    
    static uint8_t calculateLrc(const QByteArray& data)
    {
        uint8_t lrc = 0;
        for (char c : data) {
            lrc ^= static_cast<uint8_t>(c);
        }
        return lrc;
    }
    
    QByteArray buildLogonMsg(uint16_t counter, const QString& opId, const QString& pwd)
    {
        QString payload = QString("0100900000%1%2%3%4%5%6")
            .arg(counter, 4, 10, QChar('0'))
            .arg(FS).arg("01").arg(opId)
            .arg(FS).arg("02").arg(pwd);
        
        QByteArray msg;
        msg.append(STX);
        msg.append(payload.toLatin1());
        msg.append(ETX);
        msg.append(static_cast<char>(calculateLrc(payload.toLatin1())));
        return msg;
    }
    
    QByteArray buildReadUidMsg(uint16_t counter)
    {
        QString payload = QString("0200900100%1").arg(counter, 4, 10, QChar('0'));
        
        QByteArray msg;
        msg.append(STX);
        msg.append(payload.toLatin1());
        msg.append(ETX);
        msg.append(static_cast<char>(calculateLrc(payload.toLatin1())));
        return msg;
    }
    
    QByteArray buildReadCardMsg(uint16_t counter)
    {
        QString payload = QString("0200900200%1").arg(counter, 4, 10, QChar('0'));
        
        QByteArray msg;
        msg.append(STX);
        msg.append(payload.toLatin1());
        msg.append(ETX);
        msg.append(static_cast<char>(calculateLrc(payload.toLatin1())));
        return msg;
    }
    
    QString parseUidResponse(const QByteArray& data)
    {
        int fsPos = data.indexOf(FS);
        while (fsPos >= 0 && fsPos + 2 < data.size()) {
            if (data[fsPos + 1] == '6' && data[fsPos + 2] == '3') {
                int start = fsPos + 3;
                int end = data.indexOf(FS, start);
                if (end < 0) end = data.indexOf(ETX, start);
                if (end > start) {
                    return QString::fromLatin1(data.mid(start, end - start));
                }
            }
            fsPos = data.indexOf(FS, fsPos + 1);
        }
        return QString();
    }
    
    QString parseCardResponse(const QByteArray& data)
    {
        int fsPos = data.indexOf(FS);
        while (fsPos >= 0 && fsPos + 2 < data.size()) {
            if (data[fsPos + 1] == '3' && data[fsPos + 2] == '5') {
                int start = fsPos + 3;
                int end = data.indexOf(FS, start);
                if (end < 0) end = data.indexOf(ETX, start);
                if (end > start) {
                    return QString::fromLatin1(data.mid(start, end - start));
                }
            }
            fsPos = data.indexOf(FS, fsPos + 1);
        }
        return QString();
    }
    
    QTcpSocket* socket_;
    uint16_t counter_;
    QString pendingOp_;
};

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    
    CorvusNfcQt reader;
    
    QObject::connect(&reader, &CorvusNfcQt::connected, [&reader]() {
        qDebug() << "=== Connected, sending logon ===";
        reader.logon();
    });
    
    QObject::connect(&reader, &CorvusNfcQt::logonComplete, [&reader](bool ok) {
        if (ok) {
            qDebug() << "=== Logon OK, reading NFC UID ===";
            reader.readNfcUid();
        }
    });
    
    QObject::connect(&reader, &CorvusNfcQt::uidRead, [](const QString& uid) {
        qDebug() << "=== NFC UID:" << uid << "===";
        QCoreApplication::quit();
    });
    
    QObject::connect(&reader, &CorvusNfcQt::error, [](const QString& msg) {
        qDebug() << "=== Error:" << msg << "===";
        QCoreApplication::quit();
    });
    
    QTimer::singleShot(30000, []() {
        qDebug() << "=== Timeout ===";
        QCoreApplication::quit();
    });
    
    reader.connectToTerminal();
    
    return app.exec();
}

#include "corvus_nfc_qt_example.moc"
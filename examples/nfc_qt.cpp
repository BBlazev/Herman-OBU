#include <QCoreApplication>
#include <QTcpSocket>
#include <QTimer>
#include <QDebug>
#include <QByteArray>
#include <QDataStream>

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
        qDebug() << "Connecting to" << host << ":" << port;
        socket_->connectToHost(host, port);
    }
    
    void readNfcUid()
    {
        qDebug() << "Reading NFC UID... Place card on Ingenico";
        pendingOp_ = "read_uid";
        
        // Format: "010000" + seqNum(4 digits) + "95" (read NFC UID command)
        QString msg = QString("010000%195")
            .arg(nextCounter(), 4, 10, QChar('0'));
        
        sendMessage(msg);
        keepAliveTimer_->start(500);
    }

signals:
    void connected();
    void disconnected();
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
                    continue;
                }
                qDebug() << "Expecting" << expectedLen_ << "bytes";
            }
            
            if (buffer_.size() < expectedLen_) {
                return;
            }
            
            QByteArray data = buffer_.left(expectedLen_);
            buffer_.remove(0, expectedLen_);
            expectedLen_ = 0;
            
            qDebug() << "Received:" << data;
            
            if (pendingOp_ == "read_uid" && data.length() >= 15) {
                QString respCode = QString::fromLatin1(data.mid(12, 3));
                qDebug() << "Response code:" << respCode;
                
                if (respCode == "000") {
                    QString uid = QString::fromLatin1(data.mid(15)).trimmed();
                    keepAliveTimer_->stop();
                    emit uidRead(uid);
                    return;
                }
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
    
    void sendMessage(const QString& msg)
    {
        QByteArray data = msg.toLatin1();
        
        char lenBuf[2];
        lenBuf[0] = (data.length() >> 8) & 0xFF;
        lenBuf[1] = data.length() & 0xFF;
        
        qDebug() << "Sending:" << data;
        
        socket_->write(lenBuf, 2);
        socket_->write(data);
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
    
    QObject::connect(&reader, &CorvusNfcQt::connected, [&reader]() {
        qDebug() << "=== Connected, reading NFC UID ===";
        reader.readNfcUid();
    });
    
    QObject::connect(&reader, &CorvusNfcQt::uidRead, [](const QString& uid) {
        qDebug() << "=== SUCCESS! NFC UID:" << uid << "===";
        QCoreApplication::quit();
    });
    
    QObject::connect(&reader, &CorvusNfcQt::error, [](const QString& msg) {
        qDebug() << "=== Error:" << msg << "===";
        QCoreApplication::quit();
    });
    
    QTimer::singleShot(30000, []() {
        qDebug() << "=== Timeout (30 sec) ===";
        QCoreApplication::quit();
    });
    
    reader.connectToTerminal();
    
    return app.exec();
}

#include "nfc_qt.moc"
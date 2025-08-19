#ifndef COMTHREAD_H
#define COMTHREAD_H

#include <QThread>
#include <QSerialPort>
#include <QSerialPortInfo>

class ComThread : public QThread
{
    Q_OBJECT

public:
    explicit ComThread(const QString &portName = QString(), QObject *parent = nullptr);
    ~ComThread() override;

    void setPortName(const QString &name);
    void setBaudRate(qint32 baudRate);
    void stop();

signals:
    void portOpened(const QString &portName);
    void distanceReceived(float distance);
    void parseError(const QString &line);
    void portOpenFailed(const QString &errorString);

protected:
    void run() override;

private:
    void processBuffer();

    QString      m_portName;
    qint32       m_baudRate = QSerialPort::Baud115200;
    bool         m_running  = false;
    QSerialPort  m_serial;
    QByteArray   m_buffer;
};

#endif // COMTHREAD_H

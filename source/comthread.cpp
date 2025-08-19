#include "comthread.h"
#include <qdebug.h>

ComThread::ComThread(const QString &portName, QObject *parent)
    : QThread(parent), m_portName(portName) {
    setObjectName(QStringLiteral("ComThread[%1]").arg(portName));
}

ComThread::~ComThread()
{
    stop();
    wait();
}

void ComThread::setPortName(const QString &name)
{
    m_portName = name;
}

void ComThread::setBaudRate(qint32 baudRate)
{
    m_baudRate = baudRate;
}

void ComThread::stop()
{
    m_running = false;
}

void ComThread::run()
{
    m_serial.setPortName(m_portName);
    m_serial.setBaudRate(m_baudRate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    connect(&m_serial, &QSerialPort::errorOccurred, this,
            [this](QSerialPort::SerialPortError e){
                if (e == QSerialPort::ResourceError || e == QSerialPort::ReadError || e == QSerialPort::DeviceNotFoundError)
                    m_running = false;
            });


    if (!m_serial.open(QIODevice::ReadOnly)) {
        emit portOpenFailed(m_serial.errorString());
        return;
    }
    emit portOpened(m_portName);

    m_running = true;
    m_buffer.clear();

    while (m_running && m_serial.isOpen()) {
        if (m_serial.waitForReadyRead(100)) {
            m_buffer += m_serial.readAll();
            // qDebug()<<"READ :"<<m_buffer;
            processBuffer();
        }
    }
    m_serial.close();
}

void ComThread::processBuffer()
{
    int idx;
    while ((idx = m_buffer.indexOf('\n')) >= 0) {
        QByteArray line = m_buffer.left(idx).trimmed();
        m_buffer.remove(0, idx + 1);

        bool ok;
        float dist = line.toFloat(&ok);
        if (ok)
            emit distanceReceived(dist);
        else
            emit parseError(QString::fromUtf8(line));
    }
}

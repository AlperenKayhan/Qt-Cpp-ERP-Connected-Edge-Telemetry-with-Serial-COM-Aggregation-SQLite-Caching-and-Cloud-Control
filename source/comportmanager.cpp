#include "comportmanager.h"
#include "comthread.h"
#include "dvclient.h"
#include <QSerialPortInfo>
#include <QDebug>

ComPortManager::ComPortManager(DvClient* client, QObject* parent): QObject(parent), m_client(client)
{//başlangıç için bir reload at
    reloadPorts();
}

ComPortManager::~ComPortManager()
{
    clearAll();
}

QStringList ComPortManager::availablePorts() const
{
    QStringList out;
    const auto ports = QSerialPortInfo::availablePorts();
    //portları oports kaydetiyoruz//COMlarda var olanlarıın numaralarını ports kaydetir
    for (const auto &p : ports) out << p.portName();//get all the port names which are not NULL
    return out;
}

void ComPortManager::setModeIdle()
{/*From the start our code will automatically start on Idle state,
    however; to see the existing ports, we do relod operation*/
    m_mode = Mode::Idle;
    reloadPorts();
}

void ComPortManager::setModeSingle(const QString &portName)
{//chose one port, not work more than one
    m_mode = Mode::SinglePort;
    m_selectedPort = portName;
    reloadPorts();
}

void ComPortManager::setModeSimulation()
{//sim'e gir
    m_mode = Mode::SimulationOnly;
    reloadPorts();
}

void ComPortManager::reloadPorts()
{//renew the ports, and delete the existing ones after that call it again
    clearAll();
    startAll();
}

void ComPortManager::stopAll()
{
    /*Normally direct existence of clearAll() would be enough. However, in dvClient we re-need to use clearAll on some areas
     ,importantly after the dvclient got updated. Thus, rather calling "~", we create stop all redo similar process
        -> Basically, for code clearty
    */
    /*
     * Divice clientta çağrılıyor
    DvClient::~DvClient()
    {
        m_portManager->stopAll();
        delete m_portManager;
    }*/
    clearAll();
}

void ComPortManager::startAll()
{
    m_openCount = 0;

    if (m_mode == Mode::Idle)
    {//burda bir şey yapmıyok açılışta sabit kalması için
        //Idle state
        m_client->setCOMSentinel(0);
        return;
    }

    if (m_mode == Mode::SimulationOnly)
    {//selection for error sim
        m_client->setCOMSentinel(1);
        emit allPortsClosed();
        return;
    }

    if (m_mode == Mode::SinglePort)
    {//Ensouring that single port is working not multiple as same time
        if (m_selectedPort.isEmpty())
        {
            emit allPortsClosed();
            m_client->setCOMSentinel(1);
            return;
        }

        ComThread *thread = new ComThread(m_selectedPort, this);
        const QString portName = m_selectedPort; // capture by value for safety
        thread->setBaudRate(QSerialPort::Baud115200);
        connect(thread, &ComThread::portOpened, this, &ComPortManager::onPortOpened);
        connect(thread, &ComThread::distanceReceived, this, &ComPortManager::onDistance);
        connect(thread, &ComThread::parseError, this, [portName](const QString &line){
            qWarning() << "error on" << portName << ":" << line;
        });

        connect(thread, &ComThread::portOpenFailed, this, &ComPortManager::onPortOpenFailed);
        connect(thread, &QThread::finished, this, &ComPortManager::onThreadFinished);
        thread->start();
        m_threads.append(thread);
        if (m_openCount == 0)
        {// will flip to 0->1 on portOpened, setting as open for current port
            m_client->setCOMSentinel(1);
        }
        return;
    }

    if (m_openCount == 0) {
        emit allPortsClosed();/*ne idle ne sibgleport ne simulationonly, thre
        which, means if the user re select "select port" on COMs closed all the existing coms
        and their corresponding thread*/
        m_client->setCOMSentinel(1);
    }
}

void ComPortManager::clearAll()
{
    for (ComThread *thread : m_threads) {
        qDebug() << "Stopping: " << thread->objectName();
        thread->stop();//   m_running = false;
        thread->wait();//   QThread in-build function
        /*burada şöyle düşündüm her ne kadar thread yerine başka bir thread kullanıyor olsada, var olan threadin silinmesi yerine bekletilmesi
        gerekiyor. Zira; aynı thread tekrar kullanılabilir*/
        qDebug() << "Stopped: " << thread->objectName();
        delete thread;// Threadi şimdi siliyoruyz,
    }
    m_threads.clear(); //QVector<ComThread*> m_threads; olarak tanımsadım ve her bir vector listedir,direk clear atabilirim free yapmam gerekemez
}

void ComPortManager::onPortOpened(const QString &portName)
{
    ++m_openCount;
    m_client->setCOMSentinel(0);
    emit anyPortOpened();
    qDebug() << "Port opened:" << portName;
}

void ComPortManager::onDistance(float distance)
{//mesafe updateleme
    m_client->updateDistance(distance);
}

void ComPortManager::onPortOpenFailed(const QString &err)
{//kullanılmıyor ama dursun yinede ilerde bir şey eklenme durumu için
    qWarning() << "Failed to open port:" << err;
}

void ComPortManager::onThreadFinished()
{
    if (m_openCount > 0) --m_openCount; else m_openCount = 0; //orjinal "m_openCount--;" chat sordum böyle yap dedi
    if (m_openCount < 0) {
        emit allPortsClosed();
        m_client->setCOMSentinel(0);
        //m_client->setSerialActive(false);
        if (m_mode != Mode::SimulationOnly) {
            qInfo() << "All serial ports closed - stopping sensor reading.";
            m_client->setErrorSimulation(false);   // <-- auto stop
        }
    }


}

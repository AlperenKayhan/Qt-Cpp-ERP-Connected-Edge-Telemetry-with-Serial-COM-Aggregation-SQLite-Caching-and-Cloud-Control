#include "comportmanager.h"
#include "comthread.h"
#include "dvclient.h"
#include <QSerialPortInfo>
#include <QDebug>

ComPortManager::ComPortManager(DvClient* client, QObject* parent): QObject(parent), m_client(client)
{/* Here is my COM thread ports main, we firstly for from  client and parent form DvClient AND comthread respectivlly.
    After that, we call the "reloadPorts" function to reload our ports. */
    //başlangıç için bir reload at
    reloadPorts();
}

ComPortManager::~ComPortManager()
{/* DESTRUCTOR: this will terminate the class and call the "clearAll()" function for precaution.
    By calling that function, we can safely clear our threads. */
    clearAll();
}

QStringList ComPortManager::availablePorts() const
{/* It is a function that shows us the available ports on the device that are usable in our context*/
    QStringList out;
    const auto ports = QSerialPortInfo::availablePorts();
    //portları oports kaydetiyoruz//COMlarda var olanlarıın numaralarını ports kaydetir
    for (const auto &p : ports) out << p.portName();//get all the port names which are not NULL
    return out;
}

void ComPortManager::setModeIdle()
{/*From the start, our code will automatically start in Idle state,
    However, to see the existing ports, we do a reload operation*/
    m_mode = Mode::Idle;
    reloadPorts();
}

void ComPortManager::setModeSingle(const QString &portName)
{/* This function allowed us to set our reading to single mode. Thus, it set the chosen 
portName to generate its unique thread. After that, we reload our ports.*/
    m_mode = Mode::SinglePort;
    m_selectedPort = portName;
    reloadPorts(); 
}

void ComPortManager::setModeSimulation()
{/* This function allowed us to set our readings as simulation ones, which is a test condition
with randomly generated values. After that, we reload our ports. */
    m_mode = Mode::SimulationOnly;
    reloadPorts();
}

void ComPortManager::reloadPorts()
{//renew the ports, and delete the existing ones after that, call it again.
    clearAll();
    startAll();
}

void ComPortManager::stopAll()
{
    /*Normally direct existence of clearAll() would be enough. However, in dvClient, we need to use clearAll on some areas, 
    importantly, after the dvClient has been updated. Thus, rather than calling "~", we stop all redo similar processes.
    Basically, for code clarity
    */
    
    /*
        DvClient::~DvClient()
        {
            m_portManager->stopAll();
            delete m_portManager;
        }
    */
    clearAll();
}

void ComPortManager::startAll()
{/* Here is a function that handles the condition of our state, and according to that, it will start the weather for the 
chosen COM or Simulation. Thus, we can also choose to stay in the Idle position. If we needed. Likewise, when the code 
starts to run, initially, it will stay in Idle.
*/
    m_openCount = 0;

    if (m_mode == Mode::Idle)
    {/* It is the Idle state in which we update the "setCOMSentinel()" value to be zero from the client. */
        m_client->setCOMSentinel(0);
        return;
    }

    if (m_mode == Mode::SimulationOnly)
    {/* It is the Simulation state in which we generate our random values to test our other structure parameters.
        For future implementation on device side, we plan to add local sensor that reads the values on device. Thus, 
        the operator will test with his hand or something like that. The reason for doing that is that,
        depending on the device, we may get some zero readings, but there are some issues with the fan blades. It  
        allowed the  operator to be sure.*/
        m_client->setCOMSentinel(1);
        emit allPortsClosed();
        return;
    }

    if (m_mode == Mode::SinglePort)
    {/* Here we are ensuring that the single port is working, not multiple COMs at the same time.
        After we can start to make our single port readings */
        if (m_selectedPort.isEmpty())
        {/* Whether the selected port is empty or not. If it is empty, then we can emit it that
            allportCloses and set the COM sentinel as one. */
            emit allPortsClosed();
            m_client->setCOMSentinel(1);
            return;
        }

        ComThread *thread = new ComThread(m_selectedPort, this); /* Allocating a new thread for our new COM. */ 
        const QString portName = m_selectedPort; //capturing the portname
        thread->setBaudRate(QSerialPort::Baud115200); //Setting our baud rate, which we are gonna read from our ESP32
        
        /*######### Thread Connections - Start #########*/
        /* We are making the requirements connections to proceed with our COM readings. Which we can see portOpen and portFailed
        distanceReceived and the parseError. and thread finish conditions.*/
        connect(thread, &ComThread::portOpened, this, &ComPortManager::onPortOpened); //Making the requirment connections
        connect(thread, &ComThread::distanceReceived, this, &ComPortManager::onDistance);
        connect(thread, &ComThread::parseError, this, [portName](const QString &line){
            qWarning() << "error on" << portName << ":" << line;
        });
        connect(thread, &ComThread::portOpenFailed, this, &ComPortManager::onPortOpenFailed);
        connect(thread, &QThread::finished, this, &ComPortManager::onThreadFinished);
        /*######### Thread Connections - End #########*/
        
        thread->start(); // after setting the connections, we can start the thread.
        m_threads.append(thread);
        
        if (m_openCount == 0)
        {// will flip to 0->1 on portOpened, setting as open for the current port
            m_client->setCOMSentinel(1);
        }
        return;
    }

    if (m_openCount == 0) {
        emit allPortsClosed();
        /*which means if the user re-selects "select port" on COMs, close all the existing COMs
        and their corresponding thread*/
        m_client->setCOMSentinel(1);
    }
}

void ComPortManager::clearAll()
{/* Before clearing the threads, we need to stop the thread and put it in wait mode.*/
    for (ComThread *thread : m_threads) {
        qDebug() << "Stopping: " << thread->objectName();
        thread->stop();//   m_running = false;
        thread->wait();//   QThread in-build function
        qDebug() << "Stopped: " << thread->objectName();
        delete thread;// Threadi şimdi siliyoruyz,
    }
    m_threads.clear(); //QVector<ComThread*> m_threads; olarak tanımsadım ve her bir vector listedir,direk clear atabilirim free yapmam gerekemez
}

void ComPortManager::onPortOpened(const QString &portName)
{/* After calling the onPortOpened, we increment the m_openCount, and 
    set "m_client->setCOMSentinel()" to 0. After that, it will say it 
    On the system chat, the port is opened and a name is assigned to it. */
    ++m_openCount;
    m_client->setCOMSentinel(0);
    emit anyPortOpened();
    qDebug() << "Port opened:" << portName;
}

void ComPortManager::onDistance(float distance)
{/* Getting read distance and updating the distance value from the client side */
    m_client->updateDistance(distance);
}

void ComPortManager::onPortOpenFailed(const QString &err)
{/* Right now, we are not using it*/
    qWarning() << "Failed to open port:" << err;
}

void ComPortManager::onThreadFinished()
{/* When the thread value finishes, does the count of the threads drop below 0
    ergo, "m_openCount".  setComSentinel has become 0, and all ports are closed.*/ 
    if (m_openCount > 0) --m_openCount; else m_openCount = 0; 
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

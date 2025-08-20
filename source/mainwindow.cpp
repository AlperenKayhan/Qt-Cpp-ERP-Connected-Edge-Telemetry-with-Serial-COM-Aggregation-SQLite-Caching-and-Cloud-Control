#include "mainwindow.h"
#include "dvclient.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QSqlQuery>
#include <QDebug>
#include <QTextCursor>
#include <QTimer>


MainWindow* MainWindow::s_instance = nullptr;

MainWindow::MainWindow(DvClient *client, QWidget *parent)
    : QMainWindow(parent)
    , client(client)
{ /* Here is where we make our main window, which consists of port selection buttons, event output, and 3D OpenGL graph   */
    s_instance = this; // To show the current instance 
    qInstallMessageHandler(messageHandler); // Installing the message handler to show the messages on our message output box.
    //Initializing the current boxes on their.
    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);

    // Top row: COM selector
    QHBoxLayout *top = new QHBoxLayout(); // Starting a new box
    portCombo = new QComboBox(this); // new Comboxes
    portCombo->addItem("Select Port");     // idx 0 for idle state pre-build 
    portCombo->addItem("Simulation");      // idx 1 for using the simulated values, it is pre build as well.
    const QStringList ports = client->serialPorts(); // Showing the read values currently on the device, which is read from the client.
    for (const QString &p : ports) portCombo->addItem(p); /// idx 3 for reading from the COM state, which is a state that shows all the ports that we need to be reading 
    connect(portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onPortChoiceChanged);
    top->addWidget(portCombo); // adding the ports that readed
    layout->addLayout(top);

    /*######## Table & Model ########*/
    /* This place where we implement our buttons that will be created, we generate a requirement area for using them. */
    tableView = new QTableView(this); 
    model = new QSqlTableModel(this, client->database());
    model->setTable("warnings"); model->select();
    tableView->setModel(model);
    tableView->horizontalHeader()->setStretchLastSection(true);
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(tableView);

    /*######## Buttons ########*/
    /* This is where we allocate our buttons that gonna be used before linking the buttons,
    We need to create them, and give them a seen on screen presence (visual), and the ones 
    that are seen code level*/
    sendLogsButton  = new QPushButton("Send Logs", this); // Allocating the button for Send Logs
    startSensorButton = new QPushButton("Start Sensor Reading", this); // Allocating the button for Start Sensor Reading
    stopSensorButton  = new QPushButton("Stop Sensor Reading", this); // Allocating the button for Stop Sensor Reading
    resetButton       = new QPushButton("Reset Database", this); // Allocating the button for Reset Database
    parametersButton  = new QPushButton("Get Parameters", this); // Allocating the button for Get Parameters
    rebootButton      = new QPushButton("Reboot", this); // Allocating the button for Reboot

    QGridLayout *btnGrid = new QGridLayout(); // Creating the button layout
    btnGrid->addWidget(sendLogsButton,    0,0); //Specifing the unique location
    btnGrid->addWidget(startSensorButton, 0,1); //Specifing the unique location
    btnGrid->addWidget(stopSensorButton,  1,0); //Specifing the unique location
    btnGrid->addWidget(resetButton,       1,1); //Specifing the unique location
    btnGrid->addWidget(parametersButton,  2,0); //Specifing the unique location
    btnGrid->addWidget(rebootButton,      2,1); //Specifing the unique location
    layout->addLayout(btnGrid);

    /*######## 3D Scatter ########*/
    /* It is where we make the requirement calls from to show our 3D OpenGL*/
    scatterWidget = new Scatter3DWidget(this);
    scatterWidget->setMinimumSize(300,300);
    layout->addWidget(scatterWidget);

    /*######## Logs ########*/
    /* Area that prints our logs on screen like COM reads, URL parameters errors, etc.*/ 
    logOutput = new QPlainTextEdit(this);
    logOutput->setReadOnly(true);
    logOutput->setMinimumSize(400,100);
    layout->addWidget(logOutput);
    setCentralWidget(central);

    /*######## Populate Previous records ########*/
    /* While we are showing the generated records on the screen, we also need to show
    The previous values have been generated as well to ensure that we need to repopulate
    the table with previous values.*/
    QSqlQuery q(client->database());
    q.exec("SELECT timestamp, level, distance, xn FROM warnings");
    while(q.next()){
        QString ts  = q.value(0).toString();
        QString lvl = q.value(1).toString();
        double d    = q.value(2).toDouble();
        double x    = q.value(3).toDouble();
        double v    = LevelDetect(lvl);
        scatterWidget->addPoint(d,x,v);
        appendLog(QString("History: %1, %2, %3").arg(ts).arg(lvl).arg(d));
    }

    /*######## Button Connect actions ########*/
    /*Connecting the generated buttons with their corresponding functions and,
    also describing how input will be getting from the screen*/
    connect(client, &DvClient::newWarning, this, &MainWindow::onNewWarning);
    connect(sendLogsButton,  &QPushButton::clicked, this, &MainWindow::onSendLogs);
    connect(startSensorButton,&QPushButton::clicked, this, &MainWindow::onStartSensor);
    connect(stopSensorButton, &QPushButton::clicked, this, &MainWindow::onStopSensor);
    connect(resetButton,      &QPushButton::clicked, this, &MainWindow::onResetDatabase);
    connect(parametersButton, &QPushButton::clicked, this, &MainWindow::onGetParameters);
    connect(rebootButton,     &QPushButton::clicked, this, &MainWindow::onReboot);

    /* Here we disable the buttons until there is a COM selection. 
    After choosing the COMs, the buttons will be unlocked. Expect the 
    reboot button because we still need to be able to reboot our COMs, 
    In case there is an update on COMs*/
    startSensorButton->setEnabled(false);
    stopSensorButton->setEnabled(false);
    resetButton->setEnabled(false);
    parametersButton->setEnabled(false);
    sendLogsButton->setEnabled(false);
}

MainWindow::~MainWindow(){/*DESTRUCTOR, it is a typical destructor, and also
individually deleting qInstallMessageHandler */
    qInstallMessageHandler(nullptr); s_instance=nullptr; 
}

double MainWindow::LevelDetect(const QString &level)
{ /*Detecting level that got on SQL Database.*/
    if(level=="WARNING-1") return 1;
    if(level=="WARNING-2") return 2;
    if(level=="WARNING-3") return 3;
    if(level=="WARNING-4") return 4;
    return 0;
}

void MainWindow::onNewWarning(const QString &, const QString &level, double distance, double xn)
{/*Message that pushed on the screen to see each sensor reading and its corresponding readings. */
    model->select(); tableView->scrollToBottom();
    double v=LevelDetect(level);
    scatterWidget->addPoint(distance,xn,v);
    appendLog(QString("-> New warning: %1, distance=%2, xn=%3").arg(level).arg(distance).arg(xn));
}

void MainWindow::appendLog(const QString &msg)
{//typing msg that was sent from the ERP system.
    logOutput->appendPlainText(msg);
    QTextCursor c=logOutput->textCursor(); logOutput->setTextCursor(c);
}
void MainWindow::onSendLogs()
{/* Which is a button's function that calls uploadLogFile from the client */
    client->uploadLogFile();
}
void MainWindow::onStartSensor()
{/* which is a condition that changes "setErrorSimulation" to START the sensor reading. */
    client->setErrorSimulation(true);
    appendLog("# Sensor reading started.");
}
void MainWindow::onStopSensor()
{/* which is a condition that changes "setErrorSimulation" to STOP the sensor reading. */
    client->setErrorSimulation(false);
    appendLog("# Sensor reading stopped.");
}
void MainWindow::onResetDatabase()
{/* button condition that resets the database and clears all the points. While resetting, 
    It will also close the error simulation. */
    scatterWidget->clearPoints();
    client->resetDatabase();
    client->setErrorSimulation(false);
    model->select();
    tableView->scrollToTop();
    appendLog("# Database reset and simulation stopped.");
}
void MainWindow::onGetParameters(){
    client->requestParameters();
}
void MainWindow::onReboot()
{
    appendLog("# Rebooting COM ports...");
    client->rebootComPorts();
    QTimer::singleShot(400, this, &MainWindow::refreshPortList);
}


void MainWindow::onPortChoiceChanged(int idx)
{// Re handle buttons only when a valid mode is chosen
    const bool enable = (idx >= 1);
    startSensorButton->setEnabled(enable);
    stopSensorButton->setEnabled(enable);
    resetButton->setEnabled(enable);
    parametersButton->setEnabled(enable);
    sendLogsButton->setEnabled(enable);
    rebootButton->setEnabled(enable);

    if (idx == 0) { // Idle
        client->comUseIdle();
        appendLog("# Mode: Select a port / idle");
    }
    else if (idx == 1) { // Simulation
        client->comUseSimulationOnly();
        appendLog("# Mode: Simulation only");
    }
    else if (idx >= 2) { // Specific port
        const QString portName = portCombo->itemText(idx);
        client->comUseSinglePort(portName);
        appendLog(QString("# Mode: Single port -> %1").arg(portName));
    }

}

void MainWindow::messageHandler(QtMsgType t,const QMessageLogContext&,const QString &m){
    QString p;
    switch(t){ case QtDebugMsg: p="DEBUG: ";break; case QtWarningMsg: p="WARNING: ";break; case QtCriticalMsg: p="CRITICAL: ";break; case QtFatalMsg: p="FATAL: ";break; default: p.clear(); }
    if(s_instance) s_instance->appendLog(p+m);
}

void MainWindow::refreshPortList()
{
    const QString prev = portCombo->currentText();

    const QStringList base = { "Select Port", "Simulation"};
    const QStringList ports = client->serialPorts();

    portCombo->blockSignals(true);
    portCombo->clear();
    for (const QString &b : base) portCombo->addItem(b);
    for (const QString &p : ports) portCombo->addItem(p);

    int idx = portCombo->findText(prev);
    portCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    portCombo->blockSignals(false);

    // Re-apply mode for the restored selection
    onPortChoiceChanged(portCombo->currentIndex());
}



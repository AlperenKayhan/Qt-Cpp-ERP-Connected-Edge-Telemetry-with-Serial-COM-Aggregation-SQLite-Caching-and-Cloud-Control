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
{
    s_instance = this;
    qInstallMessageHandler(messageHandler);

    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);

    // Top row: COM selector
    QHBoxLayout *top = new QHBoxLayout();
    portCombo = new QComboBox(this);
    portCombo->addItem("Select Port");     // idx 0 -> idle (no read/no sim)
    portCombo->addItem("Simulation");      // idx 1
    const QStringList ports = client->serialPorts();
    for (const QString &p : ports) portCombo->addItem(p); // idx >= 3
    connect(portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onPortChoiceChanged);
    top->addWidget(portCombo);
    layout->addLayout(top);

    // Table & model
    tableView = new QTableView(this);
    model = new QSqlTableModel(this, client->database());
    model->setTable("warnings"); model->select();
    tableView->setModel(model);
    tableView->horizontalHeader()->setStretchLastSection(true);
    tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(tableView);

    // Buttons
    sendLogsButton  = new QPushButton("Send Logs", this);
    startSensorButton = new QPushButton("Start Sensor Reading", this);
    stopSensorButton  = new QPushButton("Stop Sensor Reading", this);
    resetButton       = new QPushButton("Reset Database", this);
    parametersButton  = new QPushButton("Get Parameters", this);
    rebootButton      = new QPushButton("Reboot", this);

    QGridLayout *btnGrid = new QGridLayout();
    btnGrid->addWidget(sendLogsButton,    0,0);
    btnGrid->addWidget(startSensorButton, 0,1);
    btnGrid->addWidget(stopSensorButton,  1,0);
    btnGrid->addWidget(resetButton,       1,1);
    btnGrid->addWidget(parametersButton,  2,0);
    btnGrid->addWidget(rebootButton,      2,1);
    layout->addLayout(btnGrid);

    // 3D Scatter
    scatterWidget = new Scatter3DWidget(this);
    scatterWidget->setMinimumSize(300,300);
    layout->addWidget(scatterWidget);

    // Logs
    logOutput = new QPlainTextEdit(this);
    logOutput->setReadOnly(true);
    logOutput->setMinimumSize(400,100);
    layout->addWidget(logOutput);

    setCentralWidget(central);

    // Populate previous records
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

    // Connect actions
    connect(client, &DvClient::newWarning, this, &MainWindow::onNewWarning);
    connect(sendLogsButton,  &QPushButton::clicked, this, &MainWindow::onSendLogs);
    connect(startSensorButton,&QPushButton::clicked, this, &MainWindow::onStartSensor);
    connect(stopSensorButton, &QPushButton::clicked, this, &MainWindow::onStopSensor);
    connect(resetButton,      &QPushButton::clicked, this, &MainWindow::onResetDatabase);
    connect(parametersButton, &QPushButton::clicked, this, &MainWindow::onGetParameters);
    connect(rebootButton,     &QPushButton::clicked, this, &MainWindow::onReboot);

    // Disable buttons until a mode is chosen
    startSensorButton->setEnabled(false);
    stopSensorButton->setEnabled(false);
    resetButton->setEnabled(false);
    parametersButton->setEnabled(false);
    sendLogsButton->setEnabled(false);
    //rebootButton->setEnabled(false);
}

MainWindow::~MainWindow(){ qInstallMessageHandler(nullptr); s_instance=nullptr; }

double MainWindow::LevelDetect(const QString &level){
    if(level=="WARNING-1") return 1;
    if(level=="WARNING-2") return 2;
    if(level=="WARNING-3") return 3;
    if(level=="WARNING-4") return 4;
    return 0;
}

void MainWindow::onNewWarning(const QString &, const QString &level, double distance, double xn){
    model->select(); tableView->scrollToBottom();
    double v=LevelDetect(level);
    scatterWidget->addPoint(distance,xn,v);
    appendLog(QString("-> New warning: %1, distance=%2, xn=%3").arg(level).arg(distance).arg(xn));
}

void MainWindow::appendLog(const QString &msg){
    logOutput->appendPlainText(msg);
    QTextCursor c=logOutput->textCursor(); logOutput->setTextCursor(c);
}
void MainWindow::onSendLogs(){
    client->uploadLogFile();
}
void MainWindow::onStartSensor(){
    client->setErrorSimulation(true);
    appendLog("# Sensor reading started.");
}
void MainWindow::onStopSensor(){
    client->setErrorSimulation(false);
    appendLog("# Sensor reading stopped.");
}
void MainWindow::onResetDatabase(){
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



#include "dvclient.h"
#include "comportmanager.h"
#include <QCoreApplication>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QFile>
#include <QDebug>
#include <QNetworkInterface>
#include <QUrl>
#include <QUrlQuery>
#include <cmath>

DvClient::DvClient(QObject *parent)
    : QObject(parent)
    , m_portManager(new ComPortManager(this, this))
{/* DvClient main, which handles the websocket connections between the ERP system and the Project. */
    loadSession();
    connect(&http, &QNetworkAccessManager::finished, this, &DvClient::onHttpFinished);
    connect(&socket, &QWebSocket::textMessageReceived, this, &DvClient::onSocketTextMessageReceived);
    connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, &DvClient::onSocketError);
    connect(&pingTimer, &QTimer::timeout, this, &DvClient::onPingTimeout);
}

DvClient::~DvClient()
{/*DESTRUCTOR: When we are done with using the Program, it will stop ping (heartbeat) and close the socket
As well as; delete the port manager */
    pingTimer.stop();
    socket.close(); // ensures no pending textMessageReceived later
    if (m_portManager) m_portManager->stopAll();
    delete m_portManager;
}

QStringList DvClient::serialPorts() const
{/* This function handles whether we have a valid ComPortManager instance or not.
By asking whether the port is available or not. if that is not the case; function will return empty string */
    // If we have a valid ComPortManager instance, ask it for available ports
    if (m_portManager) {
        return m_portManager->availablePorts();
    }
    else {//empty string handle
        return QStringList{};
    }
}


bool DvClient::initDatabase()
{/* Here we are initializing the SQL database for warnings, to make local storage of our values. */
    db = QSqlDatabase::addDatabase("QSQLITE"); // "QSQLite" is the Qt version of SQLite
    db.setDatabaseName(QCoreApplication::applicationDirPath() + "/warnings.db");//Setting DB name
    if (!db.open()) {// if not open handle
        qWarning() << "Cannot open SQLite:" << db.lastError().text();
        return false;
    }
    QSqlQuery q;
    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS warnings (
          id        INTEGER PRIMARY KEY AUTOINCREMENT,
          timestamp TEXT    NOT NULL,
          level     TEXT    NOT NULL,
          distance  REAL    NOT NULL,
          xn        REAL    NOT NULL
        )
    )")) {/* If the table does not exist, it will generate one according  to the given format*/
        qWarning() << "Failed to create table:" << q.lastError().text();
        return false;
    }
    qDebug() << "SQLite initialized at" << db.databaseName();
    return true;
}

void DvClient::start()
{/* It is a function that starts the device. It will first generate the URL that we need using the "buildDvOpURL" function.
With the generated URL, we can send our open Request to the ERP system and fetch our session.*/
    QString url = buildDvOpUrl(sessionId);
    qDebug() << "Fetching session via:" << url;
    http.get(QNetworkRequest(QUrl(url)));
}

void DvClient::onHttpFinished(QNetworkReply *reply)
{
    const QScopedPointer<QNetworkReply, QScopedPointerDeleteLater> guard(reply); // auto deleteLater()
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "HTTP error:" << reply->errorString();
        return;
    }
    QByteArray body = reply->readAll();
    auto doc = QJsonDocument::fromJson(body).object();
    if (doc["status"].toString() != "succes") {
        qWarning() << "Bad status:" << body;
        return;
    }
    auto data = doc["data"].toObject();
    sessionId  = data["S"].toString();
    corpsID    = data["corps_id"].toString();
    locationID = data["corps_locations_id"].toString();
    devicesID  = data["devices_id"].toString();
    if (!haveSavedSession) { saveSession(); haveSavedSession = true; }
    QNetworkRequest req(QUrl("wss://dev-kodx.mepsan.com.tr/s.io/?EIO=4&transport=websocket"));
    req.setRawHeader("Cookie", QByteArray("S=") + sessionId.toUtf8());
    socket.open(req);
}

void DvClient::onSocketTextMessageReceived(const QString &msg)
{/* It is where we process the message that we received from the ERP system. The message itself is 
in the raw data format. Thus, this function also transforms that data into our message.*/
    // SocketIO handshake protocol
    if (msg.startsWith('0')) { socket.sendTextMessage("40"); return; } 
    if (msg == "2")        { socket.sendTextMessage("3");  return; }
    if (msg == "3")        { return; }

    if (msg.startsWith("40") && !registered) {// Registration processes step
        QJsonArray reg{ "r", QJsonObject{{"n", sessionId}, {"r","dev"}} };
        socket.sendTextMessage("42" + QJsonDocument(reg).toJson(QJsonDocument::Compact));
        registered = true;
        pingTimer.start(5000); // Condition that make our registration allive
        return;
    }

    if (msg.startsWith("42")) 
    {/* This part handles the true event of the message, which initially processes the message that we received 
        from ERP. Then, we need to convert it into a QbyteArray, which becomes our raw reading to be processed.
        then we pulled our ev data, which message string to be seen. It can be "pong" or "m*(message) or 
        various other depending on what ERP sends*/
        QByteArray raw = msg.mid(2).toUtf8();
        auto arr = QJsonDocument::fromJson(raw).array();
        QString ev = arr.at(0).toString();

        if (ev == "pong") {
            if (ErrorSimulationSentinelVal) 
            {/*This heartbeat implementation is very valuable to the ERP system to keep the device open
                because, if there is no response after a certain amount of time ERP system will shut down the device.
                So, every 5 seconds, ERP sends a tick to the device device sends a ping*/
                double dist; // will be the distance that readed from the COMs
                if (comSentinel)
                {/* If comSentinel is set, use a random simulated distance between 10 and 200
                WHY we have this, it is a test condition that other elements are working or not*/
                    dist = QRandomGenerator::global()->generateDouble() * 190.0 + 10.0;
                } 
                else {
                    // Otherwise, use the actual currentDistance value from the COM port
                    dist = this->currentDistance;
                }
                
                // Xn processing
                double t  = 7.0 * (dist * 10.0) + 3.0;
                double xn = std::fmod(t, 4.0);
                if (xn < 0.0) xn += 4.0;
                
                QString lvl;/* Depending on the value of the Xn, we determine our warning level, 
                whether it is in the range or not. Warning values are important because, depending on their value,
                which level they will populate their corresponding locations on the 3D Graph*/
                if (xn <= 1.5) lvl = "WARNING-1"; 
                else if (xn <= 2.1) lvl = "WARNING-2";
                else if (xn <= 3.1) lvl = "WARNING-3";
                else lvl = "WARNING-4";
                
                QSqlQuery ins;/* This SQL condition handles to sets our calculated and read values to store in our local Database.
                The reason why we are doing it dynamically is to protect the data, if there is be connection error with ERP*/
                
                ins.prepare(R"(INSERT INTO warnings (timestamp, level, distance, xn) VALUES (:ts, :lvl, :d, :xn))");
                QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + "Z";
                ins.bindValue(":ts",  now);
                ins.bindValue(":lvl", lvl);
                ins.bindValue(":d",   dist);
                ins.bindValue(":xn",  xn);
                if (!ins.exec()) {
                    qWarning() << "DB insert failed:" << ins.lastError().text();
                } else {
                    emit newWarning(now, lvl, dist, xn);
                }
            }
        }
        else if (ev == "m")
        {/* This is where we process our "m" message value. If the ERP system sends a message, 
        then we need to process that message value further. For us to get a specific "cmd" 
        command value to precede the commands on the device*/
            QJsonObject obj = arr.at(1).toObject();
            QString tStr = obj.value("t").toString();
            QJsonDocument innerDoc = QJsonDocument::fromJson(tStr.toUtf8());
            QJsonObject inner = innerDoc.object();
            QString cmd = inner.value("f").toString();

            if (cmd == "send_logs") 
            {/* It is a command that calls the uploadding recorded sql file to the ERP system */
                qInfo() << "==> LOGs will be uploading:";
                uploadLogFile();
            }
            else if (cmd == "get_d_parameters")
            {/*Function that lists the device parameters */
                requestParameters();
            }
            else if (cmd == "reboot") 
            {/*Command that reboot the device which, also stops the sensor reading and resets the Local DataBase */
                qInfo() << "==> Reboot Received";
                resetDatabase();
                qInfo() << "  DB Reseted";
                ErrorSimulationSentinelVal = 0;
                QCoreApplication::exit(0);
            }
            else if (cmd == "send_msg_log")
            {/*Shows the message that sent by the ERP system. */
                qInfo() << "==> MSG:" << inner.value("msg").toString();
            }
            else if (cmd == "changed_parameters")
            {/* Allowed to start sensor readimng remotly from ERP system */
                qInfo() << "\n\nWARNING: System UNSTABLE";
                ErrorSimulationSentinelVal = 1;
            }
            else if (cmd == "ping")
            {//Sending a legit ping from ERP that gave an response
                onPingTimeout();
            }
            else if (cmd == "refresh")
            {/* Have similar Usage with reboot, on future updates it will get more abilities.
                it will allowed us to reboot the COM ports on the ERP system.*/
                //resetDatabase();
                ErrorSimulationSentinelVal = 0;
                rebootComPorts();
                //setErroSimulation_LOW();
            }
            else {//Unknown command handler, if there will be an Unknown command recieved from ERP system.
                qWarning() << "Unknown Command:" << cmd;
            }
        }
    }
}

void DvClient::resetDatabase()
{/* Function that cleans the values in the local SQLite database*/
    if (db.isOpen()) db.close();
    QString path = QCoreApplication::applicationDirPath() + "/warnings.db";
    if (QFile::exists(path) && !QFile::remove(path))
        qWarning() << "Failed to remove DB file:" << path;
    initDatabase();
}

void DvClient::setCOMSentinel(int value)
{// Setting the new COM Sentinel
    comSentinel = value;
    qDebug() << "COMsentinel set to:" << value;
}

void DvClient::updateDistance(float distance)
{/* Update the distance readed by the HC-SR04 sensor*/
    currentDistance = distance;
}

void DvClient::requestParameters()
{/*Showing the device parameters that listed like seassionID, CorpsID, LocationID,
IP and MAC. Thus, MAC and IP getted by using "getNetworkInfo()" function then 
spearete the pairs as IP and MAC as first and second pair*/
    QPair<QString, QString> pair = getNetworkInfo();
    QString ip  = pair.first;
    QString mac = pair.second;
    qInfo() << "==> Parameters:";
    qInfo() << "   Session ID :" << sessionId;
    qInfo() << "   Corps ID   :" << corpsID;
    qInfo() << "   Location ID:" << locationID;
    qInfo() << "   IP          :" << ip;
    qInfo() << "   MAC         :" << mac;
}

void DvClient::onSocketError(QAbstractSocket::SocketError)
{//Socket ERror handler
    qWarning() << "WS error:" << socket.errorString();
}

void DvClient::onPingTimeout()
{//Legit Ping that got from ERP system.
    socket.sendTextMessage("42[\"ping\",{}]");
}

QString DvClient::buildDvOpUrl(const QString &session)
{/* to send an open request to the database, we need to first build our URL with
various parameters. We recieved the seasssionID from inside from text file which, is 
recorded on locally inside the device. The reason that we use the same seassionID, it 
will overloads the ERP system with too many seassionIDs, if that is the case then our ERp system will kill the seasssionIDs 
automatically.Some names may be inconsistent due to not sharing company methods in detail.*/
    QUrl u("https://devSampllle.mepsan.com.tr/deicev/DevicevOpen"); // -> Sample Names
    QUrlQuery q;
    q.addQueryItem("pts", QString::number(QDateTime::currentMSecsSinceEpoch()));
    q.addQueryItem("S[S]", session); // seassion İd that we store on device
    q.addQueryItem("S[ptof]", "180"); // essential local values
    q.addQueryItem("S[country]", "225"); // essential local values
    q.addQueryItem("S[lang]", "tr"); // essential local values
    q.addQueryItem("S[serial_no]", "251306200097"); // Device serial ID that needs to be recorded on ERP to recognize by it.
    q.addQueryItem("S[serial_no_hw]", "724564889999");
    q.addQueryItem("d_short_code", "kodxmcu_avenda_lindo_01"); //Device name -> important for ERP to recognize
    q.addQueryItem("d_firmware", "kodxmcu_avenda_lindo_01"); //Device name -> important for ERP to recognize
    q.addQueryItem("d_mac_id", "00:30:18:03:26:88"); //Unique Device MAC Address, which is a specified ip on this context
    q.addQueryItem("d_local_ip", "192.168.5.172"); //Unique Device Local IP Address, which is a specified ip on this context
    q.addQueryItem("d_oper", "Prod");
    q.addQueryItem("d_mdl_id", "9100200");
    q.addQueryItem("d_sites_id", "9100200");
    u.setQuery(q);
    return u.toString(QUrl::FullyEncoded); // returning the setted URL
}

void DvClient::uploadLogFile()
{/* Function that allowed us to upload our local database values on to the ERP system with converting the SQL reading as JSON format
in order for the ERP system to undetrstand. */
    QSqlQuery query("SELECT timestamp, level, distance, xn FROM warnings");
    QJsonArray logs;
    while (query.next()) {
        QJsonObject e;
        e["timestamp"] = query.value(0).toString();
        e["level"]     = query.value(1).toString();
        e["distance"]  = query.value(2).toDouble();
        e["Xn_val"]    = query.value(3).toDouble();
        logs.append(e);
    }

    QString tempFile = QCoreApplication::applicationDirPath() + "/logs_temp.json";
    QFile f(tempFile);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot open temp log file";
        return;
    }
    f.write(QJsonDocument(logs).toJson(QJsonDocument::Compact));
    f.close();

    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   "form-data; name=\"file\"; filename=\"logs_temp.json\"");
    auto *filePart = new QFile(tempFile);
    filePart->open(QIODevice::ReadOnly);
    part.setBodyDevice(filePart);
    filePart->setParent(multi);
    multi->append(part);

    QNetworkRequest req(QUrl("https://devSampllle.mepsan.com.tr/dl/DeviceLogUpload"));// -> Sample Name
    req.setRawHeader("Cookie", QByteArray("S=") + sessionId.toUtf8());
    req.setRawHeader("sys_objects_name", "alperen_test"); //raw header name given as that way to recongnize it is a test device.
    req.setRawHeader("p_devices_id", devicesID.toUtf8());

    auto *reply = http.post(req, multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, [reply, filePart, tempFile]() {
        if (reply->error() != QNetworkReply::NoError)
            qWarning() << "Upload failed:" << reply->errorString();
        else
            qDebug() << "-> Upload Successful";
        filePart->deleteLater();
        QFile::remove(tempFile);
        reply->deleteLater();
    });
}

QPair<QString, QString> DvClient::getNetworkInfo()
{/* Function that provide our network Info as two paired string. 
    -Which after the reading*/
    for (auto iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (auto entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                return { entry.ip().toString(), iface.hardwareAddress() };
        }
    }
    return { QString(), QString() };
}

void DvClient::loadSession()
{/* This function allowed us to pull the our pre recorded seassionID*/
    QString file = QCoreApplication::applicationDirPath() + "/sessionID.txt";
    QFile f(file);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        sessionId = QString::fromUtf8(f.readAll()).trimmed();
        haveSavedSession = true;
    }
}

void DvClient::saveSession()
{/* it is a function that allowed us to load our seassionID on seassionID text file.
    NOTE that, when this code first run in a new device it will generate the seassionID once then later, 
    Upload that generated file to the text file. It will done once and then we used later and later again.
    As long as our .txt file exists. If the file does not exist also it will generate the file as fail save.
    */ 
    QString file = QCoreApplication::applicationDirPath() + "/sessionID.txt";
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(sessionId.toUtf8());
    }
}

void DvClient::setErrorSimulation(bool enable)
{/*A simple if else condition that wheather our error sentinel value unlocked or not. */
    ErrorSimulationSentinelVal = enable ? 1 : 0;
}

void DvClient::setErroSimulation_LOW()
{/* Closing the Error simulation, as I described that the use will be increased in the future updates*/
    ErrorSimulationSentinelVal = 0;
}

void DvClient::rebootComPorts()
{/* This reboot condition that reboot our COM ports if there is a another device has connected or not.
    Which, it is a need due to operator test multiple device.*/
    if (m_portManager)
        m_portManager->reloadPorts();
}

void DvClient::comUseIdle() { if (m_portManager) m_portManager->setModeIdle(); }// Setting the code on Idle which, also start of the code as well on Idle state
void DvClient::comUseSimulationOnly() { if (m_portManager) m_portManager->setModeSimulation(); } // Use the simulation value.

void DvClient::comUseSinglePort(const QString &p)
{/*Setting for single Port usage send with the setted version we can start to read to port and generate our threads according to that. */
    if (m_portManager)
        m_portManager->setModeSingle(p);
}

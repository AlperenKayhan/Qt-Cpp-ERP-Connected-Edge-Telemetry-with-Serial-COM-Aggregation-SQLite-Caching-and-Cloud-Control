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
{/* DvClient main which handles the websocket connections betweeen ERP system and the Project. */
    loadSession();
    connect(&http, &QNetworkAccessManager::finished, this, &DvClient::onHttpFinished);
    connect(&socket, &QWebSocket::textMessageReceived, this, &DvClient::onSocketTextMessageReceived);
    connect(&socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this, &DvClient::onSocketError);
    connect(&pingTimer, &QTimer::timeout, this, &DvClient::onPingTimeout);
}

DvClient::~DvClient()
{/*DESTRUCTOR: When we are done with using the Program it will stop ping (heartbeat) and close the socket
As well as; delete the port manager */
    pingTimer.stop();
    socket.close(); // ensures no pending textMessageReceived later
    if (m_portManager) m_portManager->stopAll();
    delete m_portManager;
}

QStringList DvClient::serialPorts() const
{/* This function handles Wheather we have valid valid ComPortManager instance or not.
With asking the port available or not. if that is not the case; function will return empty string */
    // If we have a valid ComPortManager instance, ask it for available ports
    if (m_portManager) {
        return m_portManager->availablePorts();
    }
    else {//empty string handle
        return QStringList{};
    }
}


bool DvClient::initDatabase()
{/* Here we are initilaizing the SQL data base for warnings; to make local storage of our values. */
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
    )")) {
        qWarning() << "Failed to create table:" << q.lastError().text();
        return false;
    }
    qDebug() << "SQLite initialized at" << db.databaseName();
    return true;
}

void DvClient::start()
{
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
{
    if (msg.startsWith('0')) { socket.sendTextMessage("40"); return; }
    if (msg == "2")        { socket.sendTextMessage("3");  return; }
    if (msg == "3")        { return; }

    if (msg.startsWith("40") && !registered) {
        QJsonArray reg{ "r", QJsonObject{{"n", sessionId}, {"r","dev"}} };
        socket.sendTextMessage("42" + QJsonDocument(reg).toJson(QJsonDocument::Compact));
        registered = true;
        pingTimer.start(5000);
        return;
    }

    if (msg.startsWith("42")) {
        QByteArray raw = msg.mid(2).toUtf8();
        auto arr = QJsonDocument::fromJson(raw).array();
        QString ev = arr.at(0).toString();

        if (ev == "pong") {
            if (ErrorSimulationSentinelVal) {
                double dist = (comSentinel ? QRandomGenerator::global()->generateDouble() * 190.0 + 10.0
                                           : this->currentDistance);
                double t  = 7.0 * (dist * 10.0) + 3.0;
                double xn = std::fmod(t, 4.0);
                if (xn < 0.0) xn += 4.0;
                QString lvl;
                if      (xn <= 1.5) lvl = "WARNING-1";
                else if (xn <= 2.1) lvl = "WARNING-2";
                else if (xn <= 3.1) lvl = "WARNING-3";
                else                lvl = "WARNING-4";
                QSqlQuery ins;
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
        else if (ev == "m") {
            QJsonObject obj = arr.at(1).toObject();
            QString tStr = obj.value("t").toString();
            QJsonDocument innerDoc = QJsonDocument::fromJson(tStr.toUtf8());
            QJsonObject inner = innerDoc.object();
            QString cmd = inner.value("f").toString();

            if (cmd == "send_logs") {
                qInfo() << "==> LOGs will be uploading:";
                uploadLogFile();
            }
            else if (cmd == "get_d_parameters") {
                requestParameters();
            }
            else if (cmd == "reboot") {
                qInfo() << "==> Reboot Received";
                resetDatabase();
                qInfo() << "  DB Reseted";
                ErrorSimulationSentinelVal = 0;
                QCoreApplication::exit(0);
            }
            else if (cmd == "send_msg_log") {
                qInfo() << "==> MSG:" << inner.value("msg").toString();
            }
            else if (cmd == "changed_parameters") {
                qInfo() << "\n\nWARNING: System UNSTABLE";
                ErrorSimulationSentinelVal = 1;
            }
            else if (cmd == "ping") {
                onPingTimeout();
            }
            else if (cmd == "refresh") {
                resetDatabase();
                ErrorSimulationSentinelVal = 0;
                setErroSimulation_LOW();
            }
            else {
                qWarning() << "Unknown Command:" << cmd;
            }
        }
    }
}

void DvClient::resetDatabase()
{
    if (db.isOpen()) db.close();
    QString path = QCoreApplication::applicationDirPath() + "/warnings.db";
    if (QFile::exists(path) && !QFile::remove(path))
        qWarning() << "Failed to remove DB file:" << path;
    initDatabase();
}

void DvClient::setCOMSentinel(int value)
{
    comSentinel = value;
    qDebug() << "COMsentinel set to:" << value;
}

void DvClient::updateDistance(float distance)
{
    currentDistance = distance;
}

void DvClient::requestParameters()
{
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
{
    qWarning() << "WS error:" << socket.errorString();
}

void DvClient::onPingTimeout()
{
    socket.sendTextMessage("42[\"ping\",{}]");
}

QString DvClient::buildDvOpUrl(const QString &session)
{
    QUrl u("https://dev-kodx.mepsan.com.tr/dv/DvOp");
    QUrlQuery q;
    q.addQueryItem("pts", QString::number(QDateTime::currentMSecsSinceEpoch()));
    q.addQueryItem("S[S]", session);
    q.addQueryItem("S[ptof]", "180");
    q.addQueryItem("S[country]", "225");
    q.addQueryItem("S[lang]", "tr");
    q.addQueryItem("S[serial_no]", "251306200097");
    q.addQueryItem("S[serial_no_hw]", "724564889999");
    q.addQueryItem("d_short_code", "kodxmcu_avenda_lindo_01");
    q.addQueryItem("d_firmware", "kodxmcu_avenda_lindo_01");
    q.addQueryItem("d_mac_id", "00:30:18:03:26:88");
    q.addQueryItem("d_local_ip", "192.168.5.172");
    q.addQueryItem("d_oper", "Prod");
    q.addQueryItem("d_mdl_id", "9100200");
    q.addQueryItem("d_sites_id", "9100200");
    u.setQuery(q);
    return u.toString(QUrl::FullyEncoded);
}

void DvClient::uploadLogFile()
{
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

    QNetworkRequest req(QUrl("https://dev-kodx.mepsan.com.tr/dl/DvLogUp"));
    req.setRawHeader("Cookie", QByteArray("S=") + sessionId.toUtf8());
    req.setRawHeader("sys_objects_name", "alperen_test");
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
{
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
{
    QString file = QCoreApplication::applicationDirPath() + "/sessionID.txt";
    QFile f(file);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        sessionId = QString::fromUtf8(f.readAll()).trimmed();
        haveSavedSession = true;
    }
}

void DvClient::saveSession()
{
    QString file = QCoreApplication::applicationDirPath() + "/sessionID.txt";
    QFile f(file);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        f.write(sessionId.toUtf8());
    }
}

void DvClient::setErrorSimulation(bool enable)
{
    ErrorSimulationSentinelVal = enable ? 1 : 0;
}

void DvClient::setErroSimulation_LOW()
{
    ErrorSimulationSentinelVal = 0;
}

void DvClient::rebootComPorts()
{
    if (m_portManager)
        m_portManager->reloadPorts();
}

void DvClient::comUseIdle()                { if (m_portManager) m_portManager->setModeIdle(); }
void DvClient::comUseSimulationOnly()      { if (m_portManager) m_portManager->setModeSimulation(); }

void DvClient::comUseSinglePort(const QString &p)
{
    if (m_portManager)
        m_portManager->setModeSingle(p);
}

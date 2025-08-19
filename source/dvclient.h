#ifndef DVCLIENT_H
#define DVCLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QWebSocket>
#include <QTimer>
#include <QSqlDatabase>
#include <QNetworkReply>
#include <QAbstractSocket>
#include <QRandomGenerator>
#include <QHttpMultiPart>
#include <QPair>
#include <QStringList>

class ComPortManager;

class DvClient : public QObject
{
    Q_OBJECT

public:
    explicit DvClient(QObject *parent = nullptr);
    ~DvClient() override;

    bool initDatabase();

    void start();
    void updateDistance(float distance);
    void setCOMSentinel(int value);
    void uploadLogFile();

    QSqlDatabase& database() { return db; }

    // COM selection helpers for UI
    QStringList serialPorts() const;                  // list available ports


public slots:
    void setErrorSimulation(bool enable);
    void setErroSimulation_LOW();
    void requestParameters();
    void resetDatabase();
    void rebootComPorts();

    // Modes
    void comUseIdle();
    void comUseSimulationOnly();
    void comUseSinglePort(const QString &portName);

signals:
    void newWarning(const QString &timestamp, const QString &level, double distance, double xn);

private slots:
    void onHttpFinished(QNetworkReply *reply);
    void onSocketTextMessageReceived(const QString &msg);
    void onSocketError(QAbstractSocket::SocketError error);
    void onPingTimeout();

private:
    QString buildDvOpUrl(const QString &session);
    QPair<QString, QString> getNetworkInfo();
    void loadSession();
    void saveSession();

    QNetworkAccessManager http;
    QWebSocket socket;
    QTimer pingTimer;
    QSqlDatabase db;
    ComPortManager *m_portManager;

    QString sessionId;
    QString corpsID;
    QString locationID;
    QString devicesID;

    bool haveSavedSession = false;
    bool registered = false;
    int ErrorSimulationSentinelVal = 0;
    int comSentinel = 0;
    float currentDistance = 0;
};

#endif // DVCLIENT_H

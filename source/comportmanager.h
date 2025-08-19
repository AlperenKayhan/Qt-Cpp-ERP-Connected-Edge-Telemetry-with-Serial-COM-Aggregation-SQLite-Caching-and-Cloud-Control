#ifndef COMPORTMANAGER_H
#define COMPORTMANAGER_H

#include <QObject>
#include <QVector>
#include <QStringList>

class ComThread;
class DvClient;

class ComPortManager : public QObject {
    Q_OBJECT
public:
    enum class Mode { Idle, AllPorts, SinglePort, SimulationOnly };

    explicit ComPortManager(DvClient* client, QObject* parent = nullptr);
    ~ComPortManager() override;

    // Query
    QStringList availablePorts() const;

public slots:
    void reloadPorts();
    void stopAll();
    void setModeIdle();
    void setModeSingle(const QString &portName);
    void setModeSimulation();

signals:
    void anyPortOpened();
    void allPortsClosed();

private slots:
    void onPortOpened(const QString &portName);
    void onDistance(float distance);
    void onPortOpenFailed(const QString &err);
    void onThreadFinished();

private:
    void startAll();
    void clearAll();

    DvClient *m_client;
    QVector<ComThread*> m_threads;
    int m_openCount = 0;

    Mode m_mode = Mode::Idle;     // start idle (no reading)
    QString m_selectedPort;
};

#endif // COMPORTMANAGER_H

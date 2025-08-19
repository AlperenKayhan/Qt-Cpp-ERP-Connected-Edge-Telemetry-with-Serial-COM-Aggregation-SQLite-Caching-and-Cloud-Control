#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTableView>
#include <QPushButton>
#include <QSqlTableModel>
#include <QPlainTextEdit>
#include <QComboBox>
#include "scatter3dwidget.h"

class DvClient;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(DvClient *client, QWidget *parent = nullptr);
    ~MainWindow() override;
    double LevelDetect(const QString &level);

public slots:
    void onNewWarning(const QString &timestamp, const QString &level, double distance, double xn);
    void appendLog(const QString &msg);

private slots:
    void onSendLogs();
    void onStartSensor();
    void onStopSensor();
    void onResetDatabase();
    void onGetParameters();
    void onReboot();
    void onPortChoiceChanged(int idx);

private:
    static MainWindow *s_instance;
    static void messageHandler(QtMsgType, const QMessageLogContext &, const QString &msg);
    void refreshPortList();

    DvClient       *client;
    QTableView     *tableView;
    QSqlTableModel *model;
    QComboBox      *portCombo;      // NEW
    QPushButton    *sendLogsButton;
    QPushButton    *startSensorButton;
    QPushButton    *stopSensorButton;
    QPushButton    *resetButton;
    QPushButton    *parametersButton;
    QPushButton    *rebootButton;
    Scatter3DWidget *scatterWidget;
    QPlainTextEdit *logOutput;
};

#endif // MAINWINDOW_H

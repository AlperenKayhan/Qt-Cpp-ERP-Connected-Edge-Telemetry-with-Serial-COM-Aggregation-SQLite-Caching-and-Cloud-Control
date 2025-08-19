#include <QApplication>
#include "dvclient.h"
#include "mainwindow.h"

int main(int argc,char *argv[]){
    QApplication app(argc,argv);
    DvClient client;
    if(!client.initDatabase())
        return -1;//DB SQLite var mı yok mu?
    MainWindow w(&client);
    w.show();
    client.start();
    return app.exec();
}

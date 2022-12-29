#include <QQuickItem>
#include "tcpserver.h"

using namespace TCPSERVER_PRIVATE;

TcpServer::TcpServer(ControlPanel *ctl, QObject *parent):
    QTcpServer(parent), controlPanel(ctl)
{
    connect(this, SIGNAL(recordButtonClick()),
            controlPanel->rootObject->findChild<QQuickItem*>("bRecord"),
            SIGNAL(activated()));
    connect(this, SIGNAL(stopButtonClick()),
            controlPanel->rootObject->findChild<QQuickItem*>("bStop"),
            SIGNAL(activated()));
    connect(this, SIGNAL(tell_socket_port(QString)),
            controlPanel,
            SLOT(receiveMessage(QString)));
}


void TcpServer::on_socket_activate(bool activate)
{
    if(activate){
        quint16 port = 20172;
        if(listen(QHostAddress::Any, port)
              || listen(QHostAddress::Any, port+1)
              || listen(QHostAddress::Any, port+2)
              || listen(QHostAddress::Any, port+3)
              || listen(QHostAddress::Any, port+4)
              || listen(QHostAddress::Any))
        {
            qDebug() << "Server started!" << this->serverPort();
            emit tell_socket_port(QString("Connect socket to PORT [%1].").arg(this->serverPort()));
        }
        else
        {
            qDebug() << "Server could not start";
            emit tell_socket_port(QString("Warning: connect socket failed"));
        }
    }
    else{
        this->close();
    }
}

// This function is called by QTcpServer when a new connection is available.
void TcpServer::incomingConnection(qintptr socketDescriptor)
{
    // We have a new connection
    qDebug() << socketDescriptor << " Connecting...";

    // Every new connection will be run in a newly created thread
    MyThread *thread = new MyThread(socketDescriptor, this);

    // connect signal/slot
    // once a thread is not needed, it will be beleted later
    connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
    thread->start();
}

bool TcpServer::tcpcmd_start_record(char *outmsg)
{
    bool currentstuts = controlPanel->rootObject->property("recording").toBool();
    if(currentstuts){
        strcpy(outmsg, "Error! Already started before.");
        return false;
    }
    bool btn_enabled = true;
    if(!btn_enabled){
        strcpy(outmsg, "Error! Device not connected.");
        return false;
    }
    emit recordButtonClick();
    strcpy(outmsg, "OK! Task should be starting.");
    return true;
}

bool TcpServer::tcpcmd_stop_record(char *outmsg)
{
    bool currentstuts = controlPanel->rootObject->property("recording").toBool();
    if(!currentstuts){
        strcpy(outmsg, "Error! Already stopped before.");
        return false;
    }
    bool btn_enabled = true;
    if(!btn_enabled){
        strcpy(outmsg, "Error! Device not connected.");
        return false;
    }
    emit stopButtonClick();
    strcpy(outmsg, "OK! Task should be stopping.");
    return true;
}

int TcpServer::tcpcmd_query_record(char *outmsg)
{
    bool btn_enabled = true;
    if(!btn_enabled){
        strcpy(outmsg, "Device not connected.");
        return 0;
    }
    bool currentstuts = controlPanel->rootObject->property("recording").toBool();
    if(currentstuts){
        strcpy(outmsg, "Running.");
        return 2;
    }
    else{
        strcpy(outmsg, "Stopped.");
        return 1;
    }
}

MyThread::MyThread(qintptr ID, TcpServer *tcpServer, QObject *parent) :
    QThread(parent)
{
    this->socketDescriptor = ID;
    this->tcpServer = tcpServer;
}

void MyThread::run()
{
    // thread starts here
    qDebug() << " Thread started";

    socket = new QTcpSocket();

    // set the ID
    if(!socket->setSocketDescriptor(this->socketDescriptor))
    {
        // something's wrong, we just emit a signal
        emit error(socket->error());
        return;
    }

    // connect socket and signal
    // note - Qt::DirectConnection is used because it's multithreaded
    //        This makes the slot to be invoked immediately, when the signal is emitted.

    connect(socket, SIGNAL(readyRead()), this, SLOT(readyRead()), Qt::DirectConnection);
    connect(socket, SIGNAL(disconnected()), this, SLOT(disconnected()));

    // We'll have multiple clients, we want to know which is which
    qDebug() << socketDescriptor << " Client connected";

    // make this thread a loop,
    // thread will stay alive so that signal/slot to function properly
    // not dropped out in the middle when thread dies
    exec();
}

void MyThread::disconnected()
{
    qDebug() << socketDescriptor << " Disconnected";
    socket->deleteLater();
    exit(0);
}

void MyThread::readyRead()
{
    const int MaxLength = 1024;
    char buffer[MaxLength+1];
    qint64 byteCount = socket->read(buffer, MaxLength);
    buffer[byteCount] = 0;
    qDebug() << socket->bytesAvailable() << buffer;

    char response[MaxLength+1];
    if(strcmp(buffer, "start_record")==0)
    {
        tcpServer->tcpcmd_start_record(response);
    }
    else if(strcmp(buffer, "stop_record")==0)
    {
        tcpServer->tcpcmd_stop_record(response);
    }
    else if(strcmp(buffer, "query_record")==0)
    {
        tcpServer->tcpcmd_query_record(response);
    }
    else
    {
        strcpy(response, "Error! Not a valid command.");
    }
    socket->write(response);
    socket->flush();
    socket->waitForBytesWritten(100);
}

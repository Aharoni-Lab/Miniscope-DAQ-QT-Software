#include "miniscope.h"
#include "videodisplay.h"

#include <QQuickView>
#include <QSemaphore>
#include <QObject>
#include <QTimer>
#include <QAtomicInt>


Miniscope::Miniscope(QObject *parent) :
    QObject(parent),
    vidDisplay(0),
    m_previousDisplayFrameNum(0),
    m_acqFrameNum(new QAtomicInt(0)),
    rootObject(0),
    miniscopeStream(0)
{
    freeFrames = new QSemaphore;
    usedFrames = new QSemaphore;
    freeFrames->release(FRAME_BUFFER_SIZE);

    miniscopeStream = new VideoStreamOCV;
    miniscopeStream->setCameraID(0);
    miniscopeStream->setBufferParameters(frameBuffer,FRAME_BUFFER_SIZE,freeFrames,usedFrames,m_acqFrameNum);

    videoStreamThread = new QThread;


//    QObject::connect(miniscopeStream, SIGNAL (error(QString)), this, SLOT (errorString(QString)));
    QObject::connect(videoStreamThread, SIGNAL (started()), miniscopeStream, SLOT (startStream()));
//    QObject::connect(miniscopeStream, SIGNAL (finished()), videoStreamThread, SLOT (quit()));
//    QObject::connect(miniscopeStream, SIGNAL (finished()), miniscopeStream, SLOT (deleteLater()));
    QObject::connect(videoStreamThread, SIGNAL (finished()), videoStreamThread, SLOT (deleteLater()));


    createView();
    connectSnS();

    miniscopeStream->moveToThread(videoStreamThread);
    videoStreamThread->start();
}

void Miniscope::createView()
{
    qmlRegisterType<VideoDisplay>("VideoDisplay", 1, 0, "VideoDisplay");

    QQuickView *view = new QQuickView;
    const QUrl url(QStringLiteral("qrc:/miniscope.qml"));

    view->setSource(url);
    view->show();

    rootObject = view->rootObject();
    vidDisplay = rootObject->findChild<VideoDisplay*>("vD");

    testImage.load("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png");

    QObject::connect(vidDisplay->window(), &QQuickWindow::beforeRendering, this, &Miniscope::sendNewFrame);

        if (vidDisplay) {
            qDebug() << "Found vid display: " << vidDisplay->displayFrame().height();
//
        }

}

void Miniscope::connectSnS(){
    QObject::connect(rootObject, SIGNAL( vidPropChangedSignal(QString,double) ),
                         miniscopeStream, SLOT( setProperty(QString,double) ));
}

void Miniscope::testSlot(QString type, double value)
{
    qDebug() << "IN SLOT!!!!! " << type << " is " << value;
}
void Miniscope::sendNewFrame(){
//    vidDisplay->setProperty("displayFrame", QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
    int f = *m_acqFrameNum;

    if (f > m_previousDisplayFrameNum) {
        m_previousDisplayFrameNum = f;
        QImage tempFrame;
//        qDebug() << "Send frame = " << f;
        f = (f - 1)%FRAME_BUFFER_SIZE;
        tempFrame = QImage(frameBuffer[f].data, frameBuffer[f].cols, frameBuffer[f].rows, frameBuffer[f].step, QImage::Format_RGB888);
        vidDisplay->setDisplayFrame(tempFrame);
    }
}


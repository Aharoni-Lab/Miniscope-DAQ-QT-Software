#include "miniscope.h"
#include "newquickview.h"
#include "videodisplay.h"
#include "videodevice.h"

#include <QQuickView>
#include <QQuickItem>
#include <QSemaphore>
#include <QObject>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QQmlApplicationEngine>
#include <QVector>


Miniscope::Miniscope(QObject *parent, QJsonObject ucDevice) :
    VideoDevice(parent, ucDevice),
    baselineFrameBufWritePos(0),
    baselinePreviousTimeStamp(0),
    m_displatState("Raw")
{

    m_ucDevice = ucDevice; // hold user config for this device
    m_cDevice = getDeviceConfig(m_ucDevice["deviceType"].toString());

    QObject::connect(this, &VideoDevice::displayCreated, this, &Miniscope::displayHasBeenCreated);
//    QObject::connect(this, &Miniscope::displayCreated, this, &Miniscope::displayHasBeenCreated);

}
void Miniscope::setupDisplayObjectPointers()
{
    // display object can only be accessed after backend call createView()
    rootDistplayObject = getRootDisplayObject();
    if (getHeadOrienataionStreamState())
        bnoDisplay = getRootDisplayChild("bno");
}
void Miniscope::handleDFFSwitchChange(bool checked)
{
    qDebug() << "Switch" << checked;
    if (checked)
        m_displatState = "dFF";
    else
        m_displatState = "Raw";
}

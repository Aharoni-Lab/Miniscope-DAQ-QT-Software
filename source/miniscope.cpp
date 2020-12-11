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
    VideoDevice(parent, ucDevice)
{


}

#ifndef MINISCOPE_H
#define MINISCOPE_H

#include <QObject>
#include <QThread>
#include <QSemaphore>
#include <QTimer>
#include <QAtomicInt>
#include <QJsonObject>
#include <QQuickView>
#include <QMap>
#include <QVector>
#include <QQuickItem>
#include <QVariant>

#include "videostreamocv.h"
#include "videodisplay.h"
#include "newquickview.h"
#include "videodevice.h"

#include <opencv2/opencv.hpp>


#define BASELINE_FRAME_BUFFER_SIZE  128


class Miniscope : public VideoDevice
{

public:
    explicit Miniscope(QObject *parent = nullptr, QJsonObject ucMiniscope = QJsonObject());
};


#endif // MINISCOPE_H

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


#define PROTOCOL_I2C            -2
#define PROTOCOL_SPI            -3
#define SEND_COMMAND_VALUE_H    -5
#define SEND_COMMAND_VALUE_L    -6
#define SEND_COMMAND_VALUE      -6
#define SEND_COMMAND_VALUE_H16  -7
#define SEND_COMMAND_VALUE_H24  -8
#define SEND_COMMAND_VALUE2_H   -9
#define SEND_COMMAND_VALUE2_L   -10
#define SEND_COMMAND_ERROR      -20

#define FRAME_BUFFER_SIZE   128
#define BASELINE_FRAME_BUFFER_SIZE  128


class Miniscope : public VideoDevice
{

public:
    explicit Miniscope(QObject *parent = nullptr, QJsonObject ucMiniscope = QJsonObject());
};


#endif // MINISCOPE_H

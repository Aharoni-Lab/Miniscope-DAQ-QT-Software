#ifndef VIDEOSTREAMLIBUVC_H
#define VIDEOSTREAMLIBUVC_H

// libuvc capture backend - Linux only. Built only when CMake finds libuvc and
// defines HAVE_LIBUVC. See videostreambase.h for why this exists (uvcvideo
// caches UVC control reads; libuvc GET_CUR bypasses that cache so the live
// frame counter and BNO head-orientation registers can be read on Linux).
#ifdef HAVE_LIBUVC

#include <QVector>
#include <opencv2/core/core.hpp>

#include <libuvc/libuvc.h>

#include "miniscopeprotocol.h"
#include "videostreambase.h"

class VideoStreamLibUVC : public VideoStreamBase
{
    Q_OBJECT
public:
    explicit VideoStreamLibUVC(QObject *parent = nullptr, int width = 0, int height = 0, double pixelClock = 0);
    ~VideoStreamLibUVC() override;

    int connect2Camera(int cameraID) override;

public slots:
    void startStream() override;
    void startRecording() override;
    void stopRecording() override;

protected:
    bool writeControlWord(quint8 selector, quint16 word) override;   // uvc_set_ctrl + settle
    bool readControl(quint8 selector, quint16 *value) override;      // fresh uvc_get_ctrl GET_CUR
    bool attemptReconnect() override;
    // Frames arrive as raw YUYV; the base's BGR copy/convert doesn't apply.
    void convertToSlot(const cv::Mat &frame, cv::Mat &slot) override;

private:
    bool openByVideoIndex(int cameraID);   // resolve /dev/videoN -> USB bus/addr, open via libuvc
    bool negotiateFormat();
    void closeStream();
    void closeDevice();

    uvc_context_t *m_ctx;
    uvc_device_t *m_dev;
    uvc_device_handle_t *m_devh;
    uvc_stream_ctrl_t m_streamCtrl;
    uvc_stream_handle_t *m_strmh;
    int m_negotiatedFps;
};

#endif // HAVE_LIBUVC
#endif // VIDEOSTREAMLIBUVC_H

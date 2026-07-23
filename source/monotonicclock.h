#ifndef MONOTONICCLOCK_H
#define MONOTONICCLOCK_H

#include <QElapsedTimer>

// Process-wide monotonic millisecond clock for frame timestamps and trace
// time axes. Unlike QDateTime::currentMSecsSinceEpoch(), it can never jump
// (NTP sync, DST, manual clock change) mid-recording - a jump would corrupt
// every inter-frame interval in timeStamps.csv. Absolute wall-clock time is
// recorded once per recording in metaData.json ("recordingStartTime").
inline qint64 monotonicTimeMs()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}

#endif // MONOTONICCLOCK_H

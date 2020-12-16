#ifndef TRACEDISPLAY_H
#define TRACEDISPLAY_H

#include "newquickview.h"

#include <QtQuick/QQuickItem>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLTexture>
#include <QtGui/QOpenGLBuffer>

#include <QJsonObject>
#include <QVector>
#include <QAtomicInt>

typedef struct Traces{

    Traces(float colors[3], float scale, QAtomicInt *displayBufNum, QAtomicInt *numDataInBuf, int bufSize, float *dataT, float *dataY):
        scale(scale),
        bufferSize(bufSize),
        displayBufferNumber(displayBufNum),
        numDataInBuffer(numDataInBuf),
        dataT(dataT),
        dataY(dataY)
    {
        color[0] = colors[0];
        color[1] = colors[1];
        color[2] = colors[2];
    }

    float color[3];
    float offset = 0.0f;
    float scale;
    int bufferSize;

    QAtomicInt* displayBufferNumber;
    QAtomicInt* numDataInBuffer;
    float* dataT;
    float* dataY;
} trace_t;

class TraceDisplayBackend : public QObject
{
    Q_OBJECT
public:
    TraceDisplayBackend(QObject *parent = nullptr, QJsonObject ucTraceDisplay = QJsonObject());
    void createView();

public slots:
    void close();

private:
    NewQuickView *view;
    QJsonObject m_ucTraceDisplay;
};

class TraceDisplayRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    TraceDisplayRenderer();
    ~TraceDisplayRenderer();

    void setT(qreal t) { m_t = t; }
    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }


    void initPrograms();

    void addNewTrace(float color[3], float scale, QAtomicInt* displayBufNum, QAtomicInt* numDataInBuf, int bufSize, float* dataT, float* dataY);

    // Display funtions for vertical grid lines
    void initGridV();
    void updateGridV();
    void drawGridV();
    QOpenGLBuffer gridVVOB;

    // Display funtions for horizontal grid lines
    void initGridH();
    void updateGridH();
    void drawGridH();
    QOpenGLBuffer gridHVOB;

    // Display functions for moving bar
    void initMovingBar();
    void updateMovingBar();
    void drawMovingBar();
    QOpenGLBuffer movingBarVBO;

    // Display functions for moving bar
    void initTraces();
    void updateTraces();
    void drawTraces();
//    QOpenGLBuffer tracesVBO;

    // User controls to change trace display view
    float pan[2];
    float scale[2];
    float magnify[2];

    float windowSize; // in seconds
    float gridSpacingV;

    float startTime;
    float currentTime;

//signals:

public slots:
    void paint();

private:
    QSize m_viewportSize;
    QOpenGLShaderProgram *m_program;
    QOpenGLTexture *m_texture;
    QQuickWindow *m_window;


    qreal m_t;

    // Vars for display
    int m_numTraces;

    // holds everything about traces
    QVector<trace_t> traces;

    // Programs used for trace display
    QOpenGLShaderProgram *m_programGridV;
    QOpenGLShaderProgram *m_programGridH;
    QOpenGLShaderProgram *m_programMovingBar;
    QOpenGLShaderProgram *m_programTraces;

    QAtomicInt bufNum;
    QAtomicInt numData[2];
    float dataT[2][10];
    float dataY[2][10];

};

class TraceDisplay : public QQuickItem
{
    Q_OBJECT
    // FOr updating xlabel values
    Q_PROPERTY(QList<QVariant > xLabel READ xLabel WRITE setXLabel NOTIFY xLabelChanged)
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)

public:
    TraceDisplay();
    QList<QVariant > xLabel() { return m_xLabel; }
    void setXLabel(QList<QVariant > label) {m_xLabel = label; xLabelChanged();}
    qreal t() const { return m_t; }
    void setT(qreal t);


signals:
    void xLabelChanged();
    void tChanged();

public slots:
    void sync();
    void cleanup();

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    qreal m_t;
    QList<QVariant > m_xLabel;
    TraceDisplayRenderer *m_renderer;

};

#endif // TRACEDISPLAY_H

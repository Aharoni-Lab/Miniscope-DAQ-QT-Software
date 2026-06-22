#ifndef TRACEDISPLAY_H
#define TRACEDISPLAY_H

#include "newquickview.h"

#include <QtQuick/QQuickItem>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLTexture>
#include <QtGui/QOpenGLBuffer>
#include <QtGui/QOpenGLFramebufferObject>

#include <QJsonObject>
#include <QVector>
#include <QAtomicInt>
#include <QString>

typedef struct Traces{

    Traces(QString name, float colors[3], float scale, QString units, bool sameOffset, QAtomicInt *displayBufNum, QAtomicInt *numDataInBuf, int bufSize, float *dataT, float *dataY):
        name(name),
        scale(scale),
        units(units),
        sameOffsetAsPrevious(sameOffset),
        bufferSize(bufSize),
        displayBufferNumber(displayBufNum),
        numDataInBuffer(numDataInBuf),
        dataT(dataT),
        dataY(dataY)
    {
        color = colors;
//        color[0] = colors[0];
//        color[1] = colors[1];
//        color[2] = colors[2];
    }
    QString name = "";
    float* color;
    float offset = 0.0f;
    float scale;
    QString units;
    bool sameOffsetAsPrevious = false;
    int bufferSize;

    QAtomicInt* displayBufferNumber;
    QAtomicInt* numDataInBuffer;
    float* dataT;
    float* dataY;

} trace_t;



class TraceDisplayRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    TraceDisplayRenderer(QObject *parent = nullptr, QSize displayWindowSize = QSize(), qint64 softwareStartTime = 0);
    ~TraceDisplayRenderer();

    void setT(qreal t) { m_t = t; }
    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }
    trace_t getTrace(int idx) { return traces[idx]; }
    float getGlobalScaling() { return m_globalScale; }
    int getNumOffets() { return m_numOffsets; }

    void doubleClickEvent(int x, int y);

    void initPrograms();

    void addNewTrace(trace_t newTrace);
    void updateTraceOffsets();

    // Display function for rendered texture
    void initTextureProgram();
    void drawRenderTexture();

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

    // Display functions for selected horizontal grid lines
    void initSelectedGridH();
    void updateSelectedGridH();
    void drawSelectedGridH();
    QOpenGLBuffer selectedGridHVOB;

    // Display functions for moving bar
    void initMovingBar();
    void updateMovingBar();
    void drawMovingBar();
    QOpenGLBuffer movingBarVBO;

    // Display functions for moving bar
    void initTraces();
    void updateTraces();
    void drawTraces();

    // Handle mouse events
    void updatePan(float deltaX, float deltaY);
    void handleWheelEvent(int scrollAmount, QVector<Qt::Key> keyMods);

    // User controls to change trace display view
    float pan[2];
    float scale[2];
    float magnify[2];

    float windowSize; // in seconds
    float gridSpacingV;

    qint64 startTime;
    qint64 currentTime;
    qint64 m_softwareStartTime;

    QVector<int> m_selectedTrace;

//signals:

public slots:
    void paint();

private:

    QSize m_viewportSize;
    QOpenGLShaderProgram *m_program;
    QOpenGLTexture *m_texture;
    QQuickWindow *m_window;


    qreal m_t;

    float m_globalScale;

    // Vars for display
    int m_numTraces;
    int m_numOffsets;

    // holds everything about traces
    QVector<trace_t> traces;

    // Frame Buffer
    QOpenGLFramebufferObject* m_fbo;
    // Programs used for trace display
    QOpenGLShaderProgram *m_programTexture;
    QOpenGLShaderProgram *m_programGridV;
    QOpenGLShaderProgram *m_programGridH;
    QOpenGLShaderProgram *m_programSelectedGridH;
    QOpenGLShaderProgram *m_programMovingBar;
    QOpenGLShaderProgram *m_programTraces;

    QAtomicInt bufNum;
    QAtomicInt numData[2];
    qint64 dataT[2][10];
    float dataY[2][10];

    qint64 m_lastTimeDisplayed;

    bool m_clearDisplayOnNextDraw;

};

class TraceDisplay : public QQuickItem
{
    Q_OBJECT
    // FOr updating xlabel values
    Q_PROPERTY(QList<QVariant > xLabel READ xLabel WRITE setXLabel NOTIFY xLabelChanged)
    Q_PROPERTY(QList<QVariant > ySelectedLabel READ ySelectedLabel WRITE setYSelectedLabel NOTIFY ySelectedLabelChanged)
    Q_PROPERTY(QList<QVariant > traceNames READ traceNames WRITE setTraceNames NOTIFY traceNamesChanged)
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)

public:
    TraceDisplay();

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

    QList<QVariant > xLabel() { return m_xLabel; }
    QList<QVariant > ySelectedLabel() { return m_ySelectedLabel; }
    QList<QVariant > traceNames() { return m_traceNames; }
    void setXLabel(QList<QVariant > label) {m_xLabel = label; xLabelChanged();}
    void setYSelectedLabel(QList<QVariant > label) {m_ySelectedLabel = label; ySelectedLabelChanged();}
    void setTraceNames(QList<QVariant > names) {m_traceNames = names; traceNamesChanged();}
    qreal t() const { return m_t; }
    void setT(qreal t);
    void addNewTrace(trace_t newTrace);
    void updateYSelectLabels();

    void setSoftwareStartTime(qint64 time) { m_softwareStartTime = time; }


signals:
    void xLabelChanged();
    void ySelectedLabelChanged();
    void traceNamesChanged();
    void tChanged();

public slots:
    void sync();
    void cleanup();

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    qreal m_t;
    QList<QVariant > m_xLabel;
    QList<QVariant> m_traceNames;
    QList<QVariant> m_ySelectedLabel;
    TraceDisplayRenderer *m_renderer;

    QVector<trace_t> m_tempTraces;


    QMouseEvent* lastMouseClickEvent;
    QMouseEvent* lastMouseReleaseEvent;
    QMouseEvent* lastMouseMoveEvent;

    qint64 m_softwareStartTime;

    // modifier keys
    QVector<Qt::Key> m_keyMods;



};

class TraceDisplayBackend : public QObject
{
    Q_OBJECT
public:
    TraceDisplayBackend(QObject *parent = nullptr, QJsonObject ucTraceDisplay = QJsonObject(), qint64 softwareStartTime = 0);
    void createView();

public slots:
    void addNewTrace(QString name, float color[3], float scale, QString units, bool sameOffset, QAtomicInt* displayBufNum, QAtomicInt* numDataInBuf, int bufSize, float* dataT, float* dataY);
    void close();

private:
    NewQuickView *view;
    TraceDisplay* m_traceDisplay;
    QJsonObject m_ucTraceDisplay;

    qint64 m_softwareStartTime;

};

#endif // TRACEDISPLAY_H

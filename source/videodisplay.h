#ifndef VIDEODISPLAY_H
#define VIDEODISPLAY_H

#include <QtQuick/QQuickItem>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLFunctions>
#include <QtGui/QOpenGLTexture>

#include <QImage>



//! [1]
class VideoDisplayRenderer : public QObject, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    VideoDisplayRenderer() :
        m_newFrame(false),
        m_t(0),
        m_displayFrame(nullptr),
        m_program(nullptr),
        m_texture(nullptr),
        m_alpha(1),
        m_beta(0),
        m_showStaturation(1)
    { }
    ~VideoDisplayRenderer();

    void setT(qreal t) { m_t = t; }
    void setDisplayFrame(QImage frame) {m_displayFrame = frame.copy(); m_newFrame = true;}
    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }
    void setAlpha(double a) {m_alpha = a;}
    void setBeta(double b) {m_beta = b;}
    void setShowSaturation(double value) {m_showStaturation = value; }


    bool m_newFrame;

signals:
    void requestNewFrame();

public slots:
    void paint();

private:
    QSize m_viewportSize;
    qreal m_t;
    QImage m_displayFrame;
    QOpenGLShaderProgram *m_program;
    QOpenGLTexture *m_texture;
    QQuickWindow *m_window;


    double m_alpha;
    double m_beta;
    double m_showStaturation;


};
//! [1]

//! [2]
class VideoDisplay : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(double acqFPS READ acqFPS WRITE setAcqFPS NOTIFY acqFPSChanged)
    Q_PROPERTY(double droppedFrameCount READ droppedFrameCount WRITE setDroppedFrameCount NOTIFY droppedFrameCountChanged)
    Q_PROPERTY(int bufferUsed READ bufferUsed WRITE setBufferUsed NOTIFY bufferUsedChanged)
    Q_PROPERTY(int maxBuffer READ maxBuffer WRITE setMaxBuffer NOTIFY maxBufferChanged)
    Q_PROPERTY(qreal t READ t WRITE setT NOTIFY tChanged)
//    Q_PROPERTY(QImage displayFrame READ displayFrame WRITE setDisplayFrame NOTIFY displayFrameChanged)

    // For visualizing ROI
    Q_PROPERTY(QList<int> ROI READ ROI WRITE setROI NOTIFY roiChanged)
    Q_PROPERTY(QList<int> addTraceROI READ addTraceROI WRITE setAddTraceROI NOTIFY addTraceROIChanged)

public:
    VideoDisplay();
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    qreal t() const { return m_t; }
    double acqFPS() const { return m_acqFPS; }
    QList<int> ROI() const { return m_ROI; }
    QList<int> addTraceROI() const { return m_addTraceROI; }
    int maxBuffer() const {return m_maxBuffer; }
    int bufferUsed() const { return m_bufferUsed; }
    int droppedFrameCount() const {return m_droppedFrameCount; }

//    QImage displayFrame() { return m_displayFrame2; }
    void setT(qreal t);
    void setAcqFPS(double acqFPS) { m_acqFPS = acqFPS; acqFPSChanged();}
    void setROI(QList<int> roi);
    void setAddTraceROI(QList<int> roi);
    void setBufferUsed(int bufUsed) { m_bufferUsed = bufUsed; }
    void setMaxBuffer(int maxBuf) { m_maxBuffer = maxBuf; }

    void setDroppedFrameCount(int count) { m_droppedFrameCount = count; }
    void setDisplayFrame(QImage frame);
    void setAlpha(double a) {m_renderer->setAlpha(a);}
    void setBeta(double b) {m_renderer->setBeta(b);}
    void setShowSaturation(double value);
    void setROISelectionState(bool state) { m_roiSelectionActive = state; }
    void addTraceROISelectionState(bool state) { m_addTraceRoiSelectionActive = state; }
    void setWindowScaleValue(double scale) { m_windowScaleValue = scale; }

signals:
    void tChanged();
    void acqFPSChanged();
    void roiChanged();
    void addTraceROIChanged();
    void maxBufferChanged();
    void bufferUsedChanged();
    void droppedFrameCountChanged();

    void displayFrameChanged();
    void newROISignal(int leftEdge, int topEdge, int width, int height);
    void newAddTraceROISignal(int leftEdge, int topEdge, int width, int height);

public slots:
    void sync();
    void cleanup();

private slots:
    void handleWindowChanged(QQuickWindow *win);

private:
    qreal m_t;
    double m_acqFPS;
    int m_bufferUsed;
    int m_maxBuffer;
    int m_droppedFrameCount;
//    QImage m_displayFrame2;
    VideoDisplayRenderer *m_renderer;

    double m_showSaturation;
    bool m_roiSelectionActive;
    bool m_addTraceRoiSelectionActive;
    double m_windowScaleValue;
    QList<int> m_ROI;
    QList<int> m_addTraceROI;
    QMouseEvent *lastMouseClickEvent;
    QMouseEvent *lastMouseReleaseEvent;
};
//! [2]

#endif // VIDEODISPLAY_H

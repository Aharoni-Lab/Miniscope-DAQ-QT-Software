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
        m_t(0),
        m_displayFrame(nullptr),
        m_program(nullptr),
        m_texture(nullptr),
        m_newFrame(false),
        m_alpha(1),
        m_beta(0),
        m_showStaturation(1)
    { }
    ~VideoDisplayRenderer();

    void setT(qreal t) { m_t = t; }
    void setDisplayFrame(QImage frame) { m_displayFrame = frame; m_newFrame = true;}
    void setViewportSize(const QSize &size) { m_viewportSize = size; }
    void setWindow(QQuickWindow *window) { m_window = window; }
    void setAlpha(double a) {m_alpha = a;}
    void setBeta(double b) {m_beta = b;}
    void setShowSaturation(double value) {m_showStaturation = value; }

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
    bool m_newFrame;

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
    Q_PROPERTY(QImage displayFrame READ displayFrame WRITE setDisplayFrame NOTIFY displayFrameChanged)

public:
    VideoDisplay();
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    qreal t() const { return m_t; }
    double acqFPS() const { return m_acqFPS; }
    int maxBuffer() const {return m_maxBuffer; }
    int bufferUsed() const { return m_bufferUsed; }
    int droppedFrameCount() const {return m_droppedFrameCount; }

    QImage displayFrame() { return m_displayFrame2; }
//    QImage displayFrame() {return m_displayFrame;}
    void setT(qreal t);
    void setAcqFPS(double acqFPS) { m_acqFPS = acqFPS; acqFPSChanged();}
    void setBufferUsed(int bufUsed) { m_bufferUsed = bufUsed; }
    void setMaxBuffer(int maxBuf) { m_maxBuffer = maxBuf; }

    void setDroppedFrameCount(int count) { m_droppedFrameCount = count; }
    void setDisplayFrame(QImage frame);
    void setAlpha(double a) {m_renderer->setAlpha(a);}
    void setBeta(double b) {m_renderer->setBeta(b);}
    void setShowSaturation(double value);

signals:
    void tChanged();
    void acqFPSChanged();
    void maxBufferChanged();
    void bufferUsedChanged();
    void droppedFrameCountChanged();

    void displayFrameChanged();

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
    QImage m_displayFrame2;
    VideoDisplayRenderer *m_renderer;

    double m_showSaturation;
};
//! [2]

#endif // VIDEODISPLAY_H

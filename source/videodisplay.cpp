#include "videodisplay.h"

#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLTexture>

#include <QImage>

//! [7]
VideoDisplay::VideoDisplay()
    : m_t(0),
      m_acqFPS(0),
      m_renderer(nullptr),
      m_roiSelectionActive(false),
      m_ROI({0,0,10,10,0}),
      lastMouseClickEvent(nullptr),
      lastMouseReleaseEvent(nullptr)
{
//    m_displayFrame2.load("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png");
    setAcceptedMouseButtons(Qt::AllButtons);
    connect(this, &QQuickItem::windowChanged, this, &VideoDisplay::handleWindowChanged);

}
//! [7]

//! [8]
void VideoDisplay::setT(qreal t)
{
    if (t == m_t)
        return;
    m_t = t;
    emit tChanged();
    if (window())
        window()->update();
}
void VideoDisplay::setDisplayFrame(QImage frame) {
//    m_displayFrame2 = frame;
    // Checking to see if there is already a new frame waiting has solved a
    // consistent source of crash.
    // This crash was due to the texture data of the next frame in the queue to be displayed
    // changing before it is moved to GPU memory I think.
    if (m_renderer && !m_renderer->m_newFrame)
        m_renderer->setDisplayFrame(frame);
//    else
//        qDebug() << "New frame available before last frame was displayed";
}

void VideoDisplay::setShowSaturation(double value)
{
    m_showSaturation = value;
    if (m_renderer) {
        m_renderer->setShowSaturation(value);
    }
}
//void VideoDisplayRenderer::setDisplayFrame(QImage frame) {
//    m_displayFrame = frame;
//}
//! [8]

//! [1]
void VideoDisplay::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &VideoDisplay::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &VideoDisplay::cleanup, Qt::DirectConnection);
//! [1]
        // If we allow QML to do the clearing, they would clear what we paint
        // and nothing would show.
//! [3]
        win->setClearBeforeRendering(false);
    }
}
//! [3]

//! [6]
void VideoDisplay::cleanup()
{
    if (m_renderer) {
        delete m_renderer;
        m_renderer = nullptr;
    }
}

VideoDisplayRenderer::~VideoDisplayRenderer()
{
    delete m_program;
    delete m_texture;
}
//! [6]

//! [9]
void VideoDisplay::sync()
{
    if (!m_renderer) {
        m_renderer = new VideoDisplayRenderer();
        m_renderer->setShowSaturation(m_showSaturation);
//        m_renderer->setDisplayFrame(QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &VideoDisplayRenderer::paint, Qt::DirectConnection);
    }
    m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
//    m_renderer->setT(m_t);
//    m_renderer->setDisplayFrame(m_displayFrame);
    m_renderer->setWindow(window());
}
//! [9]

void VideoDisplay::mousePressEvent(QMouseEvent *event){
    if ((m_roiSelectionActive || m_addTraceRoiSelectionActive) && event->button() == Qt::LeftButton) {
        // TODO: Send info to shader to draw rectangle
        lastMouseClickEvent = new QMouseEvent(*event);
    }
//        qDebug() << "Mouse Press" << event;
}

void VideoDisplay::mouseMoveEvent(QMouseEvent *event) {

    if (m_roiSelectionActive /*&& event->button() == Qt::LeftButton*/ && lastMouseClickEvent != nullptr) {
        int leftEdge = (lastMouseClickEvent->x() < event->x()) ? (lastMouseClickEvent->x()) : (event->x());
        int topEdge = (lastMouseClickEvent->y() < event->y()) ? (lastMouseClickEvent->y()) : (event->y());
        int width = abs(lastMouseClickEvent->x() - event->x());
        int height = abs(lastMouseClickEvent->y() - event->y());

        setROI({leftEdge,topEdge,width,height,m_roiSelectionActive});
        qDebug() << "Mouse Move" << event;
    }
    if (m_addTraceRoiSelectionActive /*&& event->button() == Qt::LeftButton*/ && lastMouseClickEvent != nullptr) {
        int leftEdge = (lastMouseClickEvent->x() < event->x()) ? (lastMouseClickEvent->x()) : (event->x());
        int topEdge = (lastMouseClickEvent->y() < event->y()) ? (lastMouseClickEvent->y()) : (event->y());
        int width = abs(lastMouseClickEvent->x() - event->x());
        int height = abs(lastMouseClickEvent->y() - event->y());

        setAddTraceROI({leftEdge,topEdge,width,height,m_addTraceRoiSelectionActive});
//        qDebug() << "Mouse Move" << event;
    }
}

void VideoDisplay::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && lastMouseClickEvent != nullptr) {
        lastMouseReleaseEvent = event;

        // Calculate ROI properties
        int leftEdge = (lastMouseClickEvent->x() < lastMouseReleaseEvent->x()) ? (lastMouseClickEvent->x()) : (lastMouseReleaseEvent->x());
        int topEdge = (lastMouseClickEvent->y() < lastMouseReleaseEvent->y()) ? (lastMouseClickEvent->y()) : (lastMouseReleaseEvent->y());
        int width = abs(lastMouseClickEvent->x() - lastMouseReleaseEvent->x());
        int height = abs(lastMouseClickEvent->y() - lastMouseReleaseEvent->y());

        if (m_roiSelectionActive ) {
            m_roiSelectionActive = false;
            // Send new ROI to behavior camera class
            emit newROISignal(leftEdge, topEdge, width, height);
            setROI({leftEdge,topEdge,width,height,m_roiSelectionActive});

        }
        if (m_addTraceRoiSelectionActive ) {
            m_addTraceRoiSelectionActive = false;
            // Send new ROI to behavior camera class
            if (width < 5 && height < 5) {
                leftEdge -= 3;
                topEdge -= 3;
                width = 7;
                height = 7;
            }
            emit newAddTraceROISignal(leftEdge, topEdge, width, height);
            setAddTraceROI({leftEdge,topEdge,width,height,m_addTraceRoiSelectionActive});

        }
    }
    // Reset these Mouse events
    lastMouseClickEvent = nullptr;
    lastMouseReleaseEvent = nullptr;
}

void VideoDisplay::setROI(QList<int> roi)
{
    m_ROI = roi;

    roiChanged();
}

void VideoDisplay::setAddTraceROI(QList<int> roi)
{
    m_addTraceROI = roi;

    addTraceROIChanged();
}

void VideoDisplayRenderer::paint()
{
    //    qDebug() << "Painting!";
    if (!m_program) {
        initializeOpenGLFunctions();

        m_program = new QOpenGLShaderProgram();
        m_program->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/imageBasic.vert");
        m_program->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/imageSaturationScaling.frag");


        m_program->bindAttributeLocation("position", 0);
        m_program->bindAttributeLocation("texcoord", 1);
        m_program->link();

        m_texture = new QOpenGLTexture(QImage(":/img/MiniscopeLogo.png").rgbSwapped());
        m_texture->bind(0);



    }
//! [4] //! [5]

    m_texture->bind(0);

    m_program->bind();

    m_program->setUniformValue( "texture", 0 ); // <----- texture unit

    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);

    float position[] = {
        -1, -1,
        1, -1,
        -1, 1,
        1, 1

    };
    float texcoord[] = {
//        1, 1,
//        1, 0,
//        0, 1,
//        0, 0
        0, 1,
        1, 1,
        0, 0,
        1, 0

    };
    m_program->setAttributeArray(0, GL_FLOAT, position, 2);
    m_program->setAttributeArray(1, GL_FLOAT, texcoord, 2);
    m_program->setUniformValue("alpha", (float) m_alpha);
    m_program->setUniformValue("beta", (float) m_beta);
    m_program->setUniformValue("showSaturation", (float) m_showStaturation);

    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());

    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);
    m_program->release();

    // Not strictly needed for this example, but generally useful for when
    // mixing with raw OpenGL.
    m_window->resetOpenGLState();

    if (m_newFrame) {
//        qDebug() << "Set new texture QImage";

        m_texture->destroy();
        m_texture->create();
        m_texture->setData(m_displayFrame);
        m_newFrame = false;
    }

}
//! [5]

#include "tracedisplay.h"
#include "newquickview.h"

#include <QObject>
#include <QGuiApplication>
#include <QScreen>
#include <QDateTime>
#include <QImage>
#include <QString>
#include <QtMath>

#include <QtQuick/qquickwindow.h>
#include <QtGui/QOpenGLShaderProgram>
#include <QtGui/QOpenGLContext>
#include <QtGui/QOpenGLTexture>
#include <QtGui/QOpenGLFramebufferObject>

TraceDisplayBackend::TraceDisplayBackend(QObject *parent, QJsonObject ucTraceDisplay, qint64 softwareStartTime):
    QObject(parent),
    m_traceDisplay(nullptr),
    m_softwareStartTime(softwareStartTime)
{
    m_ucTraceDisplay = ucTraceDisplay;
    // Create Window
    createView();
}

void TraceDisplayBackend::createView()
{
//    QScreen *screen = QGuiApplication::primaryScreen();
//    QRect  screenGeometry = screen->geometry();
//    int height = screenGeometry.height();
//    int width = screenGeometry.width();

    qmlRegisterType<TraceDisplay>("TraceDisplay", 1, 0, "TraceDisplay");

    const QUrl url(QStringLiteral("qrc:/TraceDisplayWindow.qml"));
    view = new NewQuickView(url);

//    view->setWidth(400);
//    view->setHeight(height*0.9);
    view->setTitle("Trace Window");

    view->setWidth(m_ucTraceDisplay["windowWidth"].toInt(640));
    view->setHeight(m_ucTraceDisplay["windowHeight"].toInt(480));

    view->setX(m_ucTraceDisplay["windowX"].toInt(1));
    view->setY(m_ucTraceDisplay["windowY"].toInt(1));

#ifdef Q_OS_WINDOWS
    view->setFlags(Qt::Window | Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint);
#endif
    view->show();

    m_traceDisplay = view->rootObject()->findChild<TraceDisplay*>("traceDisplay");
    m_traceDisplay->setSoftwareStartTime(m_softwareStartTime);
}

void TraceDisplayBackend::addNewTrace(QString name, float color[3], float scale, QString units, bool sameOffset, QAtomicInt *displayBufNum, QAtomicInt *numDataInBuf, int bufSize, float *dataT, float *dataY)
{

    trace_t newTrace = trace_t(name, color, scale, units, sameOffset, displayBufNum, numDataInBuf, bufSize, dataT, dataY);
    m_traceDisplay->addNewTrace(newTrace);
}

void TraceDisplayBackend::close()
{
    view->close();
}

TraceDisplay::TraceDisplay()
    : m_t(0),
      m_renderer(nullptr),
      lastMouseClickEvent(nullptr),
      lastMouseReleaseEvent(nullptr),
      lastMouseMoveEvent(nullptr),
      m_softwareStartTime(0)
{

    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    connect(this, &QQuickItem::windowChanged, this, &TraceDisplay::handleWindowChanged);

    // Initially sets x labels
    QList<QVariant> tempLabels;
    for (int i=0;i < 5; i++) {
        tempLabels.append(QString::number(i*2 ,'f',1) + "s");
    }
    setXLabel(tempLabels);
}

void TraceDisplay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        lastMouseClickEvent = new QMouseEvent(*event);
    }
//    qDebug() << "Mouse Press" << event;
}

void TraceDisplay::mouseMoveEvent(QMouseEvent *event)
{
    // Drag event:
//    float deltaX, deltaY;
//    if (event->buttons() == Qt::LeftButton) {
//        if (lastMouseMoveEvent) {
//            deltaX = lastMouseMoveEvent->x() - event->x();
//            deltaY = lastMouseMoveEvent->y() - event->y();
//        }
//        else {
//            deltaX = lastMouseClickEvent->x() - event->x();
//            deltaY = lastMouseClickEvent->y() - event->y();
//        }
//        lastMouseMoveEvent = new QMouseEvent(*event);

//        m_renderer->updatePan(deltaX, deltaY);


//    }
//    qDebug() << "Mouse Move" << event;
}

void TraceDisplay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        lastMouseReleaseEvent = new QMouseEvent(*event);
        lastMouseMoveEvent = nullptr;
    }
//    qDebug() << "Mouse Release" << event;
}

void TraceDisplay::wheelEvent(QWheelEvent *event)
{
    int scrollAmount = event->angleDelta().y();
    m_renderer->handleWheelEvent(scrollAmount, m_keyMods);

    QList<QVariant> tempLabels;
    for (int i=0;i < 5; i++) {
        tempLabels.append(QString::number(i * m_renderer->windowSize/5,'f',1) + "s");
    }
    setXLabel(tempLabels);

    updateYSelectLabels();
    qDebug() << "Wheel" << event;
}

void TraceDisplay::hoverMoveEvent(QHoverEvent *event)
{
    //    qDebug() << "Hover" << event->pos();
}

void TraceDisplay::mouseDoubleClickEvent(QMouseEvent *event)
{

    if (event->button() == Qt::LeftButton) {
        m_renderer->doubleClickEvent(event->x(), event->y());
    }
    if (m_renderer->m_selectedTrace.isEmpty()) {
            // This is really poorly done!!!
            m_ySelectedLabel.clear();
            ySelectedLabelChanged();
    }
    else {
        updateYSelectLabels();
    }
}

void TraceDisplay::keyPressEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) {
        if (event->key() == Qt::Key_Control) {
            m_keyMods.append(Qt::Key_Control);
        }
        if (event->key() == Qt::Key_Shift) {
            m_keyMods.append(Qt::Key_Shift);
        }
    }

    qDebug() << "Press" << event;
}

void TraceDisplay::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) {
        if (event->key() == Qt::Key_Control) {
            if (event->key() == Qt::Key_Control) {
                m_keyMods.removeOne(Qt::Key_Control);
            }
            if (event->key() == Qt::Key_Shift) {
                m_keyMods.removeOne(Qt::Key_Shift);
            }
        }
    }
}


void TraceDisplay::setT(qreal t)
{
    if (t == m_t)
        return;
    m_t = t;
    emit tChanged();
    if (window())
        window()->update();
//    setXLabel({"0.0","1.0","2.0","3.0","4.0"});

}

void TraceDisplay::addNewTrace(trace_t newTrace)
{

    if(m_renderer) {
        m_renderer->addNewTrace(newTrace);
    }
    else {
        // renderer hasn't been created yet. Store trace info till it is.
        m_tempTraces.append(newTrace);
    }

    if (newTrace.sameOffsetAsPrevious)
        m_traceNames.last() = m_traceNames.last().toString() + "," + newTrace.name;
    else
        m_traceNames.append(newTrace.name);

    emit traceNamesChanged();
}

void TraceDisplay::updateYSelectLabels()
{
    m_ySelectedLabel.clear();
    if (!m_renderer->m_selectedTrace.isEmpty()) {
        float tempValue;
        trace_t trace = m_renderer->getTrace(m_renderer->m_selectedTrace.first());
        for (int i=-5; i< 6; i++){
            // Gets the vertical position from -1 to 1
            tempValue = -2.0f * (float)i/11.0f - trace.offset;

            // Scales value based on scaling
            tempValue = (tempValue / (m_renderer->getGlobalScaling() * trace.scale)) * (float)m_renderer->getNumOffets();
            m_ySelectedLabel.append(QString::number(tempValue,'f',2) + trace.units);
        }
        ySelectedLabelChanged();
    }
}

void TraceDisplay::handleWindowChanged(QQuickWindow *win)
{
    if (win) {
        connect(win, &QQuickWindow::beforeSynchronizing, this, &TraceDisplay::sync, Qt::DirectConnection);
        connect(win, &QQuickWindow::sceneGraphInvalidated, this, &TraceDisplay::cleanup, Qt::DirectConnection);
//! [1]
        // If we allow QML to do the clearing, they would clear what we paint
        // and nothing would show.
//! [3]
        win->setClearBeforeRendering(false);
    }
}

void TraceDisplay::sync()
{
    if (!m_renderer) {
        m_renderer = new TraceDisplayRenderer(nullptr, window()->size() * window()->devicePixelRatio(), m_softwareStartTime);
//        m_renderer->setShowSaturation(m_showSaturation);
//        m_renderer->setDisplayFrame(QImage("C:/Users/DBAharoni/Pictures/Miniscope/Logo/1.png"));
        connect(window(), &QQuickWindow::beforeRendering, m_renderer, &TraceDisplayRenderer::paint, Qt::DirectConnection);

        for (int i=0; i < m_tempTraces.length(); i++) {
            m_renderer->addNewTrace(m_tempTraces[i]);
        }
    }
    m_renderer->setViewportSize(window()->size() * window()->devicePixelRatio());
//    m_renderer->setT(m_t);
//    m_renderer->setDisplayFrame(m_displayFrame);
    m_renderer->setWindow(window());
}

void TraceDisplay::cleanup()
{
    if (m_renderer) {
        delete m_renderer;
        m_renderer = nullptr;
    }
}

TraceDisplayRenderer::TraceDisplayRenderer(QObject *parent, QSize displayWindowSize, qint64 softwareStartTime) :
    QObject(parent),
    m_softwareStartTime(softwareStartTime),
    m_program(nullptr),
    m_texture(nullptr),
    m_t(0),
    m_numTraces(2),
    m_fbo(nullptr),
    m_programTexture(nullptr),
    m_programGridV(nullptr),
    m_programGridH(nullptr),
    m_programSelectedGridH(nullptr),
    m_programMovingBar(nullptr),
    m_programTraces(nullptr)
{
    m_globalScale = 1.0f;
    m_numOffsets = 0;
    m_clearDisplayOnNextDraw = false;
    m_viewportSize = displayWindowSize;
    windowSize = 10; // in seconds. Consider having this defined in user config!


    pan[0] = 0.0f; pan[1] = 0.0f;
    scale[0] = 1.0f; scale[1] = 1.0f;
    magnify[0] = 1.0f; magnify[1] = 1.0f;

    startTime =  QDateTime().currentMSecsSinceEpoch(); //time software started up
    m_lastTimeDisplayed = startTime;

    initPrograms();
    float c[] = {0.5,0.7,1.0};
    bufNum = 0;
    numData[0] = 10;
    numData[1] = 10;
//    for (int a=0; a < 10; a++) {
//        dataY[0][a] = ((float)a)/10.0;
//        dataT[0][a] = ((float)a)/10;
//        dataY[1][a] = ((float)a)/10.0;
//        dataT[1][a] = ((float)a)/10;
//    }

//    traces.append(trace_t(c, 1, &bufNum, numData, 10, &dataT[0][0], &dataY[0][0]));
//    updateTraceOffsets();

}

TraceDisplayRenderer::~TraceDisplayRenderer()
{
    movingBarVBO.destroy();

    delete m_program;
    delete m_texture;

    delete m_programTexture;
    delete m_programGridV;
    delete m_programGridH;
    delete m_programSelectedGridH;
    delete m_programMovingBar;
    delete m_programTraces;
}

void TraceDisplayRenderer::initPrograms()
{

    initializeOpenGLFunctions();

    // Texture program
    m_programTexture = new QOpenGLShaderProgram();
    m_programTexture->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/texture.vert");
    m_programTexture->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/texture.frag");
    m_programTexture->link();
    initTextureProgram();

    // Vertical lines program
    m_programGridV = new QOpenGLShaderProgram();
    m_programGridV->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/grid.vert");
    m_programGridV->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag");
    m_programGridV->link();
    initGridV();

    // Horizontal lines program
    m_programGridH = new QOpenGLShaderProgram();
    m_programGridH->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/grid.vert");
    m_programGridH->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag");
    m_programGridH->link();
    initGridH();

    // Horizontal lines program for selected traces
    m_programSelectedGridH = new QOpenGLShaderProgram();
    m_programSelectedGridH->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/grid.vert");
    m_programSelectedGridH->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/grid.frag");
    m_programSelectedGridH->link();
    initSelectedGridH();

    // Moving bar program
    m_programMovingBar = new QOpenGLShaderProgram();
    m_programMovingBar->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/movingBar.vert");
    m_programMovingBar->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/movingBar.frag");
    m_programMovingBar->link();
    initMovingBar();

    // Moving bar program
    m_programTraces = new QOpenGLShaderProgram();
    m_programTraces->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex,":/shaders/trace.vert");
    m_programTraces->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment,":/shaders/trace.frag");
    m_programTraces->link();
    initTraces();


}

void TraceDisplayRenderer::addNewTrace(trace_t newTrace)
{
    newTrace.offset = 0.0f;
    if (newTrace.sameOffsetAsPrevious == false)
        m_numOffsets++;

    traces.append(newTrace);

    updateTraceOffsets();


    m_clearDisplayOnNextDraw = true;



}

void TraceDisplayRenderer::updateTraceOffsets()
{
    int numTraces = traces.length();
    int numOffsets = 0;
    for (int num=0; num < numTraces; num++) {
        if (traces[num].sameOffsetAsPrevious == false)
            numOffsets++;
    }

    float offsetStep = 2.0f / ((float)numOffsets + 1);

    int countOffset = -1;
    for (int num=0; num < numTraces; num++) {
        if (traces[num].sameOffsetAsPrevious == false) {
            countOffset++;
            traces[num].offset = 1 - ((countOffset + 1) * offsetStep);

        }
        else {
            traces[num].offset = 1 - ((countOffset + 1) * offsetStep);
        }

    }
}

void TraceDisplayRenderer::initTextureProgram()
{

    // Need to make global or us vertex buff for these to only be setup once!
//    float position[] = {-1.0f, -1.0f,
//                        1.0f, -1.0f,
//                        -1.0f, 1.0f,
//                        1.0f, 1.0f};
//    float textcoord[] = {0.0f, 0.0f,
//                        1.0f, 0.0f,
//                        0.0f, 1.0f,
//                        1.0f, 1.0f};
//    m_programTexture->setAttributeArray("a_position", GL_FLOAT, position,2);
//    m_programTexture->setAttributeArray("a_texcoord", GL_FLOAT, textcoord,2);

//    QOpenGLFramebufferObjectFormat format;
    m_fbo = new QOpenGLFramebufferObject(m_viewportSize);
    m_texture = new QOpenGLTexture(QOpenGLTexture::Target2D);

}

void TraceDisplayRenderer::drawRenderTexture()
{
//    m_texture->bind(m_fbo->takeTexture());
    GLuint textUnit = m_fbo->texture();
    glBindTexture(GL_TEXTURE_2D, textUnit);
//    m_texture = new QOpenGLTexture(QImage(":/img/MiniscopeLogo.png").rgbSwapped());
//    qDebug() << "Texture num" << m_fbo->texture();
//    m_texture->bind(m_fbo->takeTexture());
    m_programTexture->bind();
    m_programTexture->setUniformValue("u_texture1", 0);

    m_programTexture->enableAttributeArray("a_position");
    m_programTexture->enableAttributeArray("a_texcoord");

    float position[] = {-1.0f, -1.0f,
                        1.0f, -1.0f,
                        -1.0f, 1.0f,
                        1.0f, 1.0f};
    float textcoord[] = {0.0f, 0.0f,
                        1.0f, 0.0f,
                        0.0f, 1.0f,
                        1.0f, 1.0f};
    m_programTexture->setAttributeArray("a_position", GL_FLOAT, position, 2);
    m_programTexture->setAttributeArray("a_texcoord", GL_FLOAT, textcoord, 2);


    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_programTexture->disableAttributeArray("a_position");
    m_programTexture->disableAttributeArray("a_texcoord");
    m_programTexture->release();
//    m_texture->release();


}

void TraceDisplayRenderer::initGridV()
{
    // Holds position, color, index for vertical grid vertex
    QVector<float> gridVData;

    gridSpacingV = .25; // Used for shader to change color of lines
    gridVVOB.destroy();
    int numVGridLines = 21;
    float gridLineStep = 2.0 / ((float)numVGridLines - 1);

    float idx = 0;
    for (float x = -1; x <= 1; x+= (gridLineStep) ) {
        // Position
        gridVData.append({x, -1.0f,
                          idx});
        gridVData.append({x, 1.0f,
                          idx});
        idx += gridSpacingV;
    }

    gridVVOB.create();
    gridVVOB.bind();
    gridVVOB.allocate(&gridVData[0], gridVData.length() * sizeof(float));
    gridVVOB.release();
}
void TraceDisplayRenderer::updateGridV()
{

    m_programGridV->bind();

    float gridVColor[] = {0.9f, 0.9f, 0.9f};
    // ------ These will be moved to mouse and keyboard slots
    m_programGridV->setUniformValueArray("u_pan", pan, 1, 2);
    m_programGridV->setUniformValueArray("u_scale", scale, 1, 2);
    m_programGridV->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programGridV->setUniformValueArray("u_color", gridVColor, 1, 3);
    m_programGridV->setUniformValue("u_spacing", gridSpacingV);
    // ----------------------------------------------------------

    // WILL UPDATE VBO HERE IF NEEDED

    m_programGridV->release();
}

void TraceDisplayRenderer::drawGridV()
{
    m_programGridV->bind();
    gridVVOB.bind();

    m_programGridV->enableAttributeArray("a_position");
    m_programGridV->enableAttributeArray("a_index");

    m_programGridV->setAttributeBuffer("a_position",GL_FLOAT, 0,                2, 3 * sizeof(float));
    m_programGridV->setAttributeBuffer("a_index",   GL_FLOAT, 2 * sizeof(float), 1, 3 * sizeof(float));


    glLineWidth(3);
    glDrawArrays(GL_LINE_STRIP, 0, gridVVOB.size()/(3 * sizeof(float)));
//    qDebug() << "SIZE" << (gridVVOB.size());

//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

    m_programGridV->disableAttributeArray("a_position");
    m_programGridV->disableAttributeArray("a_index");

    gridVVOB.release();
    m_programGridV->release();

}

void TraceDisplayRenderer::initGridH()
{
    // Holds position, color, index for horizontal grid vertex
//    gridHVOB.destroy();
    QVector<float> gridHData;

    int numOffsets = 0;
    for (int num=0; num < traces.length(); num++) {
        if (traces[num].sameOffsetAsPrevious == false)
            numOffsets++;
    }

    int numHGridLines = numOffsets;

    float gridLineStep = 2 / ((float)numHGridLines + 1);

    float idx = 0;
    for (float y = -1; y <= 1; y+= (gridLineStep) ) {
        // Position
        if (idx > 0 && idx <= numHGridLines) {
            gridHData.append({-1.0f, y,
                              idx});
            gridHData.append({1.0f, y,
                              idx});
        }
        idx += 1;
    }


    if (!gridHVOB.isCreated())
        gridHVOB.create();
    gridHVOB.bind();
    gridHVOB.allocate(&gridHData[0], gridHData.length() * sizeof(float));

    qDebug() << numHGridLines << gridHData.length() << gridHVOB.size();

    gridHVOB.release();
}

void TraceDisplayRenderer::updateGridH()
{

    m_programGridH->bind();

    float gridHColor[] = {0.9f, 0.9f, 0.9f};

    // ------ These will be moved to mouse and keyboard slots
    m_programGridH->setUniformValueArray("u_pan", pan, 1, 2);
    m_programGridH->setUniformValueArray("u_scale", scale, 1, 2);
    m_programGridH->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programGridH->setUniformValueArray("u_color", gridHColor, 1, 3);
    m_programGridH->setUniformValue("u_spacing", 1.0f);
    // ----------------------------------------------------------

    // WILL UPDATE VBO HERE IF NEEDED

    m_programGridH->release();
}

void TraceDisplayRenderer::drawGridH()
{
    m_programGridH->bind();
    gridHVOB.bind();

    m_programGridH->enableAttributeArray("a_position");
//    m_programGridH->enableAttributeArray("a_color");
    m_programGridH->enableAttributeArray("a_index");

    m_programGridH->setAttributeBuffer("a_position",GL_FLOAT, 0,                2, 3 * sizeof(float));
    m_programGridH->setAttributeBuffer("a_index",   GL_FLOAT, 2 * sizeof(float), 1, 3 * sizeof(float));


    glLineWidth(3);
    glDrawArrays(GL_LINE_STRIP, 0, gridHVOB.size()/(3 * sizeof(float)));
//    qDebug() << "SIZE" << (gridVVOB.size());

//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

    m_programGridH->disableAttributeArray("a_position");
    m_programGridH->disableAttributeArray("a_index");

    gridHVOB.release();
    m_programGridH->release();

}

void TraceDisplayRenderer::initSelectedGridH()
{
    QVector<float> gridHData;

    int numHGridLines = 11;

    float gridLineStep = 2 / ((float)numHGridLines + 1);

    float idx = 0;
    for (float y = -1; y <= 1; y+= (gridLineStep) ) {
        // Position
        if (idx > 0 && idx <= numHGridLines) {
            gridHData.append({-1.0f, y,
                              idx});
            gridHData.append({1.0f, y,
                              idx});
        }
        idx += 1;
    }


    if (!selectedGridHVOB.isCreated())
        selectedGridHVOB.create();
    selectedGridHVOB.bind();
    selectedGridHVOB.allocate(&gridHData[0], gridHData.length() * sizeof(float));
    selectedGridHVOB.release();
}

void TraceDisplayRenderer::updateSelectedGridH()
{
    m_programSelectedGridH->bind();

    float gridHColor[] = {0.9f, 0.9f, 0.9f};

    // ------ These will be moved to mouse and keyboard slots
    m_programSelectedGridH->setUniformValueArray("u_pan", pan, 1, 2);
    m_programSelectedGridH->setUniformValueArray("u_scale", scale, 1, 2);
    m_programSelectedGridH->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programSelectedGridH->setUniformValueArray("u_color", gridHColor, 1, 3);
    m_programSelectedGridH->setUniformValue("u_spacing", 1.0f);
    // ----------------------------------------------------------

    // WILL UPDATE VBO HERE IF NEEDED

    m_programSelectedGridH->release();
}

void TraceDisplayRenderer::drawSelectedGridH()
{
    m_programSelectedGridH->bind();
    selectedGridHVOB.bind();

    m_programSelectedGridH->enableAttributeArray("a_position");
//    m_programGridH->enableAttributeArray("a_color");
    m_programSelectedGridH->enableAttributeArray("a_index");

    m_programSelectedGridH->setAttributeBuffer("a_position",GL_FLOAT, 0,                2, 3 * sizeof(float));
    m_programSelectedGridH->setAttributeBuffer("a_index",   GL_FLOAT, 2 * sizeof(float), 1, 3 * sizeof(float));


    glLineWidth(3);
    glDrawArrays(GL_LINE_STRIP, 0, selectedGridHVOB.size()/(3 * sizeof(float)));
//    qDebug() << "SIZE" << (gridVVOB.size());

//    glDrawArrays(GL_TRIANGLE_STRIP, 0, 3);

    m_programSelectedGridH->disableAttributeArray("a_position");
    m_programSelectedGridH->disableAttributeArray("a_index");

    selectedGridHVOB.release();
    m_programSelectedGridH->release();
}

void TraceDisplayRenderer::initMovingBar()
{
    movingBarVBO.create();
    movingBarVBO.bind();

    float movingBarData[] = {0.0f, -1.0f,
                             0.0f, 1.0f};
    movingBarVBO.allocate(movingBarData, 4 * sizeof(float));
    movingBarVBO.release();
}
void TraceDisplayRenderer::updateMovingBar()
{

    m_programMovingBar->bind();
    float movingBarColor[] = {0.9f, 0.9f, 0.9f};
    // ------ These will be moved to mouse and keyboard slots
    m_programMovingBar->setUniformValueArray("u_pan", pan, 1, 2);
    m_programMovingBar->setUniformValueArray("u_scale", scale, 1, 2);
    m_programMovingBar->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programMovingBar->setUniformValueArray("u_color", movingBarColor, 1, 3);

    m_programMovingBar->setUniformValue("u_windowSize", windowSize);
    // ----------------------------------------------------------
    m_programMovingBar->setUniformValue("u_time", (float)(currentTime - m_softwareStartTime)/1000.0f);
    m_programMovingBar->release();
}

void TraceDisplayRenderer::drawMovingBar()
{
    m_programMovingBar->bind();
    movingBarVBO.bind();

    m_programMovingBar->enableAttributeArray("a_position");
    m_programMovingBar->setAttributeBuffer("a_position",GL_FLOAT, 0, 2);

    glLineWidth(10);
    glDrawArrays(GL_LINE_STRIP, 0, 2);

    m_programMovingBar->disableAttributeArray("a_position");
    movingBarVBO.release();
    m_programMovingBar->release();
}

void TraceDisplayRenderer::initTraces()
{

}
void TraceDisplayRenderer::updateTraces()
{

    m_programTraces->bind();

    // ------ These will be moved to mouse and keyboard slots
    m_programTraces->setUniformValueArray("u_pan", pan, 1, 2);
    m_programTraces->setUniformValueArray("u_scale", scale, 1, 2);
    m_programTraces->setUniformValueArray("u_magnify", magnify, 1, 2);
    m_programTraces->setUniformValue("u_windowSize", windowSize);
    // ----------------------------------------------------------

    // WILL UPDATE VBO HERE IF NEEDED

    m_programTraces->release();
}

void TraceDisplayRenderer::drawTraces()
{
    QVector<float> tempTime;
    int arrayOffset;
    m_programTraces->bind();

    m_programTraces->setUniformValue("u_time", (float)(currentTime - m_softwareStartTime)/1000.0f);

    for (int num = 0; num < traces.length(); num++) {
//        if (num == 0) {
//            // Test trace
//            traces[num].numDataInBuffer[0] = 10;
//            traces[num].numDataInBuffer[1] = 10;
//            for (int a=0; a < 10; a++) {
//                dataY[0][a] = (float)a/5.0 - 1.0;
//                dataY[1][a] = (float)a/5.0 - 1.0;
//                dataT[0][a] = currentTime - (float)a/5.0 * 1000;
//                dataT[1][a] = currentTime - (float)a/5.0 * 1000;
//            }
//        }

        if (traces[num].numDataInBuffer[!*traces[num].displayBufferNumber] > 1) {

            // Switches the display buffer number and reset data count
            if (*traces[num].displayBufferNumber == 0) {

                traces[num].dataT[traces[num].bufferSize] = traces[num].dataT[traces[num].numDataInBuffer[0] - 1];
                traces[num].dataY[traces[num].bufferSize] = traces[num].dataY[traces[num].numDataInBuffer[0] - 1];
                traces[num].numDataInBuffer[0] = 1;
                *traces[num].displayBufferNumber = 1;
                arrayOffset = traces[num].bufferSize;
            }
            else {

                traces[num].dataT[0] = traces[num].dataT[traces[num].bufferSize + traces[num].numDataInBuffer[1] - 1];
                traces[num].dataY[0] = traces[num].dataY[traces[num].bufferSize + traces[num].numDataInBuffer[1] - 1];
                traces[num].numDataInBuffer[1] = 1;
                *traces[num].displayBufferNumber = 0;
                arrayOffset = 0;
            }


            // Update uniforms for specific trace
//            qDebug() << num << traces[num].color[0] << traces[num].color[1]  << traces[num].color[2];
            m_programTraces->setUniformValueArray("u_color", traces[num].color, 1, 3);
            m_programTraces->setUniformValue("u_scaleTrace", traces[num].scale /((float)m_numOffsets) * m_globalScale);
            m_programTraces->setUniformValue("u_offset", traces[num].offset);
            if (m_selectedTrace.isEmpty()) {
                m_programTraces->setUniformValue("u_traceSelected", 0.0f);
            }
            else {
                if (m_selectedTrace.contains(num))
                    m_programTraces->setUniformValue("u_traceSelected", 1.0f);
                else
                    m_programTraces->setUniformValue("u_traceSelected", -1.0f);
            }


            // Set the data for the trace
            m_programTraces->enableAttributeArray("a_dataTime");
            m_programTraces->enableAttributeArray("a_dataY");

            m_programTraces->setAttributeArray("a_dataTime", GL_FLOAT, &traces[num].dataT[arrayOffset], 1);
            m_programTraces->setAttributeArray("a_dataY", GL_FLOAT, &traces[num].dataY[arrayOffset], 1);


            glLineWidth(5);
            glDrawArrays(GL_LINE_STRIP, 0, traces[num].numDataInBuffer[*traces[num].displayBufferNumber]);


            m_programTraces->disableAttributeArray("a_dataTime");
            m_programTraces->disableAttributeArray("a_dataY");
        }
    }

    m_programTraces->release();

}

void TraceDisplayRenderer::updatePan(float deltaX, float deltaY)
{
    float dX, dY;

    dX = 2.0 * deltaX / (float)m_window->width();
    dY = 2.0 * deltaY / (float)m_window->height();

    pan[0] = pan[0] - dX/scale[0];
    pan[1] = pan[1] + dY/scale[1];
    m_clearDisplayOnNextDraw = true;


}

void TraceDisplayRenderer::handleWheelEvent(int scrollAmount, QVector<Qt::Key> keyMods)
{
    if (keyMods.contains(Qt::Key_Control)) {
        // Adjust scale
        if (m_selectedTrace.isEmpty()) {
            // Adjust all scales
//            if (m_globalScale > 0.1f && m_globalScale < 10.0f)
                m_globalScale = m_globalScale * (1.0f + (float)scrollAmount/1000.0f);
        }
        else {
            // Adjust specific scales
            for (int i=0; i < m_selectedTrace.length(); i++) {
                traces[m_selectedTrace[i]].scale = traces[m_selectedTrace[i]].scale * (1.0f + (float)scrollAmount/1000.0f);
            }
        }
    }
    else {
        windowSize += (float)scrollAmount/10.0f;
        if (windowSize < 1)
            windowSize = 1;
        if (windowSize > 120)
            windowSize = 120;
        m_clearDisplayOnNextDraw = true;
    }

//    initGridV();
}

void TraceDisplayRenderer::paint()
{
    currentTime =  QDateTime().currentMSecsSinceEpoch();

    if (m_clearDisplayOnNextDraw)
        initGridH();
//    m_texture->bind();
    m_fbo->bind();
    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());
    glDisable(GL_DEPTH_TEST);

    int pastScrollBarPos = m_viewportSize.width() * std::fmod((m_lastTimeDisplayed - m_softwareStartTime)/1000.0f, windowSize) / windowSize;
    int clearWidth = ((currentTime - m_lastTimeDisplayed)/1000.0) / windowSize * m_viewportSize.width();

    if (m_clearDisplayOnNextDraw == true) {
        m_clearDisplayOnNextDraw = false;
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    else {
        glEnable(GL_SCISSOR_TEST);
        glScissor(pastScrollBarPos, 0, 0.05f * m_viewportSize.width(), 10000);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glScissor(0, 0, 10000, 10000);
    }

//    scrollBarPos = self.physical_size[0] * (self.programLine["u_time"]%self.windowSize)/self.windowSize
//    clearWidth = 0.5/self.windowSize * self.physical_size[0]  # first number is in seconds
//    gloo.set_scissor(scrollBarPos-clearWidth, 0.0, clearWidth, 10000)
//    gloo.clear(color=(0.0, 0.0, 0.0, 0.0))
//    gloo.set_scissor(0.0, 0.0, 10000, 10000)

//    glClearColor(0, 0, 0, 1);
//    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draws traces
    updateTraces();
    drawTraces();

    m_fbo->release();
//    QImage fboImage(m_fbo->toImage());
//    fboImage.save("fboImage.png");
//    m_texture->release();
//    m_texture = new QOpenGLTexture(fboImage);

    glViewport(0, 0, m_viewportSize.width(), m_viewportSize.height());

    glDisable(GL_DEPTH_TEST);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw rendered texture
    drawRenderTexture();

    // Draw vertical grid lines
    updateGridV();
    drawGridV();

    // Draw vertical grid lines
    if (m_selectedTrace.isEmpty()) {
        updateGridH();
        drawGridH();
    }
    else {
        // Draw horizontal lines with values for selected trace(s)
        updateSelectedGridH();
        drawSelectedGridH();
    }

    // Draw moving vertical bar
    updateMovingBar();
    drawMovingBar();

    m_lastTimeDisplayed = currentTime;



//    // Not strictly needed for this example, but generally useful for when
//    // mixing with raw OpenGL.
    m_window->resetOpenGLState();

}

void TraceDisplayRenderer::doubleClickEvent(int x, int y)
{
    if (m_selectedTrace.isEmpty()){
        // currently no trace selected

        // Find what trace was closest to click
        float tempY = (-2.0f * ((float)y) /(float)m_window->height()) + 1.0f;
        float offsetStep = 2.0f / ((float)m_numOffsets + 1);
        for (int i=0; i < traces.length(); i++) {
            if (qFabs(tempY - traces[i].offset) <= offsetStep/2)
                m_selectedTrace.append(i);
        }
    }
    else {
        m_selectedTrace.clear();

    }
}

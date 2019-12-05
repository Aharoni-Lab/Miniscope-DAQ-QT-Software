#include "miniscope.h"

#include <QQuickView>

Miniscope::Miniscope(QObject *parent) : QObject(parent)
{
    createView();
}

void Miniscope::createView()
{
    QQuickView *view = new QQuickView;
    const QUrl url(QStringLiteral("qrc:/miniscope.qml"));
    view->setSource(url);
    view->show();
}

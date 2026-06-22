#ifndef NEWQUICKVIEW_H
#define NEWQUICKVIEW_H

#include <QObject>
#include <QQuickView>

class NewQuickView: public QQuickView {
    // Class makes it so we can handle window closing events
    // TODO: if recording don't release camera
    Q_OBJECT
public:
    NewQuickView(QUrl url):
        QQuickView(url) {}
public:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::Close) {
            // your code here
            qDebug() << "CLOSEING!!";
            emit closing();
        }
        return QQuickView::event(event);
    }
signals:
    void closing();

public slots:

};

#endif // NEWQUICKVIEW_H

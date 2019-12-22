#ifndef CONTROLPANEL_H
#define CONTROLPANEL_H

#include "newquickview.h"

#include <QObject>
#include <QQuickItem>

class ControlPanel : public QObject
{
    Q_OBJECT
public:
    explicit ControlPanel(QObject *parent = nullptr);
    void createView();
    void connectSnS();

public slots:
    void receiveMessage(QString msg);

signals:
//    void newMessage(QString msg);

private:
    NewQuickView *view;
    QObject *rootObject;
    QQuickItem  *messageTextArea;


};

#endif // CONTROLPANEL_H

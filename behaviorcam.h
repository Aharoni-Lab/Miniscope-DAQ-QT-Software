#ifndef BEHAVIORCAM_H
#define BEHAVIORCAM_H

#include <QObject>

class BehaviorCam : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorCam(QObject *parent = nullptr);

signals:

public slots:
};

#endif // BEHAVIORCAM_H

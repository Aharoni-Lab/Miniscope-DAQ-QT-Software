#ifndef BEHAVIORTRACKER_H
#define BEHAVIORTRACKER_H

#include <QObject>

class BehaviorTracker : public QObject
{
    Q_OBJECT
public:
    explicit BehaviorTracker(QObject *parent = nullptr);

signals:

public slots:
};

#endif // BEHAVIORTRACKER_H

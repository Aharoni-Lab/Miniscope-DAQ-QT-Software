#ifndef DATASAVER_H
#define DATASAVER_H

#include <QObject>

class dataSaver : public QObject
{
    Q_OBJECT
public:
    explicit dataSaver(QObject *parent = nullptr);

signals:

public slots:
};

#endif // DATASAVER_H

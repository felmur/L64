#ifndef TIMEBASE_H
#define TIMEBASE_H

#include <QThread>
#include <QApplication>

class timebase : public QThread
{
    Q_OBJECT
public:
    explicit timebase(QObject *parent = nullptr);
    ~timebase();

signals:
    void frameReady(); // Segnale inviato a Qt ogni 20.26 ms per fare l'update video

protected:
    void run() override;
};

#endif // TIMEBASE_H


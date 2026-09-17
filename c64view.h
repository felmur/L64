#ifndef C64VIEW_H
#define C64VIEW_H

#include <QGraphicsView>
#include <QObject>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QResizeEvent>
#include <vector>

using namespace std;

class C64View : public QGraphicsView
{
    Q_OBJECT
public:
    explicit C64View(QWidget *parent=nullptr);
    vector<uint32_t> pixelBuffer; // Il tuo emulatore scriverà direttamente qui

protected:
    // Gestisce il ridimensionamento della finestra mantenendo le proporzioni corrette
    void resizeEvent(QResizeEvent *event) override;

public slots:
    void updateFrame();

private:
    QGraphicsScene *scene;
    QGraphicsPixmapItem *pixmapItem;
};

#endif // C64VIEW_H

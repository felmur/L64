// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "c64view.h"
#include <QImage>
#include <QPixmap>

const int C64_WIDTH = 504;
const int C64_HEIGHT = 302;

C64View::C64View(QWidget *parent): QGraphicsView(parent)
{
    // 1. Inizializza la scena con le dimensioni native del C64
    scene = new QGraphicsScene(0, 0, C64_WIDTH, C64_HEIGHT, this);
    this->setScene(scene);

    // 2. Disabilita le barre di scorrimento
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 3. FORZA i pixel nitidi (Pixel Art) durante il ridimensionamento della finestra
    this->setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    this->setRenderHint(QPainter::Antialiasing, false);
    this->setRenderHint(QPainter::SmoothPixmapTransform, false); // Cruciale per evitare sfocature

    // 4. Inizializza il pixel buffer (simula la memoria video generata dal VIC-II)
    // Usiamo il formato RGB32 (0xAARRGGBB)
    pixelBuffer.resize(C64_WIDTH * C64_HEIGHT, 0xFF000000); // Schermo nero iniziale

    // 5. Crea l'item grafico per la bitmap e aggiungilo alla scena
    pixmapItem = new QGraphicsPixmapItem();
    scene->addItem(pixmapItem);

}

void C64View::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    // Ridimensiona la scena per riempire la finestra mantenendo l'Aspect Ratio originale
    this->fitInView(scene->sceneRect(), Qt::KeepAspectRatio);
}

void C64View::updateFrame()
{
    // 7. Converte il pixel buffer in una QImage senza copiare i dati (zero-copy per performance)
    QImage frameImage(
        reinterpret_cast<uchar*>(pixelBuffer.data()),
        C64_WIDTH,
        C64_HEIGHT,
        C64_WIDTH * sizeof(uint32_t), // Bytes per riga (stride)
        QImage::Format_RGB32
        );

    // 8. Aggiorna l'elemento grafico nella scena
    pixmapItem->setPixmap(QPixmap::fromImage(frameImage));
}


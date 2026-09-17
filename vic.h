// *****************************************************
// L64 - Yet Another C64 Emulator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#ifndef VIC_H
#define VIC_H

#include <QObject>
#include <QList>
#include <vector>
#include <cstdint>

using namespace std;

extern uint8_t ioarea[4096];

class VIC : public QObject {
    Q_OBJECT
public:
    explicit VIC(QObject *parent = nullptr);

    void Reset();
    void Poke(uint16_t address, uint8_t value);
    uint8_t Peek(uint16_t address);

    // Chiamato dall'timebase thread a ogni ciclo (1 ciclo = 8 pixel orizzontali in PAL)
    void Update(int cycles);

    // Ritorna il puntatore al buffer video 504x302 per QGraphicsView
    uint32_t* GetPixelBuffer() { return pixelBuffer.data(); }

    uint8_t vic_bank = 0;
    void set_vic_bank(uint8_t bank);

    // Valore linea a cui generare l'interrupt
    uint16_t iline = 0;
    // Flag da settare se interrupt raster globale è abilitato da $D01A
    bool interrupt = false;

private:
    // Helper per estrarre i colori globali dai registri del VIC
    uint8_t GetBGColor(int index) { return ioarea[0x21 + (index & 0x03)] & 0x0F; } // $D021 - $D024

    // Dimensioni totali dello schermo emulato
    static const int SCREEN_WIDTH = 504;
    static const int SCREEN_HEIGHT = 302;

    // Buffer video lineare RGB32
    vector<uint32_t> pixelBuffer;

    // Posizione attuale del raggio catodico
    int rasterLine;   // Da 0 a 311 (PAL)
    int rasterCycle;  // Da 0 a 62 (63 cicli per riga in PAL)

    // PEPTOPAL default palette, vedi /usr/share/vice/C64/pepto-palold.vpl dell'emulatore VICE
    QList<uint32_t> color1 = {
        0xff000000, 0xffffffff, 0xff58291d, 0xff91c6d5,
        0xff915ca8, 0xff588d43, 0xff352879, 0xffb8c76f,
        0xff916f43, 0xff433900, 0xff9a6759, 0xff353535,
        0xff747474, 0xff9ad284, 0xff7466be, 0xffb8b8b8
    };

    QList<uint32_t> palette = color1;

    // Funzioni di rendering interno
    void RenderRasterLine();
    void RenderSpritesForLine(uint8_t spritePixels[SCREEN_WIDTH], uint8_t spriteColors[SCREEN_WIDTH], bool spritePriority[SCREEN_WIDTH]);

    // Helper per leggere la memoria dal punto di vista del VIC-II (Banche CIA2 + ROM Caratteri)
    uint8_t VICRead(uint16_t vic_addr);

protected:
    uint8_t sprite_sprite_collision; // Registro $D01E
    uint8_t sprite_bg_collision;     // Registro $D01F
};

#endif // VIC_H

// *****************************************************
// L64 - Yet Another C64 Emulator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "vic.h"
#include "common.h"
#include <iostream>

#include "c64view.h"
extern C64View *c64view;

#include "cpu.h"
extern CPU *cpu;

extern uint8_t memory[65536];
extern uint8_t ioarea[4096];
extern uint8_t chars[4096];

VIC::VIC(QObject *parent) : QObject(parent), rasterLine(0), rasterCycle(0) {
    c64view->pixelBuffer.resize(SCREEN_WIDTH * SCREEN_HEIGHT, 0xFF000000);
    Reset();
}

void VIC::Reset() {
    fill(c64view->pixelBuffer.begin(), c64view->pixelBuffer.end(), 0xFF000000);
    rasterLine = 0;
    rasterCycle = 0;
    sprite_sprite_collision = 0;
    sprite_bg_collision = 0;
}

void VIC::Poke(uint16_t address, uint8_t value) {
    uint8_t reg = address & 0x3F;
    ioarea[reg] = value; // Salva il valore nell'area di I/O globale

    switch(reg) {
    case 0x11: // Registro $D011: Il bit 7 è il nono bit di iline
        if (value & (1 << 7)) {
            iline |= (1 << 8);
        } else {
            iline &= 0x00FF;
        }
        break;

    case 0x12: // Registro $D012: Primi 8 bit di iline
        iline = (iline & 0x0100) | value;
        break;

    case 0x19:
        // Registro $D019 (Interrupt Flag): Scrivere 1 sui bit dei flag LI AZZERA (Acknowledge)
        // Il bit 7 si spegne automaticamente se non ci sono altri flag attivi.
        ioarea[0x19] &= ~(value & 0x0F);
        if ((ioarea[0x19] & 0x0F) == 0) {
            ioarea[0x19] &= ~0x80; // Spegne Any VIC IRQ
        }
        break;

    case 0x1A: // Registro $D01A (Interrupt Mask): Il bit 0 abilita l'interrupt raster
        if (value & 1) {
            interrupt = true;
        } else {
            interrupt = false;
        }
        break;
    }
}

uint8_t VIC::Peek(uint16_t address) {
    uint8_t reg = address & 0x3F;

    if (reg == 0x1E) {
        uint8_t tmp = sprite_sprite_collision;
        sprite_sprite_collision = 0; // Si azzera automaticamente alla lettura
        ioarea[0x1E] = 0;
        return tmp;
    }
    if (reg == 0x1F) {
        uint8_t tmp = sprite_bg_collision;
        sprite_bg_collision = 0; // Si azzera automaticamente alla lettura
        ioarea[0x1F] = 0;
        return tmp;
    }

    return ioarea[reg];
}

uint8_t VIC::VICRead(uint16_t vic_addr) {
    // Calcola l'indirizzo assoluto unendo la banca del VIC selezionata dalla CIA2 (0-3)
    uint32_t real_addr = (vic_bank * 16384) + (vic_addr & 0x3FFF);

    // Nelle banche 0 e 2, l'intervallo relativo $1000-$1FFF punta sempre alla ROM dei caratteri
    if (vic_bank == 0 || vic_bank == 2) {
        if (vic_addr >= 0x1000 && vic_addr <= 0x1FFF) {
            return chars[vic_addr - 0x1000];
        }
    }

    return memory[real_addr & 0xFFFF];
}

void VIC::set_vic_bank(uint8_t bank) {
    vic_bank = bank;
}

void VIC::Update(int cycles) {
    // Il thread principale della timebase chiama questa funzione passandogli i cicli eseguiti
    for (int i = 0; i < cycles; ++i) {

        // Nel C64 PAL reale, lo scatto del Raster IRQ e l'allineamento dei registri
        // avvengono esattamente al ciclo 12 di ogni linea raster
        if (rasterCycle == 12) {
            // Aggiorna in tempo reale i registri stabili letti dalla CPU tramite ioarea
            ioarea[0x12] = rasterLine & 0xFF;
            ioarea[0x11] = (ioarea[0x11] & 0x7F) | ((rasterLine & 0x100) >> 1);

            // GESTIONE HARDWARE RASTER INTERRUPT
            if (interrupt && (iline == rasterLine)) {
                ioarea[0x19] |= 0x01; // Imposta il Bit 0 (Raster IRQ Flag)
                ioarea[0x19] |= 0x80; // Imposta il Bit 7 (ANY VIC IRQ) per notificare il Kernal
                cpu->IRQ(false);       // Innesca fisicamente la linea IRQ della CPU
            }
        }

        // Eseguiamo il rendering dell'intera riga al ciclo 0 quando il raggio attraversa la sezione visiva
        if (rasterLine < SCREEN_HEIGHT && rasterCycle == 0) {
            RenderRasterLine();
        }

        rasterCycle++;
        if (rasterCycle >= 63) { // 63 cicli totali per riga nel sistema PAL
            rasterCycle = 0;
            rasterLine++;
            if (rasterLine >= 312) { // 312 righe totali per frame PAL
                rasterLine = 0;
            }
        }
    }
}


void VIC::RenderSpritesForLine(uint8_t spritePixels[SCREEN_WIDTH], uint8_t spriteColors[SCREEN_WIDTH], bool spritePriority[SCREEN_WIDTH]) {
    // Cache veloci dei registri globali degli sprite letti direttamente da ioarea[]
    uint8_t d015_val = ioarea[0x15]; // Abilitazione sprite
    uint8_t d017_val = ioarea[0x17]; // Espansione Y
    uint8_t d01d_val = ioarea[0x1D]; // Espansione X
    uint8_t d01c_val = ioarea[0x1C]; // Multicolor sprite
    uint8_t d01b_val = ioarea[0x1B]; // Priorità sprite-sfondo
    uint8_t d010_val = ioarea[0x10]; // Nono bit coordinata X

    uint8_t mcColor1 = ioarea[0x25] & 0x0F;
    uint8_t mcColor2 = ioarea[0x26] & 0x0F;

    uint16_t screenBase = (ioarea[0x18] & 0xF0) << 6;
    uint16_t pointerAddr = screenBase + 0x3F8;

    for (int s = 7; s >= 0; --s) {
        uint8_t mask = (1 << s);

        // 1. Verifica se lo sprite è attivo
        if (!(d015_val & mask)) continue;

        int realSpriteY = ioarea[0x01 + (s * 2)] + 1;
        bool expansionY = (d017_val & mask);
        int spriteHeight = expansionY ? 42 : 21;

        if (rasterLine < realSpriteY || rasterLine >= (realSpriteY + spriteHeight)) continue;

        int rowInSprite = rasterLine - realSpriteY;
        if (expansionY) {
            rowInSprite /= 2;
        }

        uint8_t pointer = VICRead(pointerAddr + s);
        uint16_t dataDataAddr = pointer * 64;

        int spriteX = ioarea[0x00 + (s * 2)];
        if (d010_val & mask) {
            spriteX |= 0x100;
        }

        int screenXStart = spriteX + (92 - 24);

        bool expansionX = (d01d_val & mask);
        bool multicolor = (d01c_val & mask);
        uint8_t spriteColor = ioarea[0x27 + s] & 0x0F;

        uint32_t spriteRowData = (VICRead(dataDataAddr + (rowInSprite * 3)) << 16) |
                                 (VICRead(dataDataAddr + (rowInSprite * 3) + 1) << 8) |
                                 (VICRead(dataDataAddr + (rowInSprite * 3) + 2));

        int currentPixelX = screenXStart;

        if (!multicolor) {
            // --- MODALITÀ HIRES STANDARD ---
            for (int bit = 23; bit >= 0; --bit) {
                bool pixelOn = (spriteRowData >> bit) & 0x01;
                int widthSteps = expansionX ? 2 : 1;

                for (int w = 0; w < widthSteps; ++w) {
                    if (currentPixelX >= 0 && currentPixelX < SCREEN_WIDTH) {
                        if (pixelOn) {
                            if (spritePixels[currentPixelX] != 0) {
                                // COLLISIONE SPRITE-SPRITE DETECTED!
                                sprite_sprite_collision |= (spritePixels[currentPixelX] | mask);
                                ioarea[0x1E] = sprite_sprite_collision; // Specchia nel registro I/O

                                // Gestione Interrupt hardware da collisione Sprite-Sprite
                                ioarea[0x19] |= 0x04; // Imposta il bit 2 (Sprite-Sprite Collision Flag)
                                if (ioarea[0x1A] & 0x04) { // Se abilitato in $D01A
                                    ioarea[0x19] |= 0x80;  // Alza Any VIC IRQ
                                    cpu->IRQ(false);       // Genera l'IRQ fisico
                                }
                            }
                            spritePixels[currentPixelX] |= mask;
                            spriteColors[currentPixelX] = spriteColor;
                            spritePriority[currentPixelX] = (d01b_val & mask);
                        }
                    }
                    currentPixelX++;
                }
            }
        } else {
            // --- MODALITÀ MULTICOLOR ---
            for (int bit = 22; bit >= 0; bit -= 2) {
                uint8_t pixelBits = (spriteRowData >> bit) & 0x03;
                int widthSteps = expansionX ? 4 : 2;

                uint8_t finalColor = 0;
                bool drawPixel = false;

                switch (pixelBits) {
                case 0x01: // %01: Multicolor 1 globale ($D025)
                    finalColor = mcColor1;
                    drawPixel = true;
                    break;
                case 0x02: // %10: Colore specifico dello sprite ($D027)
                    finalColor = spriteColor;
                    drawPixel = true;
                    break;
                case 0x03: // %11: Multicolor 2 globale ($D026)
                    finalColor = mcColor2;
                    drawPixel = true;
                    break;
                case 0x00: // %00: Trasparente
                default:
                    drawPixel = false;
                    break;
                }

                for (int w = 0; w < widthSteps; ++w) {
                    if (currentPixelX >= 0 && currentPixelX < SCREEN_WIDTH) {
                        if (drawPixel) {
                            if (spritePixels[currentPixelX] != 0) {
                                sprite_sprite_collision |= (spritePixels[currentPixelX] | mask);
                                ioarea[0x1E] = sprite_sprite_collision;
                                ioarea[0x19] |= 0x04;
                                if (ioarea[0x1A] & 0x04) {
                                    ioarea[0x19] |= 0x80;
                                    cpu->IRQ(false);
                                }
                            }
                            spritePixels[currentPixelX] |= mask;
                            spriteColors[currentPixelX] = finalColor;
                            spritePriority[currentPixelX] = (d01b_val & mask);
                        }
                    }
                    currentPixelX++;
                }
            }
        }
    }
}
void VIC::RenderRasterLine() {
    // --- 1. FETCH PREVENTIVO DEI REGISTRI (Eseguito una sola volta per riga!) ---
    uint8_t d011_val = ioarea[0x11];
    uint8_t d016_val = ioarea[0x16];
    uint8_t d018_val = ioarea[0x18];

    int yScroll = d011_val & 0x07;
    bool rowMode24 = !(d011_val & 0x08);
    bool colMode38 = !(d016_val & 0x08);
    int xScroll = d016_val & 0x07;

    bool ecm = d011_val & 0x40;
    bool bmm = d011_val & 0x20;
    bool mcm = d016_val & 0x10;

    uint16_t screenBase = (d018_val & 0xF0) << 6;
    uint16_t charBase = (d018_val & 0x0E) << 10;
    uint16_t bitmapBase = (d018_val & 0x08) << 10;

    int activeBorderTop = rowMode24 ? 55 : 51;
    int activeBorderBottom = rowMode24 ? 247 : 251;
    int activeBorderLeft = colMode38 ? 100 : 92;
    int activeBorderRight = colMode38 ? 404 : 412;

    uint32_t borderColor = palette[ioarea[0x20] & 0x0F];

    // Buffer grafici locali della linea
    uint8_t spritePixels[SCREEN_WIDTH] = {0};
    uint8_t spriteColors[SCREEN_WIDTH] = {0};
    bool spritePriority[SCREEN_WIDTH] = {0};

    uint8_t gfxColors[320] = {0};
    uint8_t gfxIsSourceBG0[320] = {0};

    // Pre-popoliamo con lo sfondo globale ($D021)
    fill(gfxColors, gfxColors + 320, ioarea[0x21] & 0x0F);
    fill(gfxIsSourceBG0, gfxIsSourceBG0 + 320, 1);

    RenderSpritesForLine(spritePixels, spriteColors, spritePriority);

    // --- 2. LOOP DI RENDERING AREA ATTIVA (40 Caratteri / 320 Pixel) ---
    if (rasterLine >= activeBorderTop && rasterLine < activeBorderBottom) {
        int yInGraphics = (rasterLine - 51) + 3 - yScroll;

        if (yInGraphics >= 0 && yInGraphics < 200) {
            int charY = yInGraphics / 8;
            int pixelY = yInGraphics % 8;

            for (int charX = 0; charX < 40; ++charX) {
                uint16_t cellOffset = (charY * 40) + charX;

                uint8_t screenByte = VICRead(screenBase + cellOffset);
                // Accesso diretto parallelo alla Color RAM senza chiamate virtuali esterne
                //uint8_t colorByte = memory[0xD800 + cellOffset] & 0x0F;
                uint8_t colorByte = ioarea[0x0800 + cellOffset] & 0x0F;

                uint8_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;
                uint8_t dataByte = 0;

                // --- PIPELINE LOGICA DELLE 5 MODALITÀ GRAFICHE ---
                if (!bmm && !ecm && !mcm) {
                    // 1. Standard Text Mode
                    dataByte = VICRead(charBase + (screenByte * 8) + pixelY);
                    c0 = ioarea[0x21] & 0x0F;
                    c1 = colorByte;

                    for (int b = 0; b < 8; ++b) {
                        int pixelScreenX = (charX * 8) + b;
                        if (pixelScreenX >= 0 && pixelScreenX < 320) {
                            bool bit = (dataByte >> (7 - b)) & 0x01;
                            gfxColors[pixelScreenX] = bit ? c1 : c0;
                            gfxIsSourceBG0[pixelScreenX] = !bit;
                        }
                    }
                }
                else if (!bmm && !ecm && mcm) {
                    // 2. Multicolor Text Mode
                    dataByte = VICRead(charBase + (screenByte * 8) + pixelY);
                    if ((colorByte & 0x08) == 0) { // Hires simulato su bit 3 azzerato
                        c0 = ioarea[0x21] & 0x0F;
                        c1 = colorByte & 0x07;
                        for (int b = 0; b < 8; ++b) {
                            int pixelScreenX = (charX * 8) + b;
                            if (pixelScreenX >= 0 && pixelScreenX < 320) {
                                bool bit = (dataByte >> (7 - b)) & 0x01;
                                gfxColors[pixelScreenX] = bit ? c1 : c0;
                                gfxIsSourceBG0[pixelScreenX] = !bit;
                            }
                        }
                    } else { // Multicolor nativo reale
                        c0 = ioarea[0x21] & 0x0F;
                        c1 = ioarea[0x22] & 0x0F;
                        c2 = ioarea[0x23] & 0x0F;
                        c3 = colorByte & 0x07;

                        for (int pair = 0; pair < 4; ++pair) {
                            uint8_t bits = (dataByte >> (6 - pair * 2)) & 0x03;
                            uint8_t finalColor = (bits == 0) ? c0 : (bits == 1) ? c1 : (bits == 2) ? c2 : c3;
                            for (int w = 0; w < 2; ++w) {
                                int pixelScreenX = (charX * 8) + (pair * 2) + w;
                                if (pixelScreenX >= 0 && pixelScreenX < 320) {
                                    gfxColors[pixelScreenX] = finalColor;
                                    gfxIsSourceBG0[pixelScreenX] = (bits == 0);
                                }
                            }
                        }
                    }
                }
                else if (!bmm && ecm && !mcm) {
                    // 3. Extended Background Text Mode (ECM)
                    uint8_t bgIndex = (screenByte >> 6) & 0x03;
                    uint8_t realChar = screenByte & 0x3F;
                    dataByte = VICRead(charBase + (realChar * 8) + pixelY);
                    c0 = ioarea[0x21 + bgIndex] & 0x0F;
                    c1 = colorByte;

                    for (int b = 0; b < 8; ++b) {
                        int pixelScreenX = (charX * 8) + b;
                        if (pixelScreenX >= 0 && pixelScreenX < 320) {
                            bool bit = (dataByte >> (7 - b)) & 0x01;
                            gfxColors[pixelScreenX] = bit ? c1 : c0;
                            gfxIsSourceBG0[pixelScreenX] = !bit;
                        }
                    }
                }
                else if (bmm && !ecm && !mcm) {
                    // 4. Bitmap Hires Mode
                    dataByte = VICRead(bitmapBase + (charY * 320) + (charX * 8) + pixelY);
                    c0 = screenByte & 0x0F;
                    c1 = (screenByte >> 4) & 0x0F;

                    for (int b = 0; b < 8; ++b) {
                        int pixelScreenX = (charX * 8) + b;
                        if (pixelScreenX >= 0 && pixelScreenX < 320) {
                            bool bit = (dataByte >> (7 - b)) & 0x01;
                            gfxColors[pixelScreenX] = bit ? c1 : c0;
                            gfxIsSourceBG0[pixelScreenX] = !bit;
                        }
                    }
                }
                else if (bmm && !ecm && mcm) {
                    // 5. Bitmap Multicolor Mode
                    dataByte = VICRead(bitmapBase + (charY * 320) + (charX * 8) + pixelY);
                    c0 = ioarea[0x21] & 0x0F;
                    c1 = (screenByte >> 4) & 0x0F;
                    c2 = screenByte & 0x0F;
                    c3 = colorByte;

                    for (int pair = 0; pair < 4; ++pair) {
                        uint8_t bits = (dataByte >> (6 - pair * 2)) & 0x03;
                        uint8_t finalColor = (bits == 0) ? c0 : (bits == 1) ? c1 : (bits == 2) ? c2 : c3;
                        for (int w = 0; w < 2; ++w) {
                            int pixelScreenX = (charX * 8) + (pair * 2) + w;
                            if (pixelScreenX >= 0 && pixelScreenX < 320) {
                                gfxColors[pixelScreenX] = finalColor;
                                gfxIsSourceBG0[pixelScreenX] = (bits == 0);
                            }
                        }
                    }
                }
            }
        }
    }

    // --- 3. COMPOSITING FINALE CON SCORRIMENTO ORIZZONTALE E REASTR IRQ ---
    int pixelIndex = rasterLine * SCREEN_WIDTH;

    for (int x = 0; x < SCREEN_WIDTH; ++x) {
        // Disegno del bordo nero o colorato esterno ($D020)
        if (rasterLine < activeBorderTop || rasterLine >= activeBorderBottom || x < activeBorderLeft || x >= activeBorderRight) {
            c64view->pixelBuffer[pixelIndex + x] = borderColor;
        }
        else {
            // Calcolo geometrico dello X-Scroll reale: fa scivolare l'array grafico dietro il bordo
            int gfxX = (x - 92) - xScroll;

            uint32_t finalPixelColor = borderColor;
            bool isBG0 = true;

            if (gfxX >= 0 && gfxX < 320) {
                finalPixelColor = palette[gfxColors[gfxX]];
                isBG0 = gfxIsSourceBG0[gfxX];
            } else {
                finalPixelColor = palette[ioarea[0x21] & 0x0F]; // Sfondo $D021 di riempimento tolleranza
                isBG0 = true;
            }

            // INNESTO COLLISIONI SPRITE-SFONDO HARDWARE E TRASPARENZE
            if (spritePixels[x] != 0) {
                // La collisione scatta solo se lo sprite tocca pixel attivi non trasparenti dello sfondo
                if (!isBG0) {
                    sprite_bg_collision |= spritePixels[x];
                    ioarea[0x1F] = sprite_bg_collision; // Specchia nel registro di lettura

                    // Trigger dell'interrupt hardware da collisione Sprite-Sfondo
                    ioarea[0x19] |= 0x02; // Alza bit 1 (Sprite-Background Collision Flag)
                    if (ioarea[0x1A] & 0x02) {
                        ioarea[0x19] |= 0x80; // Any VIC IRQ
                        cpu->IRQ(false);
                    }
                }

                // Disegno finale dello sprite se ha priorità o se lo sfondo è trasparente
                if (!spritePriority[x] || isBG0) {
                    finalPixelColor = palette[spriteColors[x]];
                }
            }

            c64view->pixelBuffer[pixelIndex + x] = finalPixelColor;
        }
    }
}

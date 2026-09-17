// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "keyboard.h"
#include "common.h"
#include "cia.h"


extern uint8_t memory[65536];
extern CIA1 *cia1;

// Riferimenti esterni alle variabili dichiarate nella CIA
extern uint8_t joystick2_state;
extern bool joystick_emulation_enabled;

Keyboard::Keyboard(QObject *parent)
    : QObject{parent}
{
    // see Keyboard matrix at https://www.c64-wiki.com/wiki/Keyboard
    // row PA0
    keymap[Qt::Key_Backspace]=0;
    keymap[Qt::Key_Return]=1;
    keymap[Qt::Key_Right]=2;
    keymap[Qt::Key_F7]=3;
    keymap[Qt::Key_F8]=3;
    keymap[Qt::Key_F1]=4;
    keymap[Qt::Key_F2]=4;
    keymap[Qt::Key_F3]=5;
    keymap[Qt::Key_F4]=5;
    keymap[Qt::Key_F5]=6;
    keymap[Qt::Key_F6]=6;
    keymap[Qt::Key_Down]=7;
    // row PA1
    keymap[Qt::Key_3]=8;
    keymap[Qt::Key_W]=9;
    keymap[Qt::Key_A]=10;
    keymap[Qt::Key_4]=11;
    keymap[Qt::Key_Z]=12;
    keymap[Qt::Key_S]=13;
    keymap[Qt::Key_E]=14;
    keymap[Qt::Key_Shift]=15;
    // row PA2
    keymap[Qt::Key_5]=16;
    keymap[Qt::Key_R]=17;
    keymap[Qt::Key_D]=18;
    keymap[Qt::Key_6]=19;
    keymap[Qt::Key_C]=20;
    keymap[Qt::Key_F]=21;
    keymap[Qt::Key_T]=22;
    keymap[Qt::Key_X]=23;
    // row PA3
    keymap[Qt::Key_7]=24;
    keymap[Qt::Key_Y]=25;
    keymap[Qt::Key_G]=26;
    keymap[Qt::Key_8]=27;
    keymap[Qt::Key_B]=28;
    keymap[Qt::Key_H]=29;
    keymap[Qt::Key_U]=30;
    keymap[Qt::Key_V]=31;
    // row PA4
    keymap[Qt::Key_9]=32;
    keymap[Qt::Key_I]=33;
    keymap[Qt::Key_J]=34;
    keymap[Qt::Key_0]=35;
    keymap[Qt::Key_M]=36;
    keymap[Qt::Key_K]=37;
    keymap[Qt::Key_O]=38;
    keymap[Qt::Key_N]=39;
    // row PA5
    keymap[Qt::Key_Plus]=40;
    keymap[Qt::Key_P]=41;
    keymap[Qt::Key_L]=42;
    keymap[Qt::Key_Minus]=43;
    keymap[Qt::Key_Period]=44;
    keymap[Qt::Key_Colon]=45;
    keymap[Qt::Key_At]=46;
    keymap[Qt::Key_Comma]=47;
    // row PA6
    keymap[Qt::Key_sterling]=48;
    keymap[Qt::Key_Asterisk]=49;
    keymap[Qt::Key_Semicolon]=50;
    keymap[Qt::Key_Home]=51;
    keymap[Qt::Key_Shift]=52;
    keymap[Qt::Key_Equal]=53;
    keymap[Qt::Key_AsciiCircum]=54;
    keymap[Qt::Key_Slash]=55;
    // row PA7
    keymap[Qt::Key_1]=56;
    keymap[Qt::Key_Less]=58;
    keymap[Qt::Key_Control]=58;
    keymap[Qt::Key_2]=59;
    keymap[Qt::Key_Space]=60;
    keymap[Qt::Key_Alt]=61;         // C= key
    keymap[Qt::Key_Super_L]=61;     // C= key
    keymap[Qt::Key_Q]=62;
    keymap[Qt::Key_Tab]=63;
    keymap[Qt::Key_Escape]=63;      // Run-Stop

    // tasti richiamabili con SHIFT
    keymap[Qt::Key_Exclam]=56;      // tasto shift-1 = punto esclamativo
    keymap[Qt::Key_QuoteDbl]=59;    // tasto shift-2 = virgolette
    keymap[Qt::Key_NumberSign]=8;   // tasto shift-3 = cancelletto
    keymap[Qt::Key_Dollar]=11;      // tasto shift-4 = dollaro
    keymap[Qt::Key_Percent]=16;     // tasto shift-5 = percento
    keymap[Qt::Key_Ampersand]=19;   // tasto shift-6 = e-commerciale
    //keymap[Qt::Key_Apostrophe]=24;  // tasto shift-7 = apostrofo
    keymap[Qt::Key_ParenLeft]=27;   // tasto shift-8 = par. tonda aperta
    keymap[Qt::Key_ParenRight]=32;  // tasto shift-9 = par. tonda chiusa

    //capsLock = checkCapsLock();
    //cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), capsLock);
    //clipboard = QGuiApplication::clipboard();
}

bool shiftpressed = false;
uint8_t last_key = 255;

void Keyboard::keyPress(int key)
{

    uint8_t t=Qt2C64(key);

    if (shiftpressed) {
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
    }

    switch (key) {
    case Qt::Key_Shift:
        shiftpressed = true;
        break;

    case Qt::Key_AltGr:
    case Qt::Key_At:
        t = 255;
        break;
    case Qt::Key_Ograve:
        t = Qt2C64(Qt::Key_At);
        break;
    case Qt::Key_Asterisk:
    case Qt::Key_sterling:
    case Qt::Key_Equal:
    case Qt::Key_Colon:
    case Qt::Key_Semicolon:
    case Qt::Key_AsciiCircum:
    case Qt::Key_Slash:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        break;
    case Qt::Key_Agrave:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_3);
        break;
    case Qt::Key_Apostrophe:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_7);
        break;
    case Qt::Key_Question:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_Slash);
        break;
    case Qt::Key_Down:
        if (joystick_emulation_enabled) joystick2_state &= ~(1 << 1); // Bit 1 a 0 (DOWN)
        break;
    case Qt::Key_Right:
        if (joystick_emulation_enabled) joystick2_state &= ~(1 << 3); // Bit 3 a 0 (RIGHT)
        break;
    case Qt::Key_Left:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_Right);
        if (joystick_emulation_enabled) joystick2_state &= ~(1 << 2); // Bit 2 a 0 (LEFT)
        break;
    case Qt::Key_Up:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_Down);
        if (joystick_emulation_enabled) joystick2_state &= ~(1 << 0); // Bit 0 a 0 (UP)
        break;
    case Qt::Key_Space:
        if (joystick_emulation_enabled) joystick2_state &= ~(1 << 4); // Bit 4 a 0 (FIRE)
        break;
    case Qt::Key_BracketLeft:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_Colon);
        break;
    case Qt::Key_BracketRight:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_Semicolon);
        break;
    case Qt::Key_Insert:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_Backspace);
        break;
    case Qt::Key_Greater:
        t = Qt2C64(Qt::Key_Period);
        break;
    case Qt::Key_Less:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        t = Qt2C64(Qt::Key_Comma);
        break;
    case Qt::Key_F2:
    case Qt::Key_F4:
    case Qt::Key_F6:
    case Qt::Key_F8:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), true);
        break;
    }

    if (t != 255){
        if (key != Qt::Key_Shift) last_key = t;
        cia1->KeyCallback(cia1, t, true);
    }

}

void Keyboard::keyRelease(int key)
{
    uint8_t t=Qt2C64(key);

    // simulazione di rilascio dell'ultimo tasto premuto assieme a shift
    if (key == Qt::Key_Shift && last_key != 255) {
        cia1->KeyCallback(cia1, last_key, false);
        last_key = 255;
    }

    switch (key) {
    case Qt::Key_Ograve:
        t = Qt2C64(Qt::Key_At);
        break;

    case Qt::Key_Agrave:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_3);
        break;

    case Qt::Key_Shift:
        shiftpressed = false;
        break;
    case Qt::Key_Apostrophe:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_7);
        break;
    case Qt::Key_Question:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_Slash);
        break;
    case Qt::Key_Right:
        if (joystick_emulation_enabled) joystick2_state |= (1 << 3);
        break;
    case Qt::Key_Down:
        if (joystick_emulation_enabled) joystick2_state |= (1 << 1);
        break;
    case Qt::Key_Space:
        if (joystick_emulation_enabled) joystick2_state |= (1 << 4);
        break;

    case Qt::Key_Left:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_Right);
        if (joystick_emulation_enabled) joystick2_state |= (1 << 2);
        break;
    case Qt::Key_Up:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_Down);
        if (joystick_emulation_enabled) joystick2_state |= (1 << 0);
        break;
    case Qt::Key_BracketLeft:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_Colon);
        break;
    case Qt::Key_BracketRight:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_Semicolon);
        break;
    case Qt::Key_Insert:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_Backspace);
        break;
    case Qt::Key_Greater:
        t = Qt2C64(Qt::Key_Period);
        break;
    case Qt::Key_Less:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        t = Qt2C64(Qt::Key_Comma);
        break;
    case Qt::Key_F2:
    case Qt::Key_F4:
    case Qt::Key_F6:
    case Qt::Key_F8:
        cia1->KeyCallback(cia1, Qt2C64(Qt::Key_Shift), false);
        break;
    }

    if (t != 255){
        cia1->KeyCallback(cia1, t, false);
    }
}

uint8_t Keyboard::Qt2C64(int key)
{
    map<uint32_t, uint8_t>::iterator i = keymap.find(key);
    if (i==keymap.end()) return 255;
    return i->second;
}

void Keyboard::writeToC64()
{
    memory[198]=0;
    //char *p = keyBuffer.toLatin1().data();
    if (keyIterator && !memory[204]) {  // se c'è da scrivere e se il cursore è disponibile per scrivere
        uint16_t offset = keyBuffer.length()-keyIterator;
        uint8_t c = keyBuffer[offset]; //*(p+offset);
        if (c==10) c=13;
        memory[631]=c;
        memory[198]=1;
        keyIterator--;
    }
}

void Keyboard::pasteClipBoard()
{

}

void Keyboard::setClipBoard(QByteArray text)
{
    (void) text;
}

void Keyboard::setKeyBuffer(QByteArray keyb)
{
    (void) keyb;
}

bool Keyboard::checkCapsLock()
{
    return false;
}

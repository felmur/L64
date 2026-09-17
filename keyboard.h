#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <QObject>
#include <QClipboard>
using namespace std;

class Keyboard : public QObject
{
    Q_OBJECT
public:
    explicit Keyboard(QObject *parent = nullptr);
    void keyPress(int key);
    void keyRelease(int key);
    void writeToC64();
    void pasteClipBoard();
    void setClipBoard(QByteArray text);
    uint16_t keyIterator=0;
    QByteArray keyBuffer;
    void setKeyBuffer(QByteArray keyb);

signals:

private:
    map<uint32_t, uint8_t> keymap;
    uint8_t Qt2C64(int key);
    bool checkCapsLock();
    bool capsLock = false;
    QClipboard *clipboard=nullptr;


signals:
};

#endif // KEYBOARD_H

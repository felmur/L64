#ifndef CIA_H
#define CIA_H

#include <QObject>

class CIA : public QObject
{
    Q_OBJECT
public:
    explicit CIA(QObject *parent = nullptr);
    ~CIA();

    bool PokeCIA(uint16_t address, uint8_t value);
    bool PeekCIA(uint16_t address, uint8_t& value);
    void ResetCIA();
    void UpdateCIA(int cycles);
    virtual void triggerInterrupt(bool active);

protected:
    // Timer A value
    uint16_t timer_a;

    // Timer B value
    uint16_t timer_b;

    // Timer A current value
    uint16_t timerval_a;

    // Timer B current value
    uint16_t timerval_b;

    // Interrupt control register
    //uint8_t icr;
    // Sostituiamo il singolo 'icr' con due registri distinti:
    uint8_t icr_mask; // Memorizza quali IRQ sono abilitati (settati tramite Poke)
    uint8_t icr_data; // Memorizza quali eventi sono accaduti (letti tramite Peek)

    // Control register A
    uint8_t cra;

    // Control register B
    uint8_t crb;

    // IRQ's that have occured
    uint8_t read_irqs;

    uint8_t ddrA = 0x00; // Registro di direzione della Porta A ($DD02)

signals:
};

// CIA1 è un oggetto CIA con alcune funzioni aggiunte
class CIA1 : public CIA
{
    Q_OBJECT
public:
    CIA1(QObject *parent=nullptr);
    void Reset();
    void Poke(uint16_t address, uint8_t value);
    uint8_t Peek(uint16_t address);
    void Update(int cycles);
    // Callback function for the host I/O
    static void KeyCallback(void* ptr, int key, bool pressed);

protected:
    // Currently selected keyboard columns
    uint8_t kbd_cur_columns;

    // Keyboard matrix
    uint8_t kbd_matrix[8];

};

// CIA2 è un oggetto CIA con alcune funzioni aggiunte
class CIA2 : public CIA
{
    Q_OBJECT
public:
    CIA2(QObject *parent=nullptr);
    void Reset();
    void Poke(uint16_t address, uint8_t value);
    uint8_t Peek(uint16_t address);
    void Update(int cycles);
    void triggerInterrupt(bool active) override;

private:
    bool last_atn_state = true; // Stato precedente di ATN per rilevare il fronte

public:

};


#endif // CIA_H

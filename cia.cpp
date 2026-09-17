// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "cia.h"

#include "common.h"
#include <iostream>
using namespace std;

#include "cpu.h"
extern CPU *cpu;
extern uint8_t ioarea[4096];

#include "vic.h"
extern VIC *vic;


// ---------------------------------------------------------------------------
// FUNZIONI classe CIA
// ---------------------------------------------------------------------------

CIA1* pCIA1=nullptr;

CIA::CIA(QObject *parent) : QObject(parent)
{

}

CIA::~CIA()
{
    cout << "[CIA] Deleting CIAs thread..." << endl;
}

void CIA::ResetCIA()
{
    timer_a = 0xffff;
    timer_b = 0xffff;
    timerval_a = 0xffff;
    timerval_b = 0xffff;
    icr_mask = 0x00;
    icr_data = 0x00;
    cra = 0x00;
    crb = 0x00;
}

bool CIA::PokeCIA(uint16_t address, uint8_t value)
{
    switch(address & 0x0f) {
    case 0x2: // Port A data direction register.
        ddrA = value;
        break;
    case 0x4: // timer a lo-b yte
        timer_a = (timer_a & 0xff00) | value;
        break;
    case 0x5: // timer a hi-byte
        timer_a = (timer_a & 0x00ff) | ((uint16_t)value << 8);
        if ((cra & 0x01) == 0) {
            timerval_a = timer_a;
        }
        break;
    case 0x6: // timer b lo-byte
        timer_b = (timer_b & 0xff00) | value;
        break;
    case 0x7: // timer b hi-byte
        timer_b = (timer_b & 0x00ff) | ((uint16_t)value << 8);
        if ((crb & 0x01) == 0) {
            timerval_b = timer_b;
        }
        break;
    case 0x1d: // Nota: se usi l'offset da 0xD000, 0x1d (ovvero $D00D) o 0x0d in base a come calcoli address
    case 0x0d: // interrupt control register (Scrittura Maschera)
        if ((value & 0x80) == 0) {
            icr_mask &= ~(value & 0x1f);
        } else {
            icr_mask |= (value & 0x1f);
        }

        // Se c'è un interrupt pendente che viene abilitato ora, attiva l'IRQ
        if ((icr_data & icr_mask) & 0x1f) {
            icr_data |= 0x80;
            cpu->IRQ(false);   // ATTIVA INTERRUPT (Linea Bassa)
        }
       break;
    case 0x0e: // control register a
        cra = value;
        if (value & 0x10) {
            timerval_a = timer_a;
            cra &= ~0x10;
        }
        break;
    case 0x0f: // control register b
        crb = value;
        if (value & 0x10) {
            timerval_b = timer_b;
            crb &= ~0x10;
        }
        break;
    default:
        return false;
    }
    return true;
}

bool CIA::PeekCIA(uint16_t address, uint8_t& value)
{
    switch(address & 0x0f) {
    case 0x4:
        value = (uint8_t)(timerval_a & 0xff);
        return true;
    case 0x5:
        value = (uint8_t)(timerval_a >> 8);
        return true;
    case 0x6:
        value = (uint8_t)(timerval_b & 0xff);
        return true;
    case 0x7:
        value = (uint8_t)(timerval_b >> 8);
        return true;
    case 0x0d: // Lettura Registro Stato Interruzioni (ICR)
        // value = icr_data;

        // // REGOLA HARDWARE: La lettura azzera i flag e disattiva la linea IRQ
        // icr_data = 0x00;
        // triggerInterrupt(true); // DISATTIVA INTERRUPT (Linea Alta)
        // return true;

        // 1. Isola i bit degli eventi reali accaduti (da 0 a 4)
        uint8_t status = icr_data & 0x7F;

        // 2. LOGICA HARDWARE REALE: Imposta il Bit 7 a 1 solo se un evento attivo
        // coincide con una maschera di abilitazione attiva in icr_mask
        if ((icr_data & icr_mask & 0x7F) != 0) {
            status |= 0x80; // Alza il bit 7
        }

        // 3. RIEMPI IL PARAMETRO: Passiamo il valore corretto calcolato alla CPU del C64
        value = status;

        // 4. REGOLA HARDWARE: La lettura azzera i flag interni
        icr_data = 0x00;

        // Disattiva la linea di interrupt hardware (NMI/IRQ a seconda della CIA) verso la CPU principale
        triggerInterrupt(true);

        // Indica che l'indirizzo $0D è stato intercettato e gestito
        return true;
    }
    return false;
}

void CIA::UpdateCIA(int cycles)
{
    // ---- GESTIONE TIMER A ----
    if (cra & 0x01) {
        if ((cra & 0x20) == 0) {
            int new_val = (int)timerval_a - cycles;

            if (new_val <= 0) {
                // --- UNDERFLOW DEL TIMER A ---
                icr_data |= 0x01;

                if (icr_mask & 0x01) {
                    icr_data |= 0x80;
                    triggerInterrupt(false);   // ATTIVA INTERRUPT (Linea Bassa)
                }

                if (cra & 0x08) { // One-Shot
                    cra &= ~0x01;
                    timerval_a = timer_a;
                } else { // Continuo
                    timerval_a = timer_a + new_val;
                }
            } else {
                timerval_a = (uint16_t)new_val;
            }
        }
    }

    // ---- GESTIONE TIMER B ----
    if (crb & 0x01) {
        if ((crb & 0x60) == 0) { // Conta i cicli di sistema Phi2
            int new_val = (int)timerval_b - cycles;
            if (new_val <= 0) {
                icr_data |= 0x02; // Bit 1 = Underflow Timer B
                if (icr_mask & 0x02) {
                    icr_data |= 0x80;
                    triggerInterrupt(false); // ATTIVA INTERRUPT (Linea Bassa)
                }
                if (crb & 0x08) {
                    crb &= ~0x01;
                    timerval_b = timer_b;
                } else {
                    timerval_b = timer_b + new_val;
                }
            } else {
                timerval_b = (uint16_t)new_val;
            }
        }
    }
}

void CIA::triggerInterrupt(bool active)
{
    cpu->IRQ(active);
}


// ---------------------------------------------------------------------------
// FUNZIONI classe CIA1
// ---------------------------------------------------------------------------

// Stato del joystick virtuale (0xFF = tutto rilasciato, logica invertita)
uint8_t joystick2_state = 0xFF;

// Flag controllato dal menù Qt per attivare/disattivare l'emulazione
bool joystick_emulation_enabled = false;

CIA1::CIA1(QObject *parent) : CIA(parent)
{
    pCIA1 = this;
}

void CIA1::Reset()
{
    kbd_cur_columns = 0;
    memset(kbd_matrix, 0xff, sizeof(kbd_matrix));
    ResetCIA();
}

void CIA1::Poke(uint16_t address, uint8_t value)
{
    // Try base CIA
    if (PokeCIA(address, value)) return;

    switch(address & 0xff) {
    case 0x0: // write: keyboard column values for keyboard scan
        kbd_cur_columns = value;
        break;
    default:
        printf("CIA1::Poke(): unhandled addr=0x%x val=%x\n", address, value);
        break;
    }

}

uint8_t CIA1::Peek(uint16_t address)
{
    uint8_t value = ioarea[address-0xD000];
    if ((PeekCIA(address, value))) return value;

    switch(address & 0xff) {
    case 0x00:
        if (joystick_emulation_enabled) {
            // Unisce lo stato della tastiera standard con i movimenti del joystick virtuale
            return value & joystick2_state;
        }
        break;
    case 0x1: // read: keyboard row values for keyboard scan
        value = 0xff;
        for (int i = 0; i < 8; i++)
            if ((kbd_cur_columns & (1 << i)) == 0) value &= kbd_matrix[i];
        return value;

    default:
        printf("CIA1::Peek(): unhandled addr=0x%x, returned ioarea value %d\n", address,value);
        break;
    }
    return value;
}

void CIA1::Update(int cycles)
{
    UpdateCIA(cycles);
}

void CIA1::KeyCallback(void *ptr, int key, bool pressed)
{
    if (pressed) pCIA1->kbd_matrix[key / 8] &= ~(1 << (key & 7));
    else pCIA1->kbd_matrix[key / 8] |=  (1 << (key & 7));
}


// ---------------------------------------------------------------------------
// FUNZIONI classe CIA2
// ---------------------------------------------------------------------------

CIA2::CIA2(QObject *parent) : CIA(parent)
{

}

void CIA2::Reset()
{
    ResetCIA();
}

void CIA2::Poke(uint16_t address, uint8_t value)
{
    // 1. Inoltra la scrittura alla classe base per processare i registri interni (compresi i timer e ddrA)
    PokeCIA(address, value);

    // 2. Isola il registro reale da 0 a 15 (gestisce gli specchiamenti hardware)
    uint8_t reg = address & 0x0F;

    switch(reg) {
    case 0x00: // --- DATA PORT A ($DD00) ---
        // Il VIC-II legge i bit 0 e 1 per determinare quale banco da 16KB guardare.
        // Poiché la logica del C64 inverte i banchi rispetto al valore binario, la formula "3 - valore" è corretta.
        vic->set_vic_bank(3 - (value & 0x03));
        break;

    default:
        break;
    }
}

uint8_t CIA2::Peek(uint16_t address)
{
    // Recupera il valore memorizzato nell'area di I/O ($D000-$DFFF)
    uint8_t value = ioarea[address - 0xD000];

    // Chiediamo alla classe base CIA di riempire 'value' con i dati corretti del registro (Timer, ICR, ecc.)
    if (PeekCIA(address, value)) {
        // Se il registro letto è proprio lo 0x00 ($DD00 / Porta A)
        if ((address & 0x0F) == 0x00) {
            // Forziamo i bit 6 e 7 a 0. Questo indica alla CPU del C64 che
            // sul bus IEC non ci sono segnali estranei o cortocircuiti latenti.
            value &= ~0xC0; // Pulisce i bit 6 e 7 (li mette a 0)
        }
        return value;
    }

    return value;
}

void CIA2::Update(int cycles)
{
    UpdateCIA(cycles);
}

void CIA2::triggerInterrupt(bool active)
{
     cpu->NMI(active);
}


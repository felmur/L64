// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************


#include "timebase.h"
#include <chrono>
#include <thread>

#include <cpu.h>
extern CPU *cpu;
#include <cia.h>
extern CIA1 *cia1;
extern CIA2 *cia2;
#include <vic.h>
extern VIC *vic;
#include "drive.h"
extern Drive *drive;

#include <iostream>
using namespace std;

timebase::timebase(QObject *parent) : QThread(parent)
{
}

timebase::~timebase()
{
    cout << "Deleting timebase thread..." << endl;
}

void timebase::run() {
    // Frequenze hardware PAL esatte
    const double C64_PAL_CLOCK = 985248.0;

    auto startTime = chrono::high_resolution_clock::now();

    double totalCyclesRequired = 0.0;
    double cyclesExecuted = 0.0;
    double cyclesExecutedInFrame = 0.0;

    while (!isInterruptionRequested()) {
        auto currentTime = chrono::high_resolution_clock::now();
        chrono::duration<double> elapsedTime = currentTime - startTime;
        totalCyclesRequired = elapsedTime.count() * C64_PAL_CLOCK;

        // *********************************************************************************
        // EMULAZIONE HLD DRIVE
        // AGGANCIO TRAPPOLE HLE DRIVE
        // *********************************************************************************
        uint16_t current_pc = cpu->GetPC();
        uint8_t device = cpu->MemoryRead(0x00BA);

        switch(current_pc){
        // LOAD di un file
        case  0xF4A5:
            // Verifichiamo se il dispositivo selezionato in RAM alla locazione $BA è l'8
            if (device != 8 && device != 1){
                cpu->SetX(0x05);
                cpu->SetPC(0xA43A);
                continue;
            }
            if (device == 8) {
                //cout << "trappola load" << endl;
                drive->handle_trap_load(); // Esegue il caricamento istantaneo in C++ e sposta il PC!
            }
            break;

        // SAVE di un file
        case 0xF5ED:
            if (device != 8 && device != 1){
                cpu->SetX(0x05);
                cpu->SetPC(0xA43A);
                continue;
            }
            if (device == 8) {
                //cout << "trappola save" << endl;
                drive->handle_trap_save();
            }
            break;

        // OPEN, tipo OPEN 15,8,15,"S0:pippo"
        case 0xF3D5:
            if (device != 8 && device != 1){
                cpu->SetX(0x05);
                cpu->SetPC(0xA43A);
                continue;
            }
            if (device == 8) {
                //cout << "trappola open" << endl;
                drive->handle_trap_open();
            }
            break;

        // CHKIN ($F211): Chiamata prima di leggere i dati (es. INPUT#15 o GET#15). Imposta il canale di input attivo.
        case 0xF211:
        case 0xFFC6: // Vettore pubblico nella Jump Table (CHKIN)
            //cout << "trappola chkin" << endl;
            drive->handle_trap_chkin();
            break;

        // CHKOUT (0xF250) Prende il byte presente nel registro A e lo appende nel buffer del canale di scrittura attivo.
        case 0xF250: // Indirizzo interno Kernal
        case 0xFFC9: // Vettore pubblico Jump Table
            drive->handle_trap_chkout();
            break;

        // GETIN ($F13E) / CHRIN ($F157): Chiamate per estrarre effettivamente i byte della stringa di errore (es. 20, READ ERROR, 18, 01).
        case 0xF13E: // GETIN interno
        case 0xF157: // CHRIN interno
        case 0xFFE4: // GETIN pubblico nella Jump Table
        case 0xFFCF: // CHRIN pubblico nella Jump Table
            //cout << "trappola getin_chrin" << endl;
            drive->handle_trap_chrin_getin();
            break;

        // TRAP CHROUT (Scrittura byte)
        case 0xF1CA: // Indirizzo interno
        case 0xFFD2: // Vettore pubblico nella Jump Table (CHROUT)
            //cout << "trappola chRout" << endl;
            drive->handle_trap_chrout();
            break;

        // CLOSE ($F291): Chiamata per chiudere il canale (CLOSE 15).
        case 0xF291:
            //cout << "trappola close" << endl;
            drive->handle_trap_close();
            break;

        // CLRCHN ($F333): Ripristina i canali standard (Tastiera e Schermo), chiudendo le sessioni attive di lettura/scrittura sui file.
        case 0xF333:
        case 0xFFCC: // Vettore pubblico nella Jump Table (CLRCHN)
            //cout << "trappola clrchn" << endl;
            drive->handle_trap_clrchn();
            break;
        }

        // *********************************************************************************
        // FINE EMULAZIONE HLE DRIVE
        // *********************************************************************************


        // Il ciclo gira per rimettersi in pari con il tempo reale
        while (cyclesExecuted < totalCyclesRequired) {

            // --- AVANZAMENTO CORE C64 (1 Singolo Ciclo Hardware) ---
            cpu->unlocktick();
            int c64Cycles = 1; // Forza stabilmente a 1 come da tua architettura hardware

            // Aggiorna i chip del C64 di un singolo ciclo
            cia1->UpdateCIA(c64Cycles);
            cia2->UpdateCIA(c64Cycles);
            vic->Update(c64Cycles);

            cyclesExecuted += c64Cycles;
            cyclesExecutedInFrame += c64Cycles;

            // GESTIONE FRAME REFRESH VIDEO (Ogni 19656 tick PAL)
            if (cyclesExecutedInFrame >= 19656.0) {
                emit frameReady();
                cyclesExecutedInFrame -= 19656.0;
            }
        }

        // Freno di sicurezza anti-overclock
        if (cyclesExecuted > totalCyclesRequired + 50) {
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
    cout << "[timebase] timebase thread stopped" << endl;
}

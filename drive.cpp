// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "drive.h"
#include "common.h"
#include "drive.h"

#include <iostream>
#include <sstream>
#include <string>
#include <iostream>

#include <cpu.h>
extern CPU *cpu;
#include "d64image.h"
extern D64Image *d64;
extern DriveStatusChannel drive15;

// Array di stato globale per i canali attivi
OpenFileChannel open_channels[10];
// Traccia quale LFN è attualmente attivo dopo una CHKIN o CHKOUT
int active_input_channel_idx;
int active_output_channel_idx;

Drive::Drive(QObject *parent) : QObject{parent}
{
    cout << "C64 HLEDrive activated" << endl;
}

#include <QFile>
void Drive::handle_trap_load() {
    uint8_t device = cpu->MemoryRead(0x00BA);
    if (device != 8) return; // Gestiamo solo il drive 8

    uint8_t len = cpu->MemoryRead(0x00B7);
    uint16_t name_ptr = cpu->MemoryRead(0x00BB) | (cpu->MemoryRead(0x00BC) << 8);

    string filename = "";
    for (int i = 0; i < len; i++) {
        filename += (char)cpu->MemoryRead(name_ptr + i);
    }

    // Gestione speciale del listato Directory ($)
    if (filename == "$") {
        vector<uint8_t> dir_bytes = d64->LoadDirectory();

        QFile f("/home/felice/tt_trap");
        if (f.open(QFile::WriteOnly)){
            f.write((char *)dir_bytes.data(),dir_bytes.size());
            f.close();
        }


        uint16_t start_addr = 0x0801; // Indirizzo standard BASIC

        for (size_t i = 0; i < dir_bytes.size(); i++) {
            cpu->MemoryWrite(start_addr + i, dir_bytes[i]);
        }

        uint16_t end_addr = start_addr + dir_bytes.size();
        cpu->MemoryWrite(0x00AE, end_addr & 0xFF);
        cpu->MemoryWrite(0x00AF, (end_addr >> 8) & 0xFF);

        cpu->SetA(0);
        cpu->SetX(end_addr & 0xFF);
        cpu->SetY((end_addr >> 8) & 0xFF);
        cpu->SetP(cpu->GetP() & ~0x01); // Clear Carry Flag (Successo)
        cpu->SetPC(0xF5A9); // Uscita standard del Kernal per LOAD riuscito
        return;
    }

    // Caricamento di un file normale dal D64
    vector<uint8_t> file_bytes = d64->LoadFile(filename);

    if (file_bytes.empty()) {
        // Se il vettore è vuoto, LoadFile ha già impostato "39, FILE NOT FOUND" in drive15
        cpu->SetA(4);                  // Errore Kernal 4 = File Not Found
        cpu->SetP(cpu->GetP() | 0x01);  // Set Carry Flag (Errore)
        cpu->SetPC(0xF480);             // Deviazione all'uscita d'errore del Kernal
        return;
    }

    // Determina l'indirizzo di destinazione
    // Se il Second Address ($B9) è 0, l'utente vuole forzare il caricamento a $0801 (BASIC)
    // Se è 1 (o superiore), usa l'indirizzo memorizzato nei primi 2 byte del file (caricamento assoluto)
    uint8_t sec_addr = cpu->MemoryRead(0x00B9);
    uint16_t target_addr = 0x0801;
    size_t byte_start_idx = 0;

    if (sec_addr != 0) {
        // Preleva l'indirizzo originale dai primi due byte del file (Little Endian)
        target_addr = file_bytes[0] | (file_bytes[1] << 8);
        byte_start_idx = 2; // I dati reali partono dal terzo byte
    } else {
        // Se il caricamento è secondario 0 ma il comando BASIC specifica un indirizzo (es: LOAD"file",8,1),
        // i registri X e Y passati con SETLFS sovrascrivono la destinazione.
        // Controlliamo il flag interno del Kernal a $B0 (0 = Usa indirizzo alternativo in X/Y)
        if (cpu->MemoryRead(0x00B0) == 0) {
            target_addr = cpu->GetX() | (cpu->GetY() << 8);
        }
        byte_start_idx = 2; // Salta comunque i primi 2 byte di intestazione del file
    }

    // Scrittura dei byte estratti nella RAM del C64
    size_t bytes_to_write = file_bytes.size() - byte_start_idx;
    for (size_t i = 0; i < bytes_to_write; i++) {
        cpu->MemoryWrite(target_addr + i, file_bytes[byte_start_idx + i]);
    }

    // Calcola l'indirizzo finale (End Address)
    uint16_t end_addr = target_addr + bytes_to_write;

    // BUG RISOLTO: Aggiorna la Zero Page del Kernal per stabilizzare il BASIC
    cpu->MemoryWrite(0x00AE, end_addr & 0xFF);
    cpu->MemoryWrite(0x00AF, (end_addr >> 8) & 0xFF);

    // // Imposta i registri di uscita richiesti dal Kernal
    cpu->SetA(0);
    cpu->SetX(end_addr & 0xFF);
    cpu->SetY((end_addr >> 8) & 0xFF);
    cpu->SetP(cpu->GetP() & ~0x01); // Clear Carry Flag (Successo)

    // Salta alla routine finale di housekeeping del LOAD del Kernal
    cpu->SetPC(0xF5A9);
}

void Drive::handle_trap_save() {
    uint8_t device = cpu->MemoryRead(0x00BA);
    if (device != 8) return;

    uint8_t len = cpu->MemoryRead(0x00B7);
    uint16_t name_ptr = cpu->MemoryRead(0x00BB) | (cpu->MemoryRead(0x00BC) << 8);

    string filename = "";
    for (int i = 0; i < len; i++) {
        filename += (char)cpu->MemoryRead(name_ptr + i);
    }

    uint16_t start_addr = cpu->MemoryRead(0x002b) | (cpu->MemoryRead(0x002c) << 8);
    uint16_t end_addr = (cpu->MemoryRead(0x002d) | (cpu->MemoryRead(0x002e) << 8));

    if (end_addr <= start_addr) {
        drive15.set_status(34, "SYNTAX ERROR", 0, 0);
        cpu->SetA(5);                  // Errore generico di salvataggio
        cpu->SetP(cpu->GetP() | 0x01);  // Set Carry Flag (Errore)
        cpu->SetPC(0xF704);             // Uscita d'errore standard SAVE del Kernal
        return;
    }

    // Costruiamo il vettore di byte da salvare
    vector<uint8_t> file_bytes;

    // SPECIFICA STANDARD: I file su PRG del 1541 contengono l'indirizzo di inizio
    // nei primi 2 byte del file.
    file_bytes.push_back(start_addr & 0xFF);
    file_bytes.push_back((start_addr >> 8) & 0xFF);

    // Copiamo i dati effettivi dalla RAM dell'emulatore
    for (uint16_t addr = start_addr; addr < end_addr; addr++) {
        file_bytes.push_back(cpu->MemoryRead(addr));
    }

    // Invochiamo la logica robusta del backend D64Image
    bool success = d64->SaveFile(filename, file_bytes);

    if (success) {
        cpu->SetA(0);
        cpu->SetP(cpu->GetP() & ~0x01); // Clear Carry Flag (Successo)

        // Uscita nativa con successo dal SAVE del Kernal
        cpu->SetPC(0xF6BC);
    } else {
        // Errore (es: DISK FULL o FILE EXISTS senza prefisso @:)
        // A è caricato con il codice di errore del bus.
        // In HLE 5 = Device Not Ready / Errore di Scrittura.
        cpu->SetA(5);
        cpu->SetP(cpu->GetP() | 0x01);  // Set Carry Flag (Errore)
        cpu->SetPC(0xF704);             // Deviazione all'uscita d'errore del Kernal
    }
}

void Drive::handle_trap_open() {
    uint8_t device   = cpu->MemoryRead(0x00BA);
    if (device != 8) return; // Gestiamo solo il drive 8

    uint8_t sec_addr = cpu->MemoryRead(0x00B9);
    uint8_t lfn      = cpu->MemoryRead(0x00B8);
    uint8_t len      = cpu->MemoryRead(0x00B7);
    uint16_t name_ptr = cpu->MemoryRead(0x00BB) | (cpu->MemoryRead(0x00BC) << 8);

    string full_cmd = "";
    for (int i = 0; i < len; i++) {
        full_cmd += (char)cpu->MemoryRead(name_ptr + i);
    }

    // Normalizzazione del Second Address (rimozione offset 0x60 del BASIC)
    uint8_t clean_sec_addr = sec_addr;
    if (clean_sec_addr >= 0x60) {
        clean_sec_addr -= 0x60;
    }

    // =========================================================================
    // CASO A: CANALE 15 (Comandi DOS dell'unità)
    // =========================================================================
    if (clean_sec_addr == 15) {
        if (!full_cmd.empty()) {
            process_drive_command(full_cmd);
        } else {
            drive15.buffer_position = 0;
        }

        // Rientro HLE coordinato per il Canale 15
        int error_code = 0;
        if (drive15.error_buffer.length() >= 2 && isdigit(drive15.error_buffer[0])) {
            error_code = (drive15.error_buffer[0] - '0') * 10 + (drive15.error_buffer[1] - '0');
        }
        cout << "[Drive] handle_trap_open success(1)" << endl;
        simulate_rts_success();
        return;
    }

    // =========================================================================
    // CASO B: CANALE DATI (File Sequenziali SEQ, Buffer #, ecc.)
    // =========================================================================
    if (!d64->IsLoaded()) {
        drive15.set_status(74, "DRIVE NOT READY", 0, 0);
        cpu->SetA(5); cpu->SetP(cpu->GetP() | 0x01); cpu->SetPC(0xF704);
        return;
    }

    string filename = full_cmd;
    bool write_mode = false;
    bool is_buffer_channel = false;

    // Controllo canale buffer grezzo ("#")
    if (!full_cmd.empty() && full_cmd[0] == '#') {
        is_buffer_channel = true;
    }

    // Parser delle virgole per i file SEQ standard (es: "NOME,S,W")
    size_t first_comma = full_cmd.find(',');
    if (first_comma != string::npos) {
        filename = full_cmd.substr(0, first_comma);
        string rest = full_cmd.substr(first_comma + 1);
        size_t second_comma = rest.find(',');
        if (second_comma != string::npos) {
            string mode_part = rest.substr(second_comma + 1);
            if (!mode_part.empty() && toupper(mode_part[0]) == 'W') write_mode = true;
        } else if (!rest.empty() && toupper(rest[0]) == 'W') {
            write_mode = true;
        }
    }

    int slot = -1;
    for (int i = 0; i < 10; i++) {
        if (!open_channels[i].is_open) { slot = i; break; }
    }
    if (slot == -1) {
        drive15.set_status(70, "NO CHANNEL", 0, 0);
        cpu->SetA(5);
        cpu->SetP(cpu->GetP() | 0x01);
        cpu->SetPC(0xF704);
        return;
    }

    // Logica di popolamento del buffer HLE
    if (is_buffer_channel) {
        open_channels[slot].buffer.clear();
        open_channels[slot].buffer.resize(256, 0x00);
        open_channels[slot].mode_write = false; // Ripristino originario neutro
    }
    else if (!write_mode) {
        // Lettura file SEQ standard
        vector<uint8_t> file_bytes = d64->LoadFile(filename);
        if (file_bytes.empty() && drive15.error_buffer.find("39") != string::npos) {
            cpu->SetA(4);
            cpu->SetP(cpu->GetP() | 0x01);
            cpu->SetPC(0xF704);
            return;
        }
        open_channels[slot].buffer = file_bytes;
    }
    else {
        // Scrittura file SEQ standard
        open_channels[slot].buffer.clear();
    }

    // Configurazione dello slot HLE del canale dati
    open_channels[slot].is_open = true;
    open_channels[slot].lfn = lfn;
    open_channels[slot].sec_addr = clean_sec_addr;
    open_channels[slot].pointer = 0;
    open_channels[slot].mode_write = write_mode;
    open_channels[slot].filename = filename;

    // Aggiornamento manuale delle tabelle RAM del Kernal C64
    uint8_t num_files = cpu->MemoryRead(0x0098);
    if (num_files < 10) {
        cpu->MemoryWrite(0x0259 + num_files, lfn);
        cpu->MemoryWrite(0x0262 + num_files, 8);
        cpu->MemoryWrite(0x026B + num_files, clean_sec_addr);
        num_files++;
        cpu->MemoryWrite(0x0098, num_files);
    }

    cpu->MemoryWrite(0x00BA, 8);
    cout << "[Drive] handle_trap_open success(2)" << endl;
    simulate_rts_success();
}



// =========================================================================
// 3. TRAP CLOSE CORRETTA DI BASSO LIVELLO ($F291)
// =========================================================================
void Drive::handle_trap_close() {
    uint8_t lfn = cpu->GetA(); // L'LFN da chiudere (es: 2) viene passato in A dal BASIC

    // 1. GESTIONE HLE DI SICUREZZA: Controlliamo prima la NOSTRA tabella interna dei canali dati
    bool hle_file_processed = false;
    for (int i = 0; i < 10; i++) {
        if (open_channels[i].is_open && open_channels[i].lfn == lfn) {
            // Se era in scrittura, eseguiamo FORZATAMENTE il flush sul D64
            if (open_channels[i].mode_write) {
                d64->SaveFile(open_channels[i].filename, open_channels[i].buffer, true);
            }

            // Rilasciamo lo slot
            open_channels[i].is_open = false;
            open_channels[i].buffer.clear();

            if (active_input_channel_idx == i)  active_input_channel_idx = -1;
            if (active_output_channel_idx == i) active_output_channel_idx = -1;

            hle_file_processed = true;
            break;
        }
    }

    // 2. RIMOZIONE E COMPATTAZIONE DELLE TABELLA RAM DEL KERNAL C64
    // Anche se il device era registrato male (0), dobbiamo rimuovere l'LFN dalla tabella
    // di sistema del C64 per evitare che la memoria rimanga satura o instabile.
    uint8_t num_files = cpu->MemoryRead(0x0098);
    int target_index = -1;

    for (int i = 0; i < num_files; i++) {
        if (cpu->MemoryRead(0x0259 + i) == lfn) {
            target_index = i;
            break;
        }
    }

    if (target_index != -1) {
        // Compattiamo l'array spostando indietro le entry successive nelle tabelle del Kernal
        for (int i = target_index; i < num_files - 1; i++) {
            cpu->MemoryWrite(0x0259 + i, cpu->MemoryRead(0x0259 + i + 1));
            cpu->MemoryWrite(0x0262 + i, cpu->MemoryRead(0x0262 + i + 1));
            cpu->MemoryWrite(0x026B + i, cpu->MemoryRead(0x026B + i + 1));
        }
        num_files--;
        cpu->MemoryWrite(0x0098, num_files); // Decrementa il contatore totale del Kernal
    }

    // Se abbiamo elaborato un nostro file HLE o rimosso una entry del Kernal, usciamo puliti
    if (hle_file_processed || target_index != -1) {
        simulate_rts_success();
    }
    // Se non era un file tracciato in nessun modo, lascia andare il Kernal nativo
}


void Drive::process_drive_command(string cmd) {
    if (cmd.empty()) return;

    char token = toupper(cmd[0]);

    // Pulizia e isolamento della stringa dei parametri
    // Cerca i due punti ':' per tagliare prefissi come "R0:", "S0:", "N0:"
    string params = "";
    size_t colon_pos = cmd.find(':');
    if (colon_pos != string::npos) {
        params = cmd.substr(colon_pos + 1);
    } else {
        // Se non ci sono i due punti, rimuove il token e l'eventuale '0' (es: da "I0" o "V0" estrae "")
        size_t start = 1;
        if (cmd.length() > 1 && cmd[1] == '0') start = 2;
        params = cmd.substr(start);
    }

    // Se l'immagine D64 non è pronta, imposta lo stato di errore dell'unità ed esce
    if (!d64->IsLoaded()) {
        drive15.set_status(74, "DRIVE NOT READY", 0, 0);
        return;
    }

    switch (token) {
    case 'I': // INITIALIZE (es: "I0" o "I")
        // Rilegge virtualmente la BAM, azzerando lo stato del Canale 15 a 00, OK
        cout << "Comando I" << endl;
        drive15.set_status(0, "OK", 0, 0);
        break;

    case 'V': // VALIDATE (es: "V0" o "V")
        // Ricostruisce la BAM analizzando i file attivi
        cout << "Comando V" << endl;
        d64->ValidateDisk(drive15);
        break;

    case 'S': // SCRATCH / CANCELLAZIONE (es: "S0:NOMEFILE")
        cout << "Comando S" << endl;
        if (!params.empty()) {
            // DeleteFile prende il nome ed esegue anche il SaveImage() interno modificando la BAM
            d64->DeleteFile(params);
        } else {
            drive15.set_status(34, "SYNTAX ERROR", 0, 0);
        }
        break;

    case 'R': // RENAME / RINOMINA (es: "R0:NUOVO=VECCHIO")
        cout << "Comando R" << endl;
        if (!params.empty()) {
            size_t eq_pos = params.find('=');
            if (eq_pos != string::npos) {
                string new_name = params.substr(0, eq_pos);
                string old_name = params.substr(eq_pos + 1);

                // Esegue la rinomina sul D64 salvando l'immagine aggiornata
                d64->RenameFile(old_name, new_name);
            } else {
                drive15.set_status(34, "SYNTAX ERROR", 0, 0);
            }
        } else {
            drive15.set_status(34, "SYNTAX ERROR", 0, 0);
        }
        break;

    case 'N': // NEW / FORMATTAZIONE (es: "N0:NOMEDISCO,ID")
        cout << "Comando N" << endl;
        if (!params.empty()) {
            size_t comma_pos = params.find(',');
            string disk_name = params;
            string disk_id = "2A"; // ID di default DOS 2.6 se omesso

            if (comma_pos != string::npos) {
                disk_name = params.substr(0, comma_pos);
                disk_id = params.substr(comma_pos + 1);
            }

            // Limita l'ID a 2 caratteri per sicurezza
            if (disk_id.length() > 2) disk_id = disk_id.substr(0, 2);

            // Esegue la formattazione rapida logica, pulendo directory e BAM
            d64->FormatDisk(disk_name, disk_id);
        } else {
            drive15.set_status(34, "SYNTAX ERROR", 0, 0);
        }
        break;

    case 'U': {
        if (cmd.length() < 2) {
            drive15.set_status(34, "SYNTAX ERROR", 0, 0);
            break;
        }

        // Assicuriamoci che il sub_token sia confrontato in maiuscolo
        char sub_token = toupper(cmd[1]);

        if (sub_token == '1' || sub_token == 'A') {
            string raw_pars = cmd.substr(2);
            for (size_t i = 0; i < raw_pars.length(); i++) {
                if (raw_pars[i] == ':' || raw_pars[i] == ',' || raw_pars[i] == ';') raw_pars[i] = ' ';
            }

            int ch_num = -1, drive_num = -1, track_num = -1, sect_num = -1;
            stringstream ss(raw_pars);
            ss >> ch_num >> drive_num >> track_num >> sect_num;

            if (ch_num < 0 || track_num < 1 || track_num > 35 || sect_num < 0) {
                drive15.set_status(34, "SYNTAX ERROR", 0, 0);
                break;
            }

            // Ora la ricerca troverà il canale '2' pulito memorizzato in tabella!
            int slot = -1;
            for (int i = 0; i < 10; i++) {
                if (open_channels[i].is_open && open_channels[i].sec_addr == ch_num) {
                    slot = i;
                    break;
                }
            }

            if (slot == -1) {
                drive15.set_status(70, "NO CHANNEL", 0, 0);
                break;
            }

            size_t sector_offset = d64->GetSectorOffset(track_num, sect_num);

            open_channels[slot].buffer.clear();
            open_channels[slot].buffer.resize(256);

            uint8_t* d64_raw = d64->GetRawDataPtr();
            memcpy(open_channels[slot].buffer.data(), &d64_raw[sector_offset], 256);

            open_channels[slot].pointer = 0;

            cout << "[Drive] U1 eseguito su Traccia " << track_num << " Settore " << sect_num << endl;
            drive15.set_status(0, "OK", 0, 0);
        }
        break;
    }

    case 'B': {
        // RIPRISTINO ORIGINALE: Gestione solo del comando "B-R:" o "B-R "
        if (cmd.length() >= 3 && cmd[1] == '-' && toupper(cmd[2]) == 'R') {
            string raw_pars = cmd.substr(3);
            for (size_t i = 0; i < raw_pars.length(); i++) {
                if (raw_pars[i] == ':' || raw_pars[i] == ',' || raw_pars[i] == ';') raw_pars[i] = ' ';
            }

            int ch_num = -1, drive_num = -1, track_num = -1, sect_num = -1;
            stringstream ss(raw_pars);
            ss >> ch_num >> drive_num >> track_num >> sect_num;

            if (ch_num < 0 || track_num < 1 || track_num > 35 || sect_num < 0) {
                drive15.set_status(34, "SYNTAX ERROR", 0, 0);
                break;
            }

            int slot = -1;
            for (int i = 0; i < 10; i++) {
                if (open_channels[i].is_open && open_channels[i].sec_addr == ch_num) {
                    slot = i;
                    break;
                }
            }

            if (slot == -1) {
                drive15.set_status(70, "NO CHANNEL", 0, 0);
                break;
            }

            size_t sector_offset = d64->GetSectorOffset(track_num, sect_num);
            open_channels[slot].buffer.clear();
            open_channels[slot].buffer.resize(256);

            uint8_t* d64_raw = d64->GetRawDataPtr();
            memcpy(open_channels[slot].buffer.data(), &d64_raw[sector_offset], 256);

            // B-R imposta il puntatore a 2 saltando i link T/S di sistema
            open_channels[slot].pointer = 2;

            drive15.set_status(0, "OK", 0, 0);
        } else {
            drive15.set_status(31, "SYNTAX ERROR", 0, 0);
        }
        break;
    }

    default:
        // Comando DOS sconosciuto o non supportato dall'emulatore
        cout << "Comando Sconosciuto" << endl;
        drive15.set_status(31, "SYNTAX ERROR", 0, 0);
        break;
    }
}

// =========================================================================
// 1. UTILITY: RTS MANUALE TRAMITE STACK
// =========================================================================

void Drive::simulate_rts_success() {
    uint8_t sp = cpu->GetS();
    uint8_t ret_lo = cpu->MemoryRead(0x0100 + (uint8_t)(sp + 1));
    uint8_t ret_hi = cpu->MemoryRead(0x0100 + (uint8_t)(sp + 2));

    cpu->SetS(sp + 2); // Incrementa lo stack pointer di 2 byte
    uint16_t target_pc = (ret_lo | (ret_hi << 8)) + 1;

    cpu->SetA(0);                   // Nessun errore
    cpu->SetP(cpu->GetP() & ~0x01); // Pulisce il Carry Flag (Successo)
    cpu->SetPC(target_pc);          // Salto al chiamante BASIC
}

void Drive::handle_trap_chkin() {
    uint8_t lfn = cpu->GetX(); // Il BASIC passa l'LFN nel registro X

    for (int i = 0; i < 10; i++) {
        if (open_channels[i].is_open && open_channels[i].lfn == lfn) {
            active_input_channel_idx = i;

            cpu->MemoryWrite(0x0099, 8);   // Input Device = 8
            cpu->MemoryWrite(0x00BA, 8);   // Device Number = 8
            cpu->MemoryWrite(0x00B5, lfn); // Current LFN = 2

            // RTS manuale tramite stack
            uint8_t sp = cpu->GetS();
            uint8_t ret_lo = cpu->MemoryRead(0x0100 + (uint8_t)(sp + 1));
            uint8_t ret_hi = cpu->MemoryRead(0x0100 + (uint8_t)(sp + 2));
            cpu->SetS(sp + 2);
            uint16_t target_pc = (ret_lo | (ret_hi << 8)) + 1;

            cpu->SetX(lfn);
            cpu->SetA(8); // A deve restituire l'indirizzo del device
            cpu->SetP(cpu->GetP() & ~0x01); // Pulisce il Carry
            cpu->SetPC(target_pc);
            return;
        }
    }
}

void Drive::handle_trap_chrin_getin() {
    // FILTRO ASSOLUTO: Se il dispositivo di input corrente non è l'8,
    // lascia andare la tastiera o lo schermo nativi
    if (cpu->MemoryRead(0x0099) != 8) {
        return;
    }

    if (active_input_channel_idx == -1) return;

    OpenFileChannel& ch = open_channels[active_input_channel_idx];
    uint8_t current_st = cpu->MemoryRead(0x0090);
    uint8_t extracted_byte = 0;

    if (ch.pointer < ch.buffer.size()) {
        extracted_byte = ch.buffer[ch.pointer];
        cpu->SetA(extracted_byte); // Mette il carattere REALE nell'Accumulatore A
        ch.pointer++;

        // Gestione corretta dello Status Byte (ST) del C64
        if (ch.pointer == ch.buffer.size()) {
            cpu->MemoryWrite(0x0090, current_st | 0x40); // Fine settore (EOI)
        } else {
            cpu->MemoryWrite(0x0090, current_st & ~0x40);
        }
    } else {
        extracted_byte = 0x0D;
        cpu->SetA(0x0D);
        cpu->MemoryWrite(0x0090, current_st | 0x40);
    }

    // --- AGGIORNAMENTO FLAG PROCESSORE (Z e N) PER IL BASIC ---
    uint8_t status_p = cpu->GetP();

    // Flag Z (Zero): deve essere 1 SE il carattere è 0. Deve essere 0 SE il carattere NON è 0.
    if (extracted_byte == 0) status_p |= 0x02;
    else                       status_p &= ~0x02;

    // Flag N (Negativo): riflette il bit 7 del byte
    if (extracted_byte & 0x80) status_p |= 0x80;
    else                       status_p &= ~0x80;

    // CRUCIALE: Pulisce il Carry Flag (Bit 0) per dire al BASIC che l'I/O è riuscito senza timeout!
    status_p &= ~0x01;

    cpu->SetP(status_p);

    // Il registro X deve contenere il Logical File Number (2)
    cpu->SetX(ch.lfn);

    // --- RTS MANUALE TRAMITE STACK POINTER ---
    // Estraiamo l'indirizzo di ritorno per saltare via PRIMA che il Kernal tocchi lo schermo!
    uint8_t sp = cpu->GetS();
    uint8_t ret_lo = cpu->MemoryRead(0x0100 + (uint8_t)(sp + 1));
    uint8_t ret_hi = cpu->MemoryRead(0x0100 + (uint8_t)(sp + 2));
    cpu->SetS(sp + 2); // Simula i due PLA della RTS
    uint16_t target_pc = (ret_lo | (ret_hi << 8)) + 1;

    cpu->SetPC(target_pc); // Salto diretto alla riga 140 del BASIC
}

void Drive::handle_trap_chkout() {
    uint8_t lfn = cpu->GetX(); // Il BASIC passa l'LFN nel registro X

    for (int i = 0; i < 10; i++) {
        if (open_channels[i].is_open && open_channels[i].lfn == lfn && open_channels[i].mode_write) {
            active_output_channel_idx = i;

            // --- ALLINEAMENTO ZERO PAGE FONDAMENTALE PER CHROUT ---
            // Dobbiamo dire esplicitamente alla CPU che il dispositivo di output attivo ORA è l'8,
            // altrimenti il filtro protettivo di CHROUT rifiuterà la chiamata pensando sia per lo schermo!
            cpu->MemoryWrite(0x009A, 8);   // $009A = Current Output Device (8)
            cpu->MemoryWrite(0x00BA, 8);   // $00BA = Current Device Number (8)
            cpu->MemoryWrite(0x0090, 0x00); // Pulisce lo Status Byte da residui

            // --- RTS MANUALE TRAMITE STACK ---
            simulate_rts_success();
            return;
        }
    }
}

void Drive::handle_trap_chrout() {
    // Se la Zero Page $9A non è stata impostata a 8 da CHKOUT, lascia andare lo schermo nativo
    if (cpu->MemoryRead(0x009A) != 8) {
        return;
    }

    // Usiamo lo slot attivo impostato un attimo prima da CHKOUT
    if (active_output_channel_idx == -1) return;

    OpenFileChannel& ch = open_channels[active_output_channel_idx];
    uint8_t byte_to_write = cpu->GetA();
    ch.buffer.push_back(byte_to_write);

    // --- RTS MANUALE TRAMITE STACK ---
    simulate_rts_success();
}


void Drive::handle_trap_clrchn() {
    // 1. Resetta gli indici dei canali HLE attivi all'interno del nostro emulatore
    active_input_channel_idx = -1;
    active_output_channel_idx = -1;

    // 2. RIPRISTINO COERENTE DELLE ZERO PAGE DEL KERNAL
    // Quando i canali seriali vengono chiusi, il C64 rimette come input la Tastiera (0)
    // e come output lo Schermo (3). Lo facciamo noi in HLE.
    cpu->MemoryWrite(0x0099, 0x00); // Default Input Device = Tastiera
    cpu->MemoryWrite(0x009A, 0x03); // Default Output Device = Schermo

    // --- RTS MANUALE TRAMITE STACK ---
    // Estraiamo l'indirizzo di ritorno per saltare via il loop seriale nativo bloccante
    simulate_rts_success();
}



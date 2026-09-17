// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "d64image.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <QFile>

using namespace std;
extern DriveStatusChannel drive15;

D64Image::D64Image(QObject *parent) : QObject(parent), loaded(false) {}

// Calcola l'offset a 256 byte nel file .d64 partendo da Traccia e Settore standard
size_t D64Image::GetSectorOffset(int track, int sector) {
    if (track < 1 || track > 35) return 0;
    size_t sector_index = 0;
    for (int i = 1; i < track; i++) {
        sector_index += sectors_per_track[i];
    }
    sector_index += sector;
    return sector_index * 256;
}

string D64Image::CleanC64Name(const uint8_t* raw_name, int max_len) {
    string res = "";
    for (int i = 0; i < max_len; i++) {
        if (raw_name[i] == 0xA0 || raw_name[i] == 0x00) break;
        res += (char)toupper(raw_name[i]);
    }
    return res;
}

bool D64Image::LoadImage(const string& filepath) {
    ifstream file(filepath, ios::binary | ios::ate);
    if (!file.is_open()) {
        drive15.set_status(74, "DRIVE NOT READY", 0, 0);
        loaded = false;
        return false;
    }

    streamsize size = file.tellg();
    file.seekg(0, ios::beg);

    data.resize(size);
    if (file.read((char*)data.data(), size)) {
        current_filepath = filepath;
        loaded = true;
        drive15.set_status(0, "OK", 0, 0);
        return true;
    }

    drive15.set_status(74, "DRIVE NOT READY", 0, 0);
    loaded = false;
    return false;
}

bool D64Image::SaveImage() {
    if (!loaded || current_filepath.empty()) {
        drive15.set_status(74, "DRIVE NOT READY", 0, 0);
        return false;
    }

    ofstream file(current_filepath, ios::binary);
    if (!file.is_open()) {
        drive15.set_status(26, "WRITE PROTECT ON", 0, 0);
        return false;
    }

    file.write((const char*)data.data(), data.size());
    return true;
}

D64Image::FileEntry D64Image::FindFile(string target_name) {
    FileEntry entry;
    if (!loaded) return entry;

    string clean_target = "";
    for (char c : target_name) {
        if (c >= 32 && c <= 126 && c != '"') clean_target += toupper(c);
    }

    uint8_t dir_track = 18;
    uint8_t dir_sector = 1;

    while (dir_track != 0) {
        size_t offset = GetSectorOffset(dir_track, dir_sector);
        if (offset + 256 > data.size()) break;

        for (int e = 0; e < 8; e++) {
            size_t entry_offset = offset + (e * 32);
            uint8_t file_type = data[entry_offset + 2]; // Tipo file a offset +2 secondo specifiche

            if (file_type & 0x80) { // File Attivo
                string current_name = CleanC64Name(&data[entry_offset + 5], 16); // Nome file a offset +5
                bool is_match = true;

                // --- PATTERN MATCHING AVANZATO CON '*' E '?' ---
                for (size_t i = 0; i < clean_target.length(); i++) {
                    char t_char = clean_target[i];

                    // Se incontriamo un asterisco '*', la ricerca per questo file ha successo immediato
                    if (t_char == '*') {
                        break;
                    }

                    // Se il nome sul disco è più corto della stringa cercata (e non abbiamo ancora trovato un '*'), non coincide
                    if (i >= current_name.length()) {
                        is_match = false;
                        break;
                    }

                    // Se c'è il punto interrogativo '?', accettiamo qualsiasi carattere e proseguiamo
                    if (t_char == '?') {
                        continue;
                    }

                    // Verifica del carattere esatto
                    if (current_name[i] != t_char) {
                        is_match = false;
                        break;
                    }
                }

                // Se dopo il ciclo la stringa cercata (senza asterisco) è più corta del nome sul disco,
                // il 1541 reale non lo considera un match esatto (a meno che non ci fosse un '*' finale).
                // Ad esempio cercando "PIPPO" non deve trovare "PIPPO2".
                if (is_match && clean_target.find('*') == string::npos) {
                    if (current_name.length() != clean_target.length()) {
                        is_match = false;
                    }
                }

                if (is_match) {
                    entry.found = true;
                    entry.file_type = file_type;
                    entry.first_track = data[entry_offset + 3];  // Traccia a offset +3
                    entry.first_sector = data[entry_offset + 4]; // Settore a offset +4
                    entry.dir_sector_offset = entry_offset;
                    entry.size_blocks = data[entry_offset + 30] | (data[entry_offset + 31] << 8);
                    return entry;
                }
            }
        }
        dir_track = data[offset + 0];
        dir_sector = data[offset + 1];
    }
    return entry;
}


vector<uint8_t> D64Image::LoadFile(const string& filename) {
    vector<uint8_t> file_bytes;
    FileEntry entry = FindFile(filename);

    if (!entry.found) {
        cout << "[D64Image] File " << filename << " not found" << endl;
        drive15.set_status(39, "FILE NOT FOUND", 0, 0);
        return file_bytes;
    }

    int track = entry.first_track;
    int sector = entry.first_sector;

    while (track != 0) {
        size_t offset = GetSectorOffset(track, sector);
        if (offset + 256 > data.size()) break;

        int next_track = data[offset + 0];
        int next_sector = data[offset + 1];

        if (next_track == 0) {
            // Ultimo settore: il byte in next_sector indica quanti byte utili ci sono
            for (int i = 2; i <= next_sector; i++) {
                file_bytes.push_back(data[offset + i]);
            }
        } else {
            // Settore intero: 254 byte di dati reali
            for (int i = 2; i < 256; i++) {
                file_bytes.push_back(data[offset + i]);
            }
        }
        track = next_track;
        sector = next_sector;
    }

    drive15.set_status(0, "OK", 0, 0);
    cout << "[D64Image] Loaded File " << filename << ", bytes " << (uint) file_bytes.size() << endl;
    return file_bytes;
}

bool D64Image::AllocateSector(int& allocated_track, int& allocated_sector) {
    size_t bam_offset = GetSectorOffset(18, 0);

    // Strategia standard 1541: Cerca partendo dalla traccia 1 in fuori (evita la 18 se possibile)
    for (int t = 1; t <= 35; t++) {
        if (t == 18) continue;
        size_t t_idx = bam_offset + (t * 4);
        if (data[t_idx] > 0) { // Ci sono settori liberi
            for (int s = 0; s < sectors_per_track[t]; s++) {
                int byte_offset = s / 8;
                int bit_offset = s % 8;
                if (data[t_idx + 1 + byte_offset] & (1 << bit_offset)) {
                    // Trovato: lo marchiamo occupato (bit a 0)
                    data[t_idx + 1 + byte_offset] &= ~(1 << bit_offset);
                    data[t_idx]--;
                    allocated_track = t;
                    allocated_sector = s;
                    return true;
                }
            }
        }
    }
    return false;
}

void D64Image::FreeSector(int track, int sector) {
    if (track < 1 || track > 35) return;
    size_t bam_offset = GetSectorOffset(18, 0);
    size_t t_idx = bam_offset + (track * 4);
    int byte_offset = sector / 8;
    int bit_offset = sector % 8;

    if (!(data[t_idx + 1 + byte_offset] & (1 << bit_offset))) {
        data[t_idx + 1 + byte_offset] |= (1 << bit_offset); // rimetti a 1 (libero)
        data[t_idx]++;
    }
}

bool D64Image::SaveFile(const string& filename, const vector<uint8_t>& file_bytes, bool is_seq) {
    if (file_bytes.empty()) {
        cout << "[D64Image] file empty" << endl;
        drive15.set_status(34, "SYNTAX ERROR", 0, 0);
        return false;
    }


    // Gestione prefisso "@:" per sovrascrittura sicura
    string clean_name = filename;
    if (filename.length() >= 2 && filename[0] == '@' && filename[1] == ':') {
        clean_name = filename.substr(2);
        DeleteFile(clean_name); // Rimuove il vecchio se esiste
    } else {
        if (FindFile(clean_name).found) {
            cout << "[D64Image] file exists on disk" << endl;
            drive15.set_status(63, "FILE EXISTS", 0, 0);
            return false;
        }
    }

    size_t total_bytes = file_bytes.size();
    size_t bytes_written = 0;
    int current_track = 0, current_sector = 0;
    int prev_track = 0, prev_sector = 0;
    uint16_t block_count = 0;

    bool first_block = true;
    int start_track = 0, start_sector = 0;

    while (bytes_written < total_bytes) {
        if (!AllocateSector(current_track, current_sector)) {
            cout << "[D64Image] Disk Full" << endl;
            drive15.set_status(72, "DISK FULL", 0, 0);
            return false;
        }
        block_count++;

        if (first_block) {
            start_track = current_track;
            start_sector = current_sector;
            first_block = false;
        } else {
            // Aggiorna i puntatori del blocco precedente a questo corrente
            size_t prev_offset = GetSectorOffset(prev_track, prev_sector);
            data[prev_offset + 0] = current_track;
            data[prev_offset + 1] = current_sector;
        }

        size_t current_offset = GetSectorOffset(current_track, current_sector);
        size_t chunk_size = total_bytes - bytes_written;

        if (chunk_size <= 254) {
            // Ultimo blocco
            data[current_offset + 0] = 0x00;
            data[current_offset + 1] = chunk_size + 1; // indica l'ultimo byte valido (+1 per includere offset dati)
            memcpy(&data[current_offset + 2], &file_bytes[bytes_written], chunk_size);
            bytes_written += chunk_size;
        } else {
            // Blocco intero
            data[current_offset + 0] = 0x99; // Placeholder provvisorio traccia successiva
            data[current_offset + 1] = 0x99; // Placeholder provvisorio settore successivo
            memcpy(&data[current_offset + 2], &file_bytes[bytes_written], 254);
            bytes_written += 254;
        }

        prev_track = current_track;
        prev_sector = current_sector;
    }

    // Scrittura Entry Directory
    uint8_t dir_track = 18;
    uint8_t dir_sector = 1;
    bool entry_written = false;

    while (dir_track != 0 && !entry_written) {
        size_t offset = GetSectorOffset(dir_track, dir_sector);
        for (int e = 0; e < 8; e++) {
            size_t entry_offset = offset + (e * 32);

            // Controlliamo se la entry è libera (tipo file = $00 o $00 nell'offset corretto)
            if (data[entry_offset + 2] == 0x00) {
                data[entry_offset + 2] = is_seq ? 0x81 : 0x82;  // Offset +2: Tipo PRG ($82) o tipo SEQ ($81)
                data[entry_offset + 3] = start_track;  // Offset +3: Traccia iniziale dati
                data[entry_offset + 4] = start_sector; // Offset +4: Settore iniziale dati

                // Offset +5: Scrittura nome file (16 byte imbottiti con $A0)
                for (uint i = 0; i < 16; i++) {
                    if (i < clean_name.length()) data[entry_offset + 5 + i] = toupper(clean_name[i]);
                    else                         data[entry_offset + 5 + i] = 0xA0;
                }

                // Se si tratta della PRIMA entry del blocco (e == 0), non dobbiamo azzerare
                // i byte 0 e 1 perché contengono il link al prossimo settore della directory!
                // Se e > 0, possiamo pulire i byte inutilizzati 0 e 1 di quella specifica entry.
                if (e > 0) {
                    data[entry_offset + 0] = 0x00;
                    data[entry_offset + 1] = 0x00;
                }

                // Offset +30 e +31: Dimensione blocchi (Little Endian)
                data[entry_offset + 30] = block_count & 0xFF;
                data[entry_offset + 31] = (block_count >> 8) & 0xFF;

                entry_written = true;
                break;
            }
        }
        if (entry_written) break;
        // Se il settore è pieno, controlla se c'è un link al prossimo settore directory
        if (data[offset + 0] == 0 && data[offset + 1] == 0xFF) {
            // Fine dei settori di directory allocati, dobbiamo allocarne uno nuovo sulla traccia 18
            int new_dir_sector = 0;
            size_t t18_idx = GetSectorOffset(18, 0) + (18 * 4);
            for (int s = 1; s < 19; s++) {
                // Traccia 18 ha 19 settori (0-18)
                int byte_offset = s / 8;
                int bit_offset = s % 8;
                if (data[t18_idx + 1 + byte_offset] & (1 << bit_offset)) {
                    data[t18_idx + 1 + byte_offset] &= ~(1 << bit_offset);
                    data[t18_idx]--;
                    new_dir_sector = s;break;
                }
            }
            if (new_dir_sector == 0) {
                cout << "[D64Image] Disk Full" << endl;
                drive15.set_status(72, "DISK FULL", 0, 0);
                return false;
            }
            data[offset + 0] = 18;
            data[offset + 1] = new_dir_sector;
            size_t new_offset = GetSectorOffset(18, new_dir_sector);
            data[new_offset + 0] = 0x00;data[new_offset + 1] = 0xFF;
            memset(&data[new_offset + 2], 0, 254);
        }
        dir_track = data[offset + 0];
        dir_sector = data[offset + 1];
    }
    SaveImage(); // Rende persistente su PCdrive15.set_status(0, "OK", 0, 0);
    cout << "[D64Image] Saved file '" << clean_name <<"', "<< (uint) file_bytes.size() << " bytes" << endl;
    return true;
}

bool D64Image::DeleteFile(const string& filename) {
    FileEntry entry = FindFile(filename);
    if (!entry.found) {
        cout << "[D64Image] file not found" << endl;
        drive15.set_status(62, "FILE NOT FOUND", 0, 0);
        return false;
    }
    int track = entry.first_track;
    int sector = entry.first_sector;
    // 1. Libera la catena dei settori nella BAM
    while (track != 0) {
        size_t offset = GetSectorOffset(track, sector);
        int next_track = data[offset + 0];
        int next_sector = data[offset + 1];
        FreeSector(track, sector);
        track = next_track;sector = next_sector;
    }
    // 2. Metti il tipo file a DEL (0x00)
    data[entry.dir_sector_offset + 2] = 0x00;
    SaveImage();
    cout << "[D64Image] file deleted" << endl;
    drive15.set_status(1, "FILES SCRATCHED", 1, 0);
    return true;
}

bool D64Image::RenameFile(const string& old_name, const string& new_name) {
    if (new_name.length() > 16 || new_name.empty()) {
        drive15.set_status(32, "SYNTAX ERROR", 0, 0);
        return false;
    }
    FileEntry old_entry = FindFile(old_name);
    if (!old_entry.found) {
        cout << "[D64Image] file not found" << endl;
        drive15.set_status(62, "FILE NOT FOUND", 0, 0);
        return false;
    }
    if (FindFile(new_name).found) {
        cout << "[D64Image] new file already exists on disk" << endl;
        drive15.set_status(63, "FILE EXISTS", 0, 0);
        return false;
    }
    // Scrittura del nuovo nome a offset 3 della entry
    size_t offset = old_entry.dir_sector_offset;
    for (uint i = 0; i < 16; i++) {
        if (i < new_name.length()) data[offset + 5 + i] = toupper(new_name[i]);
        else data[offset + 5 + i] = 0xA0;
    }
    SaveImage();
    cout << "[D64Image] file remamed" << endl;
    drive15.set_status(0, "OK", 0, 0);
    return true;
}

bool D64Image::ValidateDisk(DriveStatusChannel& drive15) {
    if (!loaded) return false;
    size_t bam_offset = GetSectorOffset(18, 0);
    // 1. Rigenera BAM a "Tutto Libero"
    for (int t = 1; t <= 35; t++) {
        size_t t_idx = bam_offset + (t * 4);
        data[t_idx] = sectors_per_track[t];
        data[t_idx + 1] = 0xFF;
        data[t_idx + 2] = 0xFF;
        data[t_idx + 3] = (t <= 17) ? 0xFF : 0x07;
    }
    auto mark_used = [&](int t, int s) {
        if (t < 1 || t > 35 || s < 0 || s >= sectors_per_track[t]) return;
        size_t t_idx = bam_offset + (t * 4);
        int byte_offset = s / 8;
        int bit_offset = s % 8;
        if (data[t_idx + 1 + byte_offset] & (1 << bit_offset)) {
            data[t_idx + 1 + byte_offset] &= ~(1 << bit_offset);
            data[t_idx]--;
        }
    };
    mark_used(18, 0);
    // BAM occupata
    // 2. Proteggi settori Directory
    uint8_t dir_track = 18;
    uint8_t dir_sector = 1;
    while (dir_track != 0) {
        mark_used(dir_track, dir_sector);
        size_t off = GetSectorOffset(dir_track, dir_sector);
        dir_track = data[off + 0];
        dir_sector = data[off + 1];
    }
    // 3. Ricalcola occupazione file attivi
    dir_track = 18;
    dir_sector = 1;
    while (dir_track != 0) {
        size_t off = GetSectorOffset(dir_track, dir_sector);
        for (int e = 0; e < 8; e++) {
            size_t entry_offset = off + (e * 32);
            if (data[entry_offset + 2] & 0x80) { // Controlla il tipo a +2
                int f_track = data[entry_offset + 3];  // Traccia a +3
                int f_sector = data[entry_offset + 4]; // Settore a +4
                while (f_track != 0) {
                    mark_used(f_track, f_sector);
                    size_t f_off = GetSectorOffset(f_track, f_sector);
                    f_track = data[f_off + 0];
                    f_sector = data[f_off + 1]; // I link interni ai file rimangono a offset 0 e 1 del blocco dati
                }
            }
        }
        dir_track = data[off + 0];
        dir_sector = data[off + 1];
    }
    SaveImage();
    cout << "[D64Image] Disk validated" << endl;
    drive15.set_status(0, "OK", 0, 0);
    return true;
}

bool D64Image::FormatDisk(const string& diskname, const string& id) {
    if (!loaded) return false;
    size_t bam_offset = GetSectorOffset(18, 0);
    // 1. BAM Libera
    for (int t = 1; t <= 35; t++) {
        size_t t_idx = bam_offset + (t * 4);
        data[t_idx] = sectors_per_track[t];
        data[t_idx + 1] = 0xFF;data[t_idx + 2] = 0xFF;
        data[t_idx + 3] = (t <= 17) ? 0xFF : 0x07;
    }
    // Rimuovi BAM (18,0) e Dir iniziale (18,1)
    size_t t18 = bam_offset + (18 * 4);
    data[t18 + 1] &= ~0x03;
    data[t18] -= 2;
    // 2. Intestazione BAM (Offset $90)
    size_t n_off = bam_offset + 0x90;
    for (uint i = 0; i < 16; i++) {
        if (i < diskname.length()) data[n_off + i] = toupper(diskname[i]);
        else data[n_off + i] = 0xA0;
    }
    data[bam_offset + 0xA0] = 0xA0;
    data[bam_offset + 0xA1] = 0xA0;
    data[bam_offset + 0xA2] = (id.length() > 0) ? toupper(id[0]) : '2';
    data[bam_offset + 0xA3] = (id.length() > 1) ? toupper(id[1]) : 'A';
    data[bam_offset + 0xA4] = 0xA0;
    data[bam_offset + 0xA5] = '2';
    data[bam_offset + 0xA6] = 'A';
    for(int i = 0xA7; i <= 0xFF; i++) data[bam_offset + i] = 0xA0;
    // 3. Pulisci primo blocco Directory (18,1)
    size_t d_off = GetSectorOffset(18, 1);
    data[d_off + 0] = 0x00;
    data[d_off + 1] = 0xFF;
    memset(&data[d_off + 2], 0, 254);
    SaveImage();
    cout << "[D64Image] Disk formatted" << endl;
    drive15.set_status(0, "OK", 0, 0);
    return true;
}

vector<uint8_t> D64Image::LoadDirectory() {
    vector<uint8_t> dir_bytes;
    if (!loaded) return dir_bytes;

    size_t bam_offset = GetSectorOffset(18, 0);
    if (bam_offset + 256 > data.size()) return dir_bytes;

    uint16_t current_ram = 0x0801; // Indirizzo di partenza in RAM del BASIC

    // --- 1. RIGA 0: INTESTAZIONE DEL DISCO ---
    // Calcoliamo la lunghezza fissa di questa riga:
    // 2 (link) + 2 (num riga) + 1 (RVS_ON) + 1 (quote) + 16 (nome) + 1 (quote) + 1 (spazio) + 2 (ID) + 1 (spazio) + 2 (2A) + 1 (fine riga) = 30 byte
    uint16_t next_ram = current_ram + 30;

    dir_bytes.push_back(next_ram & 0xFF);        // Link alla riga successiva (Low)
    dir_bytes.push_back((next_ram >> 8) & 0xFF); // Link alla riga successiva (High)
    dir_bytes.push_back(0x00);                   // Numero riga: 0 (Low)
    dir_bytes.push_back(0x00);                   // Numero riga: 0 (High)
    dir_bytes.push_back(0x12);                   // RVS ON ($12)
    dir_bytes.push_back('"');
    for (int i = 0; i < 16; i++) {
        dir_bytes.push_back(data[bam_offset + 0x90 + i]); // Nome disco
    }
    dir_bytes.push_back('"');
    dir_bytes.push_back(' ');
    dir_bytes.push_back(data[bam_offset + 0xA2]); // ID 1
    dir_bytes.push_back(data[bam_offset + 0xA3]); // ID 2
    dir_bytes.push_back(' ');
    dir_bytes.push_back(data[bam_offset + 0xA5]); // '2'
    dir_bytes.push_back(data[bam_offset + 0xA6]); // 'A'
    dir_bytes.push_back(0x00);                   // Fine riga 0

    current_ram = next_ram;

    // --- 2. RIGHE DEI FILE ---
    uint8_t dir_track = 18;
    uint8_t dir_sector = 1;

    while (dir_track != 0) {
        size_t offset = GetSectorOffset(dir_track, dir_sector);
        if (offset + 256 > data.size()) break;

        for (int e = 0; e < 8; e++) {
            size_t entry_offset = offset + (e * 32);
            uint8_t file_type = data[entry_offset + 2]; // Tipo file a offset +2

            if (file_type & 0x80) { // File Attivo
                vector<uint8_t> fileline;
                // Ogni riga file ha lunghezza fissa:
                // 2 (link) + 2 (num riga/blocchi) + 3 (spazi) + 1 (quote) + 16 (nome) + 1 (quote) + 1 (spazio) + 3 (tipo) + 2 (flags) + 1 (fine riga) = 32 byte
                next_ram = current_ram + 32;

                fileline.push_back(next_ram & 0xFF);        // Link Low
                fileline.push_back((next_ram >> 8) & 0xFF); // Link High

                // Numero di riga = Dimensione in blocchi
                uint16_t blocks = data[entry_offset + 30] | (data[entry_offset + 31] << 8);
                fileline.push_back(blocks & 0xFF);
                fileline.push_back((blocks >> 8) & 0xFF);

                // Spazi di formattazione per allineare le virgolette in base alle cifre dei blocchi
                uint8_t l = to_string(blocks).length();
                for (uint i=0; i<4-l; i++) fileline.push_back(' ');

                fileline.push_back('"');
                for (int i = 0; i < 16; i++) {
                    uint8_t c = data[entry_offset + 5 + i];
                    if (c != 0xA0) {
                        fileline.push_back(c); // Nome file
                    }
                }
                fileline.push_back('"');

                uint sz = fileline.size();
                for(uint i=0; i<27-sz-l; i++) fileline.push_back(' ');

                // Stringa del tipo file
                string type_str = "???";
                switch (file_type & 0x07) {
                case 0: type_str = "DEL"; break;
                case 1: type_str = "SEQ"; break;
                case 2: type_str = "PRG"; break;
                case 3: type_str = "USR"; break;
                case 4: type_str = "REL"; break;
                }
                for (char c : type_str) fileline.push_back(c);

                // Flag finali (* o spazi, < o spazi)
                if (!(file_type & 0x80)) fileline.push_back('*');
                else if (file_type & 0x40) fileline.push_back('<');
                else fileline.push_back(' ');

                sz = fileline.size();
                for(uint i=0; i<31-sz; i++) fileline.push_back(' ');

                fileline.push_back(0x00); // Fine riga file
                dir_bytes.insert(dir_bytes.end(), fileline.begin(), fileline.end());

                current_ram = next_ram;
            }
        }
        dir_track = data[offset + 0];
        dir_sector = data[offset + 1];
    }

    // --- 3. ULTIMA RIGA: BLOCCHI LIBERI ---
    // Lunghezza riga blocchi liberi:
    // 2 (link) + 2 (num riga) + 12 (BLOCKS FREE.) + 12 (spazi) + 1 (fine riga) = 29 byte
    next_ram = current_ram + 29;

    dir_bytes.push_back(next_ram & 0xFF);
    dir_bytes.push_back((next_ram >> 8) & 0xFF);

    // Conta i blocchi liberi reali
    uint16_t free_blocks = 0;
    for (int t = 1; t <= 35; t++) {
        if (t == 18) continue;
        free_blocks += data[bam_offset + (t * 4)];
    }
    dir_bytes.push_back(free_blocks & 0xFF);
    dir_bytes.push_back((free_blocks >> 8) & 0xFF);

    string free_str = "BLOCKS FREE.";
    for (char c : free_str) dir_bytes.push_back(c);
    for (int i = 0; i < 12; i++) dir_bytes.push_back(' '); // Spazi di riempimento fittizi

    dir_bytes.push_back(0x00); // Fine riga blocchi

    current_ram = next_ram;

    // --- 4. FINE DEL PROGRAMMA BASIC ---
    // Il BASIC si aspetta che la riga finale abbia come puntatore successivo 00 00
    dir_bytes.push_back(0x00);
    dir_bytes.push_back(0x00);

    cout << "[D64Image] Directory read" << endl;
    return dir_bytes;
}

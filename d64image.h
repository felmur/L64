#ifndef D64IMAGE_H
#define D64IMAGE_H

#include <QObject>
#include <vector>
#include <string>
#include <fstream>
using namespace std;

// Dichiarazione preventiva della struttura del canale 15
struct DriveStatusChannel {
    string error_buffer = "73,CBM DOS V2.6 1541,00,00\r";
    size_t buffer_position = 0;
    bool active_for_input = false;

    void set_status(int code, const string& message, int track, int sector) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%02d,%s,%02d,%02d\r", code, message.c_str(), track, sector);
        error_buffer = buf;
        buffer_position = 0;
    }
};

class D64Image : public QObject
{
    Q_OBJECT
public:
    explicit D64Image(QObject *parent = nullptr);

    // Struttura standard di una entry trovata
    struct FileEntry {
        bool found = false;
        uint8_t file_type = 0;
        int first_track = 0;
        int first_sector = 0;
        size_t size_bytes = 0;
        size_t size_blocks = 0;
        size_t dir_sector_offset = 0; // Offset assoluto della entry nel vettore data
    };

    // Operazioni richieste
    bool LoadImage(const string& filepath);
    bool SaveImage(); // Salva le modifiche sul file PC corrente

    // Gestione File
    vector<uint8_t> LoadFile(const string& filename);
    bool SaveFile(const string& filename, const vector<uint8_t>& file_bytes, bool is_seq = false);
    bool DeleteFile(const string& filename);
    bool RenameFile(const string& old_name, const string& new_name);

    // Manutenzione Disco
    bool ValidateDisk(DriveStatusChannel& drive15);
    bool FormatDisk(const string& diskname, const string& id);

    // Generazione Directory per il BASIC
    vector<uint8_t> LoadDirectory();

    // Utility di stato
    bool IsLoaded() const { return loaded; }
    FileEntry FindFile(string target_name);

    size_t GetSectorOffset(int track, int sector);
    uint8_t* GetRawDataPtr() { return data.data(); }

private:
    vector<uint8_t> data;
    bool loaded = false;
    string current_filepath;

    // Settori per traccia standard 1541
    const int sectors_per_track[36] = {
        0,
        21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21, // 1-17
        19,19,19,19,19,19,19,                               // 18-24
        18,18,18,18,18,18,                                  // 25-30
        17,17,17,17,17                                      // 31-35
    };

    // Funzioni helper interne
    string CleanC64Name(const uint8_t* raw_name, int max_len);
    bool AllocateSector(int& allocated_track, int& allocated_sector);
    void FreeSector(int track, int sector);
};

#endif // D64IMAGE_H


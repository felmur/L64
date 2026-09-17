#ifndef DRIVE_H
#define DRIVE_H

#include <QObject>
#include <vector>

using namespace std;

// Struttura HLE per un singolo canale di file sequenziale/dati aperto
struct OpenFileChannel {
    bool is_open = false;
    uint8_t lfn = 0;         // Logical File Number
    uint8_t sec_addr = 0;    // Secondary Address
    vector<uint8_t> buffer;  // Buffer contenente tutti i byte del file
    size_t pointer = 0;      // Indice del byte corrente in lettura/scrittura
    bool mode_write = false; // true = scrittura, false = lettura
    string filename;
};

class Drive : public QObject
{
    Q_OBJECT
public:
    explicit Drive(QObject *parent = nullptr);

    void handle_trap_load();
    void handle_trap_save();
    void handle_trap_open();
    void handle_trap_close();
    void process_drive_command(string cmd);
    void simulate_rts_success();
    void handle_trap_chkin();
    void handle_trap_chkout();
    void handle_trap_chrin_getin();
    void handle_trap_chrout();
    void handle_trap_clrchn();
    void handle_trap_chrout_hardware();

    string ToLowerCase(string str);

    void handle_trap_ciout_hardware();
    void handle_trap_listen_hardware();
signals:
};

#endif // DRIVE_H

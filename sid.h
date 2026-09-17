#ifndef SID_H
#define SID_H

#include <QObject>
#include <SDL2/SDL.h>
#include <cstdint>
#include <mutex>

using namespace std;

struct SIDVoice {
    uint16_t frequency = 0;
    uint16_t pulseWidth = 0;
    uint8_t waveControl = 0;
    uint8_t attackDecay = 0;
    uint8_t sustainRelease = 0;

    double phase = 0.0;
    enum ADSRState { ATTACK, DECAY, SUSTAIN, RELEASE, IDLE };
    ADSRState adsrState = IDLE;
    double envLevel = 0.0;
};

class SID : public QObject {
    Q_OBJECT
public:
    explicit SID(QObject *parent = nullptr);
    ~SID();

    void Reset();
    void Poke(uint16_t address, uint8_t value);
    uint8_t Peek(uint16_t address);

    // Non serve più un update sincrono che genera vettori!
    // Ora l'Update incrementa solo i registri se necessario.
    void Update(int cycles);

    // Funzione che SDL chiamerà automaticamente in background quando ha bisogno di audio
    void AudioCallback(uint8_t *stream, int len);

private:
    static const int SAMPLE_RATE = 44100;

    uint8_t regs[0x20];
    SIDVoice voices[3];
    SDL_AudioDeviceID audioDevice;

    // Mutex standard C++ per proteggere i registri durante la lettura del thread SDL
    mutex sidMutex;

    int16_t GenerateNextSample();
    void UpdateADSR(int voiceIdx);

    // Variabili di stato del filtro (una per canale/passo di integrazione)
    double f_low;  // Stato Low-Pass
    double f_band; // Stato Band-Pass
};

#endif // SID_H

// *****************************************************
// L64 - Yet Another C64 Emultator for Linux
// (c) 2026 by Felice Murolo, Salerno, Italia
// email: linuxboy@fel.hotpo.org
// *****************************************************

#include "sid.h"
#include <cstdlib>

// Funzione ponte statica richiesta da SDL
void SDL_Audio_Callback_Wrapper(void *userdata, uint8_t *stream, int len) {
    static_cast<SID*>(userdata)->AudioCallback(stream, len);
}

SID::SID(QObject *parent) : QObject(parent), audioDevice(0) {
    // Inizializza il sottosistema audio di SDL2
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
        printf("Errore inizializzazione SDL Audio: %s\n", SDL_GetError());
        return;
    }

    SDL_AudioSpec desiredSpec;
    SDL_zero(desiredSpec);
    desiredSpec.freq = SAMPLE_RATE;
    desiredSpec.format = AUDIO_S16SYS; // 16-bit firmati nativi del sistema
    desiredSpec.channels = 1;          // Mono
    desiredSpec.samples = 512;         // Buffer piccolo = bassa latenza (256 o 512)
    desiredSpec.callback = SDL_Audio_Callback_Wrapper; // La funzione da chiamare
    desiredSpec.userdata = this;       // Passa l'oggetto SID alla callback

    SDL_AudioSpec obtainedSpec;
    audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desiredSpec, &obtainedSpec, 0);

    if (audioDevice == 0) {
        printf("Errore apertura periferica SDL: %s\n", SDL_GetError());
    } else {
        // Avvia la riproduzione audio
        SDL_PauseAudioDevice(audioDevice, 0);
    }

    Reset();
}

SID::~SID() {
    if (audioDevice != 0) {
        SDL_CloseAudioDevice(audioDevice);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void SID::Reset() {
    lock_guard<mutex> lock(sidMutex);
    memset(regs, 0, sizeof(regs));
    for (int i = 0; i < 3; ++i) {
        voices[i] = SIDVoice();
    }
}

void SID::Poke(uint16_t address, uint8_t value) {
    lock_guard<mutex> lock(sidMutex); // Protegge la scrittura dei registri
    uint8_t reg = address & 0x1F;
    regs[reg] = value;

    int v = reg / 7;
    int localReg = reg % 7;

    if (v < 3) {
        switch (localReg) {
        case 0: voices[v].frequency = (voices[v].frequency & 0xFF00) | value; break;
        case 1: voices[v].frequency = (voices[v].frequency & 0x00FF) | ((uint16_t)value << 8); break;
        case 2: voices[v].pulseWidth = (voices[v].pulseWidth & 0xFF00) | value; break;
        case 3: voices[v].pulseWidth = (voices[v].pulseWidth & 0x00FF) | ((uint16_t)value << 8); break;
        case 4: {
            uint8_t oldWave = voices[v].waveControl;
            voices[v].waveControl = value;
            if ((value & 0x01) && !(oldWave & 0x01)) voices[v].adsrState = SIDVoice::ATTACK;
            else if (!(value & 0x01) && (oldWave & 0x01)) voices[v].adsrState = SIDVoice::RELEASE;
            break;
        }
        case 5: voices[v].attackDecay = value; break;
        case 6: voices[v].sustainRelease = value; break;
        }
    }
}

uint8_t SID::Peek(uint16_t address) {
    lock_guard<mutex> lock(sidMutex);
    return regs[address & 0x1F];
}

void SID::Update(int cycles) {
    // Con SDL non serve più accumulare cicli matematici qui!
    // I campioni vengono generati in modalità "Pull" direttamente nella Callback asincrona.
    // Puoi lasciare questa funzione vuota o usarla se in futuro vorrai aggiornare i contatori dei filtri.
}

// QUESTA FUNZIONE GIRA NEL THREAD AUDIO DI SDL
void SID::AudioCallback(uint8_t *stream, int len) {
    lock_guard<mutex> lock(sidMutex); // Evita che la CPU faccia Poke mentre leggiamo

    int16_t *buffer = reinterpret_cast<int16_t*>(stream);
    int samplesCount = len / sizeof(int16_t);

    for (int i = 0; i < samplesCount; ++i) {
        buffer[i] = GenerateNextSample();
    }
}

int16_t SID::GenerateNextSample() {
    double filteredOutput = 0.0;
    double directOutput = 0.0;

    // Volume globale ($D418 bit 0-3)
    double masterVolume = (double)(regs[0x18] & 0x0F) / 15.0;

    // 1. ESTRAZIONE PARAMETRI DEL FILTRO DAI REGISTRI
    // Frequenza di cutoff: unisce i 3 bit bassi di $D415 con gli 8 bit di $D416 (Totale 11 bit: 0-2047)
    uint16_t cutoffReg = ((regs[0x15] & 0x07) | ((uint16_t)regs[0x16] << 3));

    // Mappatura frequenza di cutoff reale del SID 6581 (da ~30Hz a ~12kHz)
    double cutoffHz = 30.0 + ((double)cutoffReg * 5.8);

    // Coefficiente di frequenza del filtro (F)
    double f = 2.0 * sin(M_PI * cutoffHz / (double)SAMPLE_RATE);
    f = clamp(f, 0.0, 1.0); // Sicurezza matematica per evitare auto-oscillazioni distruttive

    // Risonanza ($D417 bit 4-7). Più è alta, più enfatizza la frequenza di taglio
    uint8_t resonanceReg = (regs[0x17] >> 4) & 0x0F;
    // Coefficiente di smorzamento (Q). Il SID mitiga la risonanza all'aumentare del valore
    double q = 1.0 - ((double)resonanceReg / 15.0) * 0.7;

    // Selezione del routing delle voci ($D417 bit 0-2)
    uint8_t routeToFilter = regs[0x17] & 0x07;

    // Selettori di modalità del filtro ($D418 bit 4-6)
    bool lp_enabled = regs[0x18] & 0x10; // Low Pass
    bool bp_enabled = regs[0x18] & 0x20; // Band Pass
    bool hp_enabled = regs[0x18] & 0x40; // High Pass
    bool v3_off     = regs[0x18] & 0x80; // Disconnette voce 3 se 1

    // 2. GENERAZIONE DELLE 3 VOCI ED INSTRADAMENTO (ROUTING)
    for (int i = 0; i < 3; ++i) {
        // Salta la voce 3 se il bit 7 di $D418 la disattiva dal mixer globale
        if (i == 2 && v3_off) continue;

        SIDVoice& v = voices[i];
        if (v.adsrState == SIDVoice::IDLE) continue;

        // Generazione della forma d'onda pura (identica al tuo codice attuale)
        double voiceFreq = ((double)v.frequency * 985248.0) / 16777216.0;
        v.phase += voiceFreq / (double)SAMPLE_RATE;
        if (v.phase >= 1.0) v.phase -= 1.0;

        double sampleValue = 0.0;
        uint8_t wave = v.waveControl & 0xF0;

        if (wave & 0x10) sampleValue = (v.phase < 0.5) ? (v.phase * 4.0 - 1.0) : (3.0 - v.phase * 4.0);
        else if (wave & 0x20) sampleValue = (v.phase * 2.0) - 1.0;
        else if (wave & 0x40) {
            double dutyCycle = (double)(v.pulseWidth & 0x0FFF) / 4095.0;
            if (dutyCycle < 0.01 || dutyCycle > 0.99) dutyCycle = 0.5;
            sampleValue = (v.phase < dutyCycle) ? 0.5 : -0.5;
        }
        else if (wave & 0x80) sampleValue = ((double)rand() / (double)RAND_MAX) * 2.0 - 1.0;

        UpdateADSR(i);
        double voiceSample = sampleValue * v.envLevel;

        // Controllo instradamento: la voce va filtrata o va diretta all'uscita?
        if (routeToFilter & (1 << i)) {
            filteredOutput += voiceSample; // Accumula nel blocco da filtrare
        } else {
            directOutput += voiceSample;   // Accumula nel blocco non filtrato (bypass)
        }
    }

    // 3. APPLICAZIONE DEL FILTRO AD OGNI CAMPIONE (Algoritmo di Chamberlin)
    // Calcola l'uscita High-Pass partendo dagli stati precedenti e dall'ingresso attuale
    double f_high = filteredOutput - f_low - (q * f_band);

    // Aggiorna lo stato Band-Pass integrando la componente High-Pass
    f_band += f * f_high;

    // Aggiorna lo stato Low-Pass integrando la componente Band-Pass
    f_low += f * f_band;

    // Sceglie quale componente del filtro far passare in base ai bit del registro $D418
    double filterComponentResult = 0.0;
    if (lp_enabled) filterComponentResult += f_low;
    if (bp_enabled) filterComponentResult += f_band;
    if (hp_enabled) filterComponentResult += f_high;

    // Se nessun filtro è attivo ma le voci erano instradate lì, passano non filtrate (Bypass parziale)
    if (!lp_enabled && !bp_enabled && !hp_enabled) {
        filterComponentResult = filteredOutput;
    }

    // 4. MIX FINALE E APPLICAZIONE DEL VOLUME MASTER
    // Unisce il segnale che è passato dal filtro con quello che lo ha saltato direttamente
    double totalMix = filterComponentResult + directOutput;

    // Normalizzazione sui 3 canali applicando il volume master
    double finalSample = (totalMix / 3.0) * masterVolume * 32767.0;

    return (int16_t)clamp(finalSample, -32768.0, 32767.0);
}


void SID::UpdateADSR(int vIdx) {
    SIDVoice& v = voices[vIdx];

    // Tabelle hardware dei tempi del SID 6581 (convertiti in secondi per passi a 44100Hz)
    // Valori da 0 a 15 ($0 a $F) esprimono la durata totale di quella fase
    const double attackTimes[16] = {
        0.002, 0.008, 0.016, 0.024, 0.038, 0.056, 0.068, 0.080,
        0.100, 0.250, 0.500, 0.800, 1.000, 3.000, 5.000, 8.000
    };

    const double decayReleaseTimes[16] = {
        0.006, 0.024, 0.048, 0.072, 0.114, 0.168, 0.204, 0.240,
        0.300, 0.750, 1.500, 2.400, 3.000, 9.000, 15.00, 24.00
    };

    uint8_t attack = (v.attackDecay >> 4) & 0x0F;
    uint8_t decay = v.attackDecay & 0x0F;
    uint8_t sustain = (v.sustainRelease >> 4) & 0x0F;
    uint8_t release = v.sustainRelease & 0x0F;

    double sustainLevel = (double)sustain / 15.0;

    switch (v.adsrState) {
    case SIDVoice::ATTACK: {
        // Calcola quanto incrementare a ogni singolo campione a 44100Hz
        // per coprire l'intera salita (da 0.0 a 1.0) nel tempo richiesto
        double attackSamples = attackTimes[attack] * (double)SAMPLE_RATE;
        v.envLevel += 1.0 / attackSamples;

        if (v.envLevel >= 1.0) {
            v.envLevel = 1.0;
            v.adsrState = SIDVoice::DECAY; // Passa automaticamente al Decay
        }
        break;
    }
    case SIDVoice::DECAY: {
        double decaySamples = decayReleaseTimes[decay] * (double)SAMPLE_RATE;

        // Emulazione dell'approssimazione esponenziale hardware:
        // più il volume scende, più rallenta la discesa verso il livello di sustain.
        // Sottraiamo una frazione proporzionale allo scarto attuale.
        double targetDrop = (v.envLevel - sustainLevel);
        if (targetDrop > 0.0) {
            v.envLevel -= targetDrop * (5.0 / decaySamples);
            if (v.envLevel <= sustainLevel) {
                v.envLevel = sustainLevel;
                v.adsrState = SIDVoice::SUSTAIN;
            }
        } else {
            v.envLevel = sustainLevel;
            v.adsrState = SIDVoice::SUSTAIN;
        }
        break;
    }
    case SIDVoice::SUSTAIN:
        // Il Sustain è un LIVELLO fisso. Resta bloccato qui finché la CPU non spegne il Gate
        v.envLevel = sustainLevel;
        break;

    case SIDVoice::RELEASE: {
        double releaseSamples = decayReleaseTimes[release] * (double)SAMPLE_RATE;

        // Anche il release decade in modo pseudo-esponenziale verso lo zero
        if (v.envLevel > 0.001) {
            v.envLevel -= v.envLevel * (5.0 / releaseSamples);
        } else {
            v.envLevel = 0.0;
            v.adsrState = SIDVoice::IDLE; // Spegnimento totale, l'oscillatore tace
        }
        break;
    }
    case SIDVoice::IDLE:
    default:
        v.envLevel = 0.0;
        break;
    }
}

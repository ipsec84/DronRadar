#ifndef DRONE_CLASSIFIER_H
#define DRONE_CLASSIFIER_H

#include <Arduino.h>

enum DroneCategory {
    DRONE_NONE = 0,
    DRONE_HEAVY = 1,       // 100 - 170 Hz: Dron de carga, fumigación, octocóptero
    DRONE_COMMERCIAL = 2,  // 180 - 275 Hz: DJI Mini, Mavic, Phantom, Autel
    DRONE_FPV = 3,         // 280 - 550 Hz: FPV Racing / Alta velocidad
    DRONE_UNKNOWN = 4      // Fuera de rango armónico común
};

enum DopplerStatus {
    DOPPLER_STATIONARY = 0, // Estático / Vuelo estacionario (Hovering)
    DOPPLER_APPROACHING = 1,// Aproximándose (frecuencia aumentando)
    DOPPLER_RECEDING = 2    // Alejándose (frecuencia disminuyendo)
};

class DroneClassifier {
public:
    static DroneCategory classify(float f0_hz) {
        if (f0_hz < 80.0f || f0_hz > 1000.0f) return DRONE_NONE;
        if (f0_hz >= 90.0f && f0_hz < 175.0f) return DRONE_HEAVY;
        if (f0_hz >= 175.0f && f0_hz < 280.0f) return DRONE_COMMERCIAL;
        if (f0_hz >= 280.0f && f0_hz <= 580.0f) return DRONE_FPV;
        return DRONE_UNKNOWN;
    }

    static const char* getCategoryName(DroneCategory cat) {
        switch (cat) {
            case DRONE_HEAVY: return "Dron Pesado / Carga / Octocopter";
            case DRONE_COMMERCIAL: return "DJI Mavic / Mini / Comercial";
            case DRONE_FPV: return "FPV Racing / Alta Velocidad";
            case DRONE_UNKNOWN: return "UAV No Clasificado";
            default: return "Sin Amenaza";
        }
    }

    static const char* getCategoryShort(DroneCategory cat) {
        switch (cat) {
            case DRONE_HEAVY: return "HEAVY-UAV";
            case DRONE_COMMERCIAL: return "DJI/COMMERCIAL";
            case DRONE_FPV: return "FPV-RACER";
            case DRONE_UNKNOWN: return "UAV-UNKNOWN";
            default: return "NONE";
        }
    }
};

class DopplerTracker {
private:
    static const int HISTORY_SIZE = 8;
    float freqHistory[HISTORY_SIZE];
    unsigned long timeHistory[HISTORY_SIZE];
    int count;
    int writeIdx;

public:
    DopplerTracker() : count(0), writeIdx(0) {}

    void reset() {
        count = 0;
        writeIdx = 0;
    }

    void addSample(float f0, unsigned long nowMs) {
        if (f0 < 50.0f) return;
        freqHistory[writeIdx] = f0;
        timeHistory[writeIdx] = nowMs;
        writeIdx = (writeIdx + 1) % HISTORY_SIZE;
        if (count < HISTORY_SIZE) count++;
    }

    DopplerStatus evaluate(float &rateHzPerSec) {
        rateHzPerSec = 0.0f;
        if (count < 4) return DOPPLER_STATIONARY;

        // Regresión lineal simple sobre los últimos puntos
        float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        unsigned long t0 = timeHistory[(writeIdx - count + HISTORY_SIZE) % HISTORY_SIZE];

        for (int i = 0; i < count; i++) {
            int idx = (writeIdx - count + i + HISTORY_SIZE) % HISTORY_SIZE;
            float x = (float)(timeHistory[idx] - t0) / 1000.0f; // Segundos
            float y = freqHistory[idx];
            sumX += x;
            sumY += y;
            sumXY += x * y;
            sumX2 += x * x;
        }

        float denom = (count * sumX2 - sumX * sumX);
        if (fabs(denom) < 0.0001f) return DOPPLER_STATIONARY;

        rateHzPerSec = (count * sumXY - sumX * sumY) / denom;

        // Deriva mayor a +1.5 Hz/s indica aproximación acústica; menor a -1.5 Hz/s alejamiento
        if (rateHzPerSec > 1.5f) return DOPPLER_APPROACHING;
        if (rateHzPerSec < -1.5f) return DOPPLER_RECEDING;
        return DOPPLER_STATIONARY;
    }

    static const char* getStatusName(DopplerStatus st) {
        switch (st) {
            case DOPPLER_APPROACHING: return "Aproximándose ⚠️";
            case DOPPLER_RECEDING: return "Alejándose 🛫";
            default: return "Estático / Hover ⏸️";
        }
    }
};

#endif // DRONE_CLASSIFIER_H

#ifndef DOA_3D_H
#define DOA_3D_H

#include <Arduino.h>
#include <math.h>

enum AltitudeCategory {
    ALTITUDE_LOW = 0,    // 0° - 25°: Rasante / Baja altitud
    ALTITUDE_MEDIUM = 1, // 25° - 60°: Media altitud
    ALTITUDE_ZENITH = 2  // 60° - 90°: Sobrevolando en vertical / Cenital
};

struct DoA3DResult {
    float azimuthDeg;     // 0° - 360° (Norte = 0°, Este = 90°, Sur = 180°, Oeste = 270°)
    float elevationDeg;   // 0° (horizonte) - 90° (cenit / vertical)
    AltitudeCategory altitudeCat;
    bool isValid;
};

class DoA3DEstimator {
public:
    // d = separación entre micrófonos en metros (aprox 0.06m = 6cm en VolAnti)
    // c = velocidad del sonido en el aire (aprox 343 m/s)
    static DoA3DResult calculate(float phaseDiffX, float phaseDiffY, float f0_hz, float micDistanceMeters = 0.06f) {
        DoA3DResult res;
        res.azimuthDeg = 0.0f;
        res.elevationDeg = 0.0f;
        res.altitudeCat = ALTITUDE_LOW;
        res.isValid = false;

        if (f0_hz < 80.0f || f0_hz > 1000.0f) {
            return res;
        }

        // 1. Azimut horizontal
        float azRad = atan2f(phaseDiffY, phaseDiffX);
        float azDeg = azRad * (180.0f / (float)M_PI);
        if (azDeg < 0.0f) azDeg += 360.0f;
        res.azimuthDeg = azDeg;

        // 2. Máxima diferencia de fase teórica para esta frecuencia y distancia
        const float c = 343.0f; // m/s
        float maxPhase = (2.0f * (float)M_PI * f0_hz * micDistanceMeters) / c;
        if (maxPhase < 0.001f) maxPhase = 0.001f;

        // Magnitud del desplazamiento de fase planar
        float r = sqrtf(phaseDiffX * phaseDiffX + phaseDiffY * phaseDiffY);
        float cosElev = r / maxPhase;
        if (cosElev > 1.0f) cosElev = 1.0f;
        if (cosElev < 0.0f) cosElev = 0.0f;

        // Elevación: acos(cosElev) -> 0 rad si cos=1 (rasante 0°), pi/2 si cos=0 (cenit 90°)
        float elevRad = acosf(cosElev);
        float elevDeg = elevRad * (180.0f / (float)M_PI);
        if (elevDeg > 90.0f) elevDeg = 90.0f;
        if (elevDeg < 0.0f) elevDeg = 0.0f;
        res.elevationDeg = elevDeg;

        // 3. Categorización de altitud
        if (elevDeg < 25.0f) {
            res.altitudeCat = ALTITUDE_LOW;
        } else if (elevDeg <= 60.0f) {
            res.altitudeCat = ALTITUDE_MEDIUM;
        } else {
            res.altitudeCat = ALTITUDE_ZENITH;
        }

        res.isValid = true;
        return res;
    }

    static const char* getAltitudeName(AltitudeCategory cat) {
        switch (cat) {
            case ALTITUDE_LOW: return "Baja Altitud / Rasante (<25°)";
            case ALTITUDE_MEDIUM: return "Media Altitud (25°-60°)";
            case ALTITUDE_ZENITH: return "Cenital / Sobre la vertical (>60°)";
            default: return "Desconocida";
        }
    }
};

#endif // DOA_3D_H

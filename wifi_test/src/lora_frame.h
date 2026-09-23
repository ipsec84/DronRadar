#ifndef LORA_FRAME_H
#define LORA_FRAME_H

#include <Arduino.h>

// ============================================================================
// ESTRUCTURA BINARIA COMPACTA LORA (8 BYTES TOTALES)
// Diseñada para máxima penetración y alcance en exteriores con mínimo AirTime
// ============================================================================

#define LORA_MAGIC_BYTE 0xAA

struct __attribute__((packed)) LoRaDronFrame {
    uint8_t magic;       // 0xAA para validación de cabecera
    uint8_t nodeId;      // 1 a 255 (ID numérico del nodo)
    uint8_t statusByte;  // Bits 0-2: Tier (0-4), Bit 3: Doppler (0=Estático, 1=Aproximación/Alejamiento), Bits 4-6: Tipo (0=None, 1=Heavy, 2=DJI, 3=FPV)
    uint8_t azimuthComp; // Azimut comprimido en 0..180 (Azimut Real = azimuthComp * 2)
    uint8_t elevation;   // Elevación vertical 0..90 grados
    uint16_t f0Hz;       // Frecuencia fundamental 0..65535 Hz (Little-Endian)
    uint8_t crc8;        // Verificación de integridad CRC-8 Dallas/Maxim
};

class LoRaFrameBuilder {
public:
    static uint8_t calcCrc8(const uint8_t *data, size_t len) {
        uint8_t crc = 0x00;
        for (size_t i = 0; i < len; i++) {
            crc ^= data[i];
            for (uint8_t j = 0; j < 8; j++) {
                if (crc & 0x80)
                    crc = (crc << 1) ^ 0x07;
                else
                    crc <<= 1;
            }
        }
        return crc;
    }

    static LoRaDronFrame build(uint8_t nodeId, uint8_t tier, uint8_t droneType, uint8_t doppler, float azimuthDeg, float elevationDeg, float f0Hz) {
        LoRaDronFrame frame;
        frame.magic = LORA_MAGIC_BYTE;
        frame.nodeId = nodeId;
        
        // Empaquetar status
        frame.statusByte = (tier & 0x07) | ((doppler & 0x01) << 3) | ((droneType & 0x07) << 4);

        // Azimut en pasos de 2 grados (0 - 180) -> 0° a 360°
        int azInt = (int)roundf(azimuthDeg / 2.0f);
        if (azInt < 0) azInt = 0;
        if (azInt > 180) azInt = 180;
        frame.azimuthComp = (uint8_t)azInt;

        // Elevación 0 - 90
        int elInt = (int)roundf(elevationDeg);
        if (elInt < 0) elInt = 0;
        if (elInt > 90) elInt = 90;
        frame.elevation = (uint8_t)elInt;

        // Frecuencia fundamental en Hz
        int f0Int = (int)roundf(f0Hz);
        if (f0Int < 0) f0Int = 0;
        if (f0Int > 65535) f0Int = 65535;
        frame.f0Hz = (uint16_t)f0Int;

        // Calcular CRC-8 sobre los primeros 7 bytes
        frame.crc8 = calcCrc8((const uint8_t*)&frame, sizeof(LoRaDronFrame) - 1);

        return frame;
    }

    static bool verify(const LoRaDronFrame& frame) {
        if (frame.magic != LORA_MAGIC_BYTE) return false;
        uint8_t computedCrc = calcCrc8((const uint8_t*)&frame, sizeof(LoRaDronFrame) - 1);
        return (computedCrc == frame.crc8);
    }
};

// Driver de abstracción para transceptor LoRa físico (SX1262 / SX1276 / RFM95)
class LoRaTransceiverStub {
private:
    bool hardwarePresent;

public:
    LoRaTransceiverStub() : hardwarePresent(false) {}

    bool begin(int csPin = 10, int rstPin = 14, int dioPin = 13) {
        // Listo para conectar con RadioLib o LoRa library cuando el hardware esté soldado
        Serial.printf("[LORA] Interfaz de radio preparada en pines CS:%d RST:%d DIO:%d (Listo para SX1262/SX1276)\n", csPin, rstPin, dioPin);
        hardwarePresent = false;
        return true;
    }

    bool isReady() const {
        return hardwarePresent;
    }

    bool transmitFrame(const LoRaDronFrame& frame) {
        if (!hardwarePresent) {
            // Log de telemetría LoRa lista para emitir
            // Serial.printf("[LORA-TX] Frame listo: Node=%d, Tier=%d, Az=%d deg, Elev=%d deg, F0=%d Hz, CRC=0x%02X\n",
            //              frame.nodeId, frame.statusByte & 0x07, frame.azimuthComp * 2, frame.elevation, frame.f0Hz, frame.crc8);
            return true;
        }
        return false;
    }
};

#endif // LORA_FRAME_H

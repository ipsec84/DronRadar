#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <soc/gpio_periph.h>
#include <soc/io_mux_reg.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <LittleFS.h>

extern "C" {
#include "detector.h"
}

#include "wifi_credentials.h"

// ============================================================================
// CONFIGURACIÓN DE PINES HARDWARE (VolAnti / DevKitC-1)
// ============================================================================
#define PIN_I2S_BCLK_A     4    // SCK Bus A (M1/M2)
#define PIN_I2S_WS_A       5    // WS Bus A (M1/M2)
#define PIN_I2S_SD_A       6    // SD Bus A: Micrófono 1 (Left) y Micrófono 2 (Right)

#define PIN_I2S_BCLK_B     7    // SCK Bus B (M3/M4)
#define PIN_I2S_WS_B       8    // WS Bus B (M3/M4)
#define PIN_I2S_SD_B       9    // SD Bus B: Micrófono 3 (Left) y Micrófono 4 (Right)

#define PIN_LED_VOLANTI    16   // LED WS2812 PCB VolAnti
#define PIN_LED_DEVKIT_48  48   // LED RGB DevKit v1.0
#define PIN_LED_DEVKIT_38  38   // LED RGB DevKit v1.1
#define PIN_LED_DISCRETE   2    // LED discreto / placa

#define SAMPLE_RATE        16000
#define I2S_BUFFER_LEN     256

// Instancias globales
WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Preferences prefs;

// Estructura de configuración NVS
struct Config {
    char mqtt_broker[64];
    int mqtt_port;
    char mqtt_user[32];
    char mqtt_pass[32];
    char topic_prefix[32];
} appConfig;

// Variables de estado de audio / micrófonos
struct MicMetrics {
    float rms[4];
    int16_t peak[4];
    bool busA_active;
    bool busB_active;
    unsigned long sample_count;
} micMetrics;

portMUX_TYPE micMux = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t historyMutex = NULL;

// Estado del detector y alertas
enum DetectorState {
    STATE_LISTENING = 0,
    STATE_ALERT_T1 = 1,
    STATE_ALERT_T2 = 2,
    STATE_ALERT_T3 = 3,
    STATE_ALERT_T4 = 4,
    STATE_SNOOZED  = 5
};

DetectorState currentState = STATE_LISTENING;
float currentScore = 0.0f;
float currentFreqHz = 0.0f;
float currentThreshold = 1.20f; // Calibrado óptimo: ruido máx ~1.05, dron ~1.40-1.95
int currentTier = 0;
unsigned long alertStartTime = 0;
unsigned long snoozeUntil = 0;
unsigned long lastMqttReport = 0;
unsigned long lastConsoleReport = 0;
bool wifiConnected = false;

// Evaluador de persistencia temporal (Confidence Integrator)
float currentConfidence = 0.0f; // 0.0 a 1.0 (0% a 100%)
unsigned long lastEvidenceTime = 0;

// Dirección de llegada acústica (DoA 360°)
float currentAzimuth = 0.0f;
bool azimuthValid = false;
#define DOA_SAMPLES 256
static int16_t doa_buf_m1[DOA_SAMPLES];
static int16_t doa_buf_m2[DOA_SAMPLES];
static int16_t doa_buf_m3[DOA_SAMPLES];
static int16_t doa_buf_m4[DOA_SAMPLES];
static int doa_idx = 0;

// Espectrograma en Cascada (Waterfall)
#define WATERFALL_BINS 48
static uint8_t currentSpectrum[WATERFALL_BINS];

// Umbral Adaptativo Inteligente
bool autoThresholdEnabled = false;
float ambientNoiseFloor = 0.70f;

// Variables del algoritmo detector VolAnti
static detector_state_t g_detector_state;
static detector_work_t g_detector_work;
static float g_analysis_block[CFG_N_FFT];
static float g_hop_buf[CFG_HOP];
static int g_hop_fill = 0;
static uint32_t g_frame_counter = 0;
static uint16_t g_current_teeth = 0;
static int32_t g_current_chain = 0;
static bool g_detector_ready = false;

// ============================================================================
// CLASIFICADOR Y REGISTRO HISTÓRICO DE DRONES
// ============================================================================
struct DroneEvent {
    char time_str[16];
    float peak_score;
    float peak_freq;
    uint16_t teeth;
    uint32_t duration_sec;
    char category[50];
};

#define MAX_EVENTS 30
static DroneEvent eventHistory[MAX_EVENTS];
static int eventCount = 0;
static bool eventInProgress = false;
static unsigned long currentEventStart = 0;
static float currentEventMaxScore = 0.0f;
static float currentEventFreq = 0.0f;
static uint16_t currentEventTeeth = 0;

const char* classifyDrone(float f0_hz) {
    if (f0_hz >= 500.0f) {
        return "Clase 1: FPV / Alta Velocidad (Racing/Freestyle)";
    } else if (f0_hz >= 300.0f) {
        return "Clase 2: Micro Dron Ligero (<250g, tipo DJI Mini)";
    } else if (f0_hz >= 160.0f) {
        return "Clase 3: Cuadricóptero Mediano (tipo DJI Mavic/Air)";
    } else if (f0_hz >= 70.0f) {
        return "Clase 4: Dron Pesado / Industrial (tipo Matrice/Carga)";
    } else {
        return "Desconocido (Baja Frecuencia)";
    }
}

static bool g_time_synced = false;

void formatUptime(unsigned long ms, char *buf, size_t len) {
    unsigned long total_sec = ms / 1000;
    unsigned int sec = total_sec % 60;
    unsigned int min = (total_sec / 60) % 60;
    unsigned int hr = (total_sec / 3600);
    snprintf(buf, len, "%02u:%02u:%02u", hr, min, sec);
}

void setNodeTime(time_t epoch_sec, long tz_offset_sec) {
    time_t local_sec = epoch_sec + tz_offset_sec;
    struct timeval tv = { .tv_sec = local_sec, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    g_time_synced = true;

    char timeStr[32];
    struct tm timeinfo;
    localtime_r(&local_sec, &timeinfo);
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    Serial.printf("\n[RELOJ] Hora del sistema sincronizada exitosamente: %s\n", timeStr);
}

void getNodeTimeString(char *buf, size_t len) {
    if (g_time_synced) {
        time_t now = time(NULL);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        strftime(buf, len, "%H:%M:%S", &timeinfo);
    } else {
        formatUptime(millis(), buf, len);
    }
}

void formatEventTime(unsigned long eventStartMs, char *buf, size_t len) {
    if (g_time_synced) {
        time_t now_sec = time(NULL);
        unsigned long elapsed_sec = (millis() - eventStartMs) / 1000;
        time_t ev_sec = (now_sec >= elapsed_sec) ? (now_sec - elapsed_sec) : now_sec;
        struct tm timeinfo;
        localtime_r(&ev_sec, &timeinfo);
        strftime(buf, len, "%H:%M:%S", &timeinfo);
    } else {
        formatUptime(eventStartMs, buf, len);
    }
}

void recordEventEnd() {
    if (!eventInProgress) return;
    eventInProgress = false;

    uint32_t dur = (millis() - currentEventStart) / 1000;
    if (dur < 1) dur = 1;

    char cat[50];
    strncpy(cat, classifyDrone(currentEventFreq), sizeof(cat) - 1);
    cat[sizeof(cat) - 1] = '\0';

    if (historyMutex && xSemaphoreTake(historyMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Desplazar histórico hacia abajo (más reciente arriba)
        int shift_limit = (eventCount < MAX_EVENTS) ? eventCount : (MAX_EVENTS - 1);
        for (int i = shift_limit; i > 0; i--) {
            eventHistory[i] = eventHistory[i - 1];
        }

        formatEventTime(currentEventStart, eventHistory[0].time_str, sizeof(eventHistory[0].time_str));
        eventHistory[0].peak_score = currentEventMaxScore;
        eventHistory[0].peak_freq = currentEventFreq;
        eventHistory[0].teeth = currentEventTeeth;
        eventHistory[0].duration_sec = dur;
        strncpy(eventHistory[0].category, cat, sizeof(eventHistory[0].category) - 1);
        eventHistory[0].category[sizeof(eventHistory[0].category) - 1] = '\0';

        if (eventCount < MAX_EVENTS) {
            eventCount++;
        }

        // Guardar persistentemente en Flash LittleFS (Caja Negra)
        File f = LittleFS.open("/history.csv", FILE_APPEND);
        if (f) {
            f.printf("%s,\"%s\",%.1f,%.2f,%d,%lu\n",
                     eventHistory[0].time_str,
                     cat,
                     currentEventFreq,
                     currentEventMaxScore,
                     currentEventTeeth,
                     (unsigned long)dur);
            f.close();
        }

        xSemaphoreGive(historyMutex);
    }

    Serial.printf("\n[HISTORIAL] Incursión registrada -> %s | Freq: %.1f Hz | Score: %.2f | Dur: %lu s\n",
                  cat, currentEventFreq, currentEventMaxScore, (unsigned long)dur);
}

void loadHistoryFromFlash() {
    if (!LittleFS.exists("/history.csv")) {
        File f = LittleFS.open("/history.csv", FILE_WRITE);
        if (f) {
            f.println("Hora,Clasificacion,Frecuencia_Hz,Score_Max,Armonicos,Duracion_s");
            f.close();
        }
        return;
    }
    File f = LittleFS.open("/history.csv", FILE_READ);
    if (!f) return;
    String header = f.readStringUntil('\n'); // saltar cabecera
    while (f.available() && eventCount < MAX_EVENTS) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() < 5) continue;
        int c1 = line.indexOf(',');
        int q1 = line.indexOf('"', c1);
        int q2 = line.indexOf('"', q1 + 1);
        int c2 = line.indexOf(',', q2);
        int c3 = line.indexOf(',', c2 + 1);
        int c4 = line.indexOf(',', c3 + 1);
        int c5 = line.indexOf(',', c4 + 1);
        if (c1 > 0 && q1 > 0 && q2 > q1 && c2 > 0) {
            for (int i = eventCount; i > 0; i--) {
                eventHistory[i] = eventHistory[i - 1];
            }
            strncpy(eventHistory[0].time_str, line.substring(0, c1).c_str(), sizeof(eventHistory[0].time_str) - 1);
            strncpy(eventHistory[0].category, line.substring(q1 + 1, q2).c_str(), sizeof(eventHistory[0].category) - 1);
            eventHistory[0].peak_freq = line.substring(c2 + 1, c3).toFloat();
            eventHistory[0].peak_score = line.substring(c3 + 1, c4).toFloat();
            eventHistory[0].teeth = line.substring(c4 + 1, c5).toInt();
            eventHistory[0].duration_sec = line.substring(c5 + 1).toInt();
            eventCount++;
        }
    }
    f.close();
    Serial.printf("[CAJA NEGRA] Se restauraron %d eventos previos desde LittleFS.\n", eventCount);
}

// Variables de parpadeo del LED
unsigned long lastLedBlink = 0;
bool ledBlinkState = false;

// ============================================================================
// CONTROL DE LEDS
// ============================================================================
void updateLedHardware(bool on, uint8_t r = 0, uint8_t g = 180, uint8_t b = 20) {
    if (!on) {
        r = 0; g = 0; b = 0;
    }
    #ifdef RGB_BUILTIN
    neopixelWrite(RGB_BUILTIN, r, g, b);
    #endif
    neopixelWrite(PIN_LED_DEVKIT_48, r, g, b);
    neopixelWrite(PIN_LED_DEVKIT_38, r, g, b);
    neopixelWrite(PIN_LED_VOLANTI, r, g, b);
    digitalWrite(PIN_LED_DISCRETE, on ? HIGH : LOW);
}

void handleLedStatus() {
    unsigned long now = millis();
    if (currentState == STATE_ALERT_T1 || currentState == STATE_ALERT_T2) {
        if (now - lastLedBlink >= 100) {
            lastLedBlink = now;
            ledBlinkState = !ledBlinkState;
            updateLedHardware(ledBlinkState, 255, 0, 0); // Rojo rápido
        }
    } else if (currentState == STATE_ALERT_T3 || currentState == STATE_ALERT_T4) {
        updateLedHardware(true, 255, 140, 0); // Amarillo fijo
    } else if (currentState == STATE_SNOOZED) {
        updateLedHardware(false);
    } else {
        // En escucha: parpadeo suave verde cada segundo
        if (now - lastLedBlink >= 700) {
            lastLedBlink = now;
            ledBlinkState = !ledBlinkState;
            updateLedHardware(ledBlinkState, 0, 150, 20);
        }
    }
}

// ============================================================================
// DRIVER I2S PARA 4 MICRÓFONOS (INMP441)
// ============================================================================
bool initI2SMicrophones() {
    Serial.println("\n[I2S] Inicializando array de 4 micrófonos (Bus A + Bus B independientes)...");

    // 1. Bus A (M1 Delantero Izq / M2 Delantero Der): I2S0 MASTER
    // SCK: GPIO 4, WS: GPIO 5, SD: GPIO 6
    i2s_config_t i2s_config_a = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = I2S_BUFFER_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config_a = {
        .bck_io_num = PIN_I2S_BCLK_A,
        .ws_io_num = PIN_I2S_WS_A,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_SD_A
    };

    esp_err_t err_a = i2s_driver_install(I2S_NUM_0, &i2s_config_a, 0, NULL);
    if (err_a == ESP_OK) {
        i2s_set_pin(I2S_NUM_0, &pin_config_a);
        i2s_start(I2S_NUM_0);
        Serial.println("[I2S OK] Bus A (M1/M2 en SD=GPIO6, SCK=4, WS=5) ACTIVO.");
    } else {
        Serial.printf("[I2S ERROR] Fallo driver I2S0: %d\n", err_a);
    }

    // 2. Bus B (M3 Trasero Izq / M4 Trasero Der): I2S1 MASTER
    // SCK: GPIO 7, WS: GPIO 8, SD: GPIO 9
    i2s_config_t i2s_config_b = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = I2S_BUFFER_LEN,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config_b = {
        .bck_io_num = PIN_I2S_BCLK_B,
        .ws_io_num = PIN_I2S_WS_B,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_SD_B
    };

    esp_err_t err_b = i2s_driver_install(I2S_NUM_1, &i2s_config_b, 0, NULL);
    if (err_b == ESP_OK) {
        i2s_set_pin(I2S_NUM_1, &pin_config_b);
        i2s_start(I2S_NUM_1);
        Serial.println("[I2S OK] Bus B (M3/M4 en SD=GPIO9, SCK=7, WS=8) ACTIVO.");
    } else {
        Serial.printf("[I2S ERROR] Fallo driver I2S1: %d\n", err_b);
    }

    return (err_a == ESP_OK && err_b == ESP_OK);
}

// Tarea en segundo plano para procesar audio en tiempo real y ejecutar el detector de dron
void micAudioTask(void *pvParameters) {
    static int32_t buf_a[I2S_BUFFER_LEN * 2];
    static int32_t buf_b[I2S_BUFFER_LEN * 2];

    while (true) {
        size_t bytes_a = 0;
        size_t bytes_b = 0;

        esp_err_t res_a = i2s_read(I2S_NUM_0, buf_a, sizeof(buf_a), &bytes_a, pdMS_TO_TICKS(50));
        esp_err_t res_b = i2s_read(I2S_NUM_1, buf_b, sizeof(buf_b), &bytes_b, pdMS_TO_TICKS(50));

        int samples_a = bytes_a / (2 * sizeof(int32_t));
        int samples_b = bytes_b / (2 * sizeof(int32_t));

        int64_sum_sq:
        int64_t sum_sq[4] = {0, 0, 0, 0};
        int16_t local_peak[4] = {0, 0, 0, 0};

        int max_samples = (samples_a > samples_b) ? samples_a : samples_b;

        for (int i = 0; i < max_samples; i++) {
            int16_t s1 = 0, s2 = 0, s3 = 0, s4 = 0;

            if (i < samples_a) {
                s1 = (int16_t)(buf_a[2 * i] >> 16);
                s2 = (int16_t)(buf_a[2 * i + 1] >> 16);
                sum_sq[0] += (int64_t)s1 * s1;
                sum_sq[1] += (int64_t)s2 * s2;
                int16_t a1 = abs(s1);
                int16_t a2 = abs(s2);
                if (a1 > local_peak[0]) local_peak[0] = a1;
                if (a2 > local_peak[1]) local_peak[1] = a2;
            }

            if (i < samples_b) {
                s3 = (int16_t)(buf_b[2 * i] >> 16);
                s4 = (int16_t)(buf_b[2 * i + 1] >> 16);
                sum_sq[2] += (int64_t)s3 * s3;
                sum_sq[3] += (int64_t)s4 * s4;
                int16_t a3 = abs(s3);
                int16_t a4 = abs(s4);
                if (a3 > local_peak[2]) local_peak[2] = a3;
                if (a4 > local_peak[3]) local_peak[3] = a4;
            }

            // Guardar muestras en ring buffer de DoA
            doa_buf_m1[doa_idx] = s1;
            doa_buf_m2[doa_idx] = s2;
            doa_buf_m3[doa_idx] = s3;
            doa_buf_m4[doa_idx] = s4;
            doa_idx = (doa_idx + 1) % DOA_SAMPLES;

            // Promedio coherente de los 4 micrófonos escalado a [-1.0, +1.0]
            int32_t mixed = ((int32_t)s1 + (int32_t)s2 + (int32_t)s3 + (int32_t)s4) / 4;
            if (g_detector_ready && g_hop_fill < CFG_HOP) {
                g_hop_buf[g_hop_fill++] = (float)mixed / 32768.0f;
            }

            // Cuando acumulamos un salto completo (512 muestras = 32ms)
            if (g_detector_ready && g_hop_fill >= CFG_HOP) {
                g_hop_fill = 0;

                // Desplazar ventana de análisis FFT (2048 muestras)
                memmove(g_analysis_block, g_analysis_block + CFG_HOP, (CFG_N_FFT - CFG_HOP) * sizeof(float));
                memcpy(g_analysis_block + (CFG_N_FFT - CFG_HOP), g_hop_buf, CFG_HOP * sizeof(float));

                // 1. FFT Front-end
                front_end(g_analysis_block, g_detector_work.spec[0], &g_detector_work);

                // 2. Combiner
                const cf32_t *sp = combiner(&g_detector_work.spec[0][0], 1, &g_detector_work);

                // 3. Back-end detector armónico de peine
                double t = (double)(g_frame_counter * CFG_HOP + CFG_N_FFT) / (double)CFG_FS;
                frame_rec_t rec;
                back_end(sp, &g_detector_state, t, g_frame_counter++, &g_detector_work, &rec);

                // Extraer 48 bins de espectro para Waterfall (de ~78 Hz a ~2350 Hz)
                for (int b = 0; b < WATERFALL_BINS; b++) {
                    int bin_idx = 10 + b * 6;
                    if (bin_idx < CFG_N_BINS) {
                        int byte_val = (int)(g_detector_work.mag[bin_idx] * 65.0f);
                        if (byte_val < 0) byte_val = 0;
                        if (byte_val > 255) byte_val = 255;
                        currentSpectrum[b] = (uint8_t)byte_val;
                    }
                }

                // Cálculo de Dirección de Llegada 360° (DoA) por fase cruzada en f0
                if (rec.f0_hz > 60.0 && rec.f0_hz < 2200.0 && rec.chain >= 2) {
                    float omega = 2.0f * (float)M_PI * (float)rec.f0_hz / 16000.0f;
                    float re1 = 0, im1 = 0, re2 = 0, im2 = 0, re3 = 0, im3 = 0, re4 = 0, im4 = 0;
                    for (int n = 0; n < DOA_SAMPLES; n++) {
                        int idx = (doa_idx + n) % DOA_SAMPLES;
                        float cos_w = cosf(omega * n);
                        float sin_w = sinf(omega * n);
                        re1 += (float)doa_buf_m1[idx] * cos_w;
                        im1 -= (float)doa_buf_m1[idx] * sin_w;
                        re2 += (float)doa_buf_m2[idx] * cos_w;
                        im2 -= (float)doa_buf_m2[idx] * sin_w;
                        re3 += (float)doa_buf_m3[idx] * cos_w;
                        im3 -= (float)doa_buf_m3[idx] * sin_w;
                        re4 += (float)doa_buf_m4[idx] * cos_w;
                        im4 -= (float)doa_buf_m4[idx] * sin_w;
                    }
                    float cross_x_re = re1 * re2 + im1 * im2;
                    float cross_x_im = im1 * re2 - re1 * im2;
                    float phase_x = atan2f(cross_x_im, cross_x_re);

                    float cross_y_re = re3 * re4 + im3 * im4;
                    float cross_y_im = im3 * re4 - re3 * im4;
                    float phase_y = atan2f(cross_y_im, cross_y_re);

                    float az_rad = atan2f(phase_x, phase_y);
                    float az_deg = az_rad * (180.0f / (float)M_PI);
                    if (az_deg < 0.0f) az_deg += 360.0f;

                    float diff = az_deg - currentAzimuth;
                    while (diff < -180.0f) diff += 360.0f;
                    while (diff > 180.0f) diff -= 360.0f;
                    currentAzimuth = fmodf(currentAzimuth + 0.20f * diff + 360.0f, 360.0f);
                    azimuthValid = true;
                }

                bool shouldEndEvent = false;
                portENTER_CRITICAL(&micMux);
                currentScore = rec.score;
                currentFreqHz = (float)rec.f0_hz;
                g_current_teeth = rec.teeth;
                g_current_chain = rec.chain;

                // ====================================================================
                // EVALUADOR DE PERSISTENCIA TEMPORAL (CONFIDENCE INTEGRATOR)
                // Evita saltos recurrentes: acumula certeza antes de disparar la alarma.
                // ====================================================================
                bool hasEvidence = (rec.fired || rec.chain >= 3 || (rec.chain >= 2 && rec.score >= currentThreshold * 0.90f));

                if (hasEvidence) {
                    lastEvidenceTime = millis();
                    float boost = (rec.chain >= 3) ? 0.20f : 0.12f;
                    if (rec.score > currentThreshold * 1.20f) boost += 0.06f;
                    currentConfidence = min(1.0f, currentConfidence + boost);
                } else {
                    // Fuga lenta controlada (decay de ~30 frames = 1s para caer a 0)
                    currentConfidence = max(0.0f, currentConfidence - 0.035f);
                }

                // Seguimiento adaptativo del suelo de ruido ambiental
                if (currentConfidence < 0.10f && rec.chain == 0) {
                    ambientNoiseFloor = 0.995f * ambientNoiseFloor + 0.005f * rec.score;
                    if (autoThresholdEnabled) {
                        currentThreshold = ambientNoiseFloor * 1.35f;
                        if (currentThreshold < 1.05f) currentThreshold = 1.05f;
                        if (currentThreshold > 1.80f) currentThreshold = 1.80f;
                        g_detector_state.thr = currentThreshold;
                    }
                }

                // Transición ponderada de estados:
                if (currentConfidence >= 0.65f) {
                    currentState = STATE_ALERT_T1; // 🔴 DRON LOCALIZADO (Confirmado)
                    currentTier = 1;
                    alertStartTime = millis();

                    if (!eventInProgress) {
                        eventInProgress = true;
                        currentEventStart = alertStartTime;
                        currentEventMaxScore = rec.score;
                        currentEventFreq = rec.f0_hz;
                        currentEventTeeth = rec.teeth;
                    } else {
                        if (rec.score > currentEventMaxScore) {
                            currentEventMaxScore = rec.score;
                            currentEventFreq = rec.f0_hz;
                            currentEventTeeth = rec.teeth;
                        }
                    }
                } else if (currentConfidence >= 0.30f) {
                    if (currentState != STATE_ALERT_T1) {
                        currentState = STATE_ALERT_T3; // 🟡 DETECTANDO / EVALUANDO
                        currentTier = 3;
                    }
                    alertStartTime = millis();
                } else {
                    // Retorno a Verde solo si la confianza es baja y hay silencio continuo de 1.5s
                    if (currentState != STATE_LISTENING && (millis() - lastEvidenceTime > 1500)) {
                        currentState = STATE_LISTENING; // 🟢 DESPEJADO
                        currentTier = 0;
                        azimuthValid = false;
                        shouldEndEvent = true;
                    }
                }
                portEXIT_CRITICAL(&micMux);

                if (shouldEndEvent) {
                    recordEventEnd();
                }
            }
        }

        portENTER_CRITICAL(&micMux);
        if (samples_a > 0) {
            micMetrics.rms[0] = sqrtf((float)sum_sq[0] / samples_a);
            micMetrics.rms[1] = sqrtf((float)sum_sq[1] / samples_a);
            micMetrics.peak[0] = local_peak[0];
            micMetrics.peak[1] = local_peak[1];
            micMetrics.busA_active = true;
        }
        if (samples_b > 0) {
            micMetrics.rms[2] = sqrtf((float)sum_sq[2] / samples_b);
            micMetrics.rms[3] = sqrtf((float)sum_sq[3] / samples_b);
            micMetrics.peak[2] = local_peak[2];
            micMetrics.peak[3] = local_peak[3];
            micMetrics.busB_active = true;
        }
        micMetrics.sample_count++;
        portEXIT_CRITICAL(&micMux);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Barra visual ASCII para vúmetro en consola
void printVuMeter(const char* label, float rms, int16_t peak) {
    const int max_bars = 10;
    int bars = (int)(rms / 50.0f);
    if (bars > max_bars) bars = max_bars;
    char bar_str[max_bars + 1];
    for (int i = 0; i < max_bars; i++) {
        bar_str[i] = (i < bars) ? '#' : '.';
    }
    bar_str[max_bars] = '\0';
    Serial.printf("%s:[%s] RMS:%5.0f PK:%5d  ", label, bar_str, rms, peak);
}

// Prototipos Web y MQTT
void setupRoutes();
void handleRoot();
void handleStatus();
void reconnectMQTT();
void publishState();

// UI Dashboard Web con Vúmetro en tiempo real y Telemetría del Detector
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>DronRadar VolAnti - Detector Acústico de Drones</title>
<style>
  :root {
    --bg: #0d1117;
    --card: #161b22;
    --border: #30363d;
    --accent: #00ff66;
    --alert: #ff3344;
    --warn: #ffcc00;
    --text: #c9d1d9;
    --text-bright: #f0f6fc;
  }
  body {
    margin: 0;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    background: var(--bg);
    color: var(--text);
    padding: 16px;
  }
  .container { max-width: 900px; margin: 0 auto; }
  header {
    display: flex; justify-content: space-between; align-items: center;
    border-bottom: 1px solid var(--border); padding-bottom: 12px; margin-bottom: 20px;
  }
  h1 { margin: 0; color: var(--text-bright); font-size: 1.4rem; }
  .badge {
    padding: 4px 10px; border-radius: 12px; font-weight: bold; font-size: 0.85rem;
  }
  .badge-online { background: #00ff6622; color: var(--accent); border: 1px solid var(--accent); }
  .badge-offline { background: #ffaa0022; color: #ffaa00; border: 1px solid #ffaa00; }
  .grid {
    display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 20px;
  }
  .card {
    background: var(--card); border: 1px solid var(--border); border-radius: 8px; padding: 16px;
  }
  .card h3 { margin: 0 0 10px 0; font-size: 0.95rem; color: #8b949e; text-transform: uppercase; }
  .val { font-size: 1.8rem; font-weight: bold; color: var(--text-bright); }
  .unit { font-size: 0.9rem; color: #8b949e; margin-left: 4px; }
  .meter-bg {
    background: #21262d; border-radius: 6px; height: 16px; width: 100%; margin-top: 10px; overflow: hidden;
  }
  .meter-fill {
    height: 100%; background: linear-gradient(90deg, #00ff66, #ffcc00, #ff3344); width: 0%;
    transition: width 0.1s ease;
  }
  .details { font-size: 0.8rem; color: #8b949e; margin-top: 6px; }
  .status-row { display: flex; justify-content: space-between; padding: 6px 0; border-bottom: 1px solid #21262d; }
  .status-row:last-child { border-bottom: none; }
  
  /* Banner de Estado */
  .banner {
    padding: 18px; border-radius: 8px; margin-bottom: 20px; text-align: center; transition: all 0.3s ease;
  }
  .banner-listening { background: #00ff6615; border: 2px solid #00ff66; color: #00ff66; }
  .banner-detecting { background: #ffaa0025; border: 2px solid #ffaa00; color: #ffaa00; }
  .banner-alert { background: #ff334433; border: 2px solid #ff3344; color: #ff3344; animation: pulse 1s infinite; }
  @keyframes pulse {
    0% { transform: scale(1); opacity: 1; }
    50% { transform: scale(1.02); opacity: 0.88; }
    100% { transform: scale(1); opacity: 1; }
  }
  .btn-row { display: flex; gap: 10px; margin-top: 14px; flex-wrap: wrap; }
  .btn {
    flex: 1; min-width: 140px; padding: 12px; font-weight: bold; border-radius: 6px; cursor: pointer; border: none; font-size: 0.95rem;
    transition: transform 0.1s ease, opacity 0.2s ease;
  }
  .btn:active { transform: scale(0.97); }
  .btn:hover { opacity: 0.9; }
  .btn-green { background: #238636; color: #fff; }
  .btn-yellow { background: #d29922; color: #000; }
  .btn-red { background: #da3633; color: #fff; }
</style>
</head>
<body>
<div class="container">
  <header>
    <div>
      <h1 style="margin: 0; font-size: 1.6rem;">DronRadar VolAnti - Nodo Acústico</h1>
      <div style="font-size: 0.85rem; color: #8b949e; margin-top: 5px; display: flex; align-items: center; gap: 8px; flex-wrap: wrap;">
        <span>Reloj del Sistema: <strong id="clock-display" style="color: #58a6ff; font-family: monospace; font-size: 0.95rem;">--:--:--</strong></span>
        <button onclick="syncBrowserTime()" style="background: #21262d; border: 1px solid #30363d; color: #c9d1d9; border-radius: 4px; padding: 2px 8px; font-size: 0.75rem; cursor: pointer;" title="Sincronizar reloj con este móvil/PC">⏱️ Sincronizar Reloj</button>
      </div>
    </div>
    <span id="net-badge" class="badge badge-offline">Conectando...</span>
  </header>

  <!-- Banner Principal de Localización de Dron -->
  <div id="alert-banner" class="banner banner-listening">
    <div style="font-size: 1.6rem; font-weight: bold;" id="alert-title">🟢 OPERATIVO (NO LOCALIZADO)</div>
    <div style="font-size: 0.95rem; margin-top: 6px;" id="alert-desc">Espacio aéreo despejado. Algoritmo armónico vigilando en silencio.</div>
  </div>

  <!-- Telemetría del Detector Matemático de Drones -->
  <div class="grid">
    <div class="card">
      <h3>Score Armónico Dron</h3>
      <div><span class="val" id="det-score">0.00</span><span class="unit">/ <span id="det-thr">0.95</span></span></div>
      <div class="meter-bg"><div class="meter-fill" id="score-bar" style="width: 0%;"></div></div>
      <div class="details">Umbral Alerta actual: <strong id="det-thr-lbl">0.95</strong></div>
    </div>
    <div class="card">
      <h3>Frecuencia Rotor (f0)</h3>
      <div><span class="val" id="det-freq">0.0</span><span class="unit">Hz</span></div>
      <div class="details">Rango búsqueda: 70 - 2000 Hz</div>
    </div>
    <div class="card">
      <h3>Armónicos (Teeth)</h3>
      <div><span class="val" id="det-teeth">0</span><span class="unit">/ 12</span></div>
      <div class="details">Picos múltiplos armónicos</div>
    </div>
    <div class="card">
      <h3>Seguimiento (Chain)</h3>
      <div><span class="val" id="det-chain">0</span><span class="unit">frames</span></div>
      <div class="details">Frames continuos coherentes (>=3)</div>
    </div>
    <div class="card" style="grid-column: 1 / -1;">
      <h3>Clasificación Táctica de Dron</h3>
      <div style="font-size: 1.25rem; font-weight: bold; color: #58a6ff;" id="det-cat">Ninguno (Despejado)</div>
      <div class="details">Estimado por Frecuencia Fundamental de Pala (BPF) y múltiplos armónicos</div>
    </div>
  </div>

  <!-- Sección Táctica: Radar 360° y Confianza del Evaluador -->
  <div class="card" style="margin-bottom: 20px;">
    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; flex-wrap: wrap; gap: 8px;">
      <h3 style="margin: 0; font-size: 1.05rem; color: #58a6ff;">🧭 Brújula Radar 360° y Confianza Táctica</h3>
      <span id="target-bearing-badge" style="font-family: monospace; font-size: 0.95rem; color: #8b949e; font-weight: bold; background: #21262d; padding: 4px 10px; border-radius: 6px;">VIGILANDO 360°</span>
    </div>
    <div style="display: flex; gap: 24px; align-items: center; justify-content: space-around; flex-wrap: wrap;">
      <div style="text-align: center;">
        <canvas id="radar-canvas" width="220" height="220" style="border-radius: 50%; background: #0a1017; border: 2px solid #238636; box-shadow: 0 0 14px rgba(35, 134, 54, 0.35); display: inline-block;"></canvas>
        <div style="margin-top: 6px; font-size: 0.8rem; color: #8b949e;">Array Acústico 4 Mics (M1/M2 X - M3/M4 Y)</div>
      </div>
      <div style="flex: 1; min-width: 240px;">
        <div style="margin-bottom: 14px;">
          <div style="display: flex; justify-content: space-between; font-size: 0.85rem; color: #8b949e; margin-bottom: 6px;">
            <span>Confianza del Evaluador (Anti-falsas alarmas):</span>
            <strong id="conf-val" style="color: #fff; font-size: 1.1rem;">0%</strong>
          </div>
          <div class="meter-bg" style="margin-top: 0; height: 18px;">
            <div id="conf-bar" class="meter-fill" style="width: 0%; background: linear-gradient(90deg, #00ff66, #ffcc00, #ff3344);"></div>
          </div>
          <div style="font-size: 0.75rem; color: #8b949e; margin-top: 5px;">Ponderación temporal continua: requiere consistencia armónica antes de disparar alerta.</div>
        </div>
        <div class="status-row"><span>Rumbo Estimado (DoA):</span><strong id="radar-bearing" style="color: #58a6ff; font-size: 1.05rem;">--° (Sin objetivo)</strong></div>
        <div class="status-row"><span>Suelo de Ruido Ambiental:</span><span id="noise-floor-lbl">0.70</span></div>
        <div class="status-row"><span>Modo de Umbral:</span><span id="thr-mode-lbl" style="font-weight: bold; color: #58a6ff;">Fijo</span></div>
      </div>
    </div>
  </div>

  <!-- Espectrograma en Cascada (Waterfall SDR en Vivo) -->
  <div class="card" style="margin-bottom: 20px;">
    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
      <h3 style="margin: 0; font-size: 1rem; color: #8b949e;">🌊 Espectrograma en Cascada (Waterfall en Vivo)</h3>
      <span style="font-size: 0.75rem; color: #8b949e; font-family: monospace;">80 Hz &larr; Espectro de Rotor &rarr; 2400 Hz</span>
    </div>
    <canvas id="waterfall-canvas" width="600" height="110" style="width: 100%; height: 110px; border-radius: 6px; background: #000; border: 1px solid #30363d; display: block;"></canvas>
    <div style="display: flex; justify-content: space-between; font-size: 0.7rem; color: #8b949e; margin-top: 4px; font-family: monospace;">
      <span>80 Hz</span>
      <span>500 Hz</span>
      <span>1000 Hz</span>
      <span>1500 Hz</span>
      <span>2400 Hz</span>
    </div>
  </div>

  <!-- Selector de Sensibilidad -->
  <div class="card" style="margin-bottom: 20px;">
    <h3 style="margin-bottom: 4px;">Sensibilidad del Detector de Drones</h3>
    <div class="btn-row">
      <button class="btn" id="btn-thr-auto" style="background: #8957e5; color: #fff;" onclick="setThresholdAuto()">🧠 ADAPTATIVO (AUTO)</button>
      <button class="btn" style="background: #1f6feb; color: #fff;" onclick="setThreshold(1.20)">🎯 TEST ALTAVOZ (1.20)</button>
      <button class="btn" style="background: #238636; color: #fff;" onclick="setThreshold(1.45)">🛡️ BALANCEADO (1.45)</button>
      <button class="btn" style="background: #30363d; color: #8b949e;" onclick="setThreshold(1.70)">🏔️ EXTERIOR (1.70)</button>
    </div>
  </div>

  <!-- Botones de Control de Prueba -->
  <div class="card" style="margin-bottom: 20px;">
    <h3 style="margin-bottom: 4px;">Control Manual y Test de LED</h3>
    <div class="btn-row">
      <button class="btn btn-green" onclick="setState('listening')">🟢 FORZAR DESPEJADO</button>
      <button class="btn btn-yellow" onclick="setState('detecting')">🟡 FORZAR DETECTANDO</button>
      <button class="btn btn-red" onclick="setState('alert')">🔴 FORZAR LOCALIZADO</button>
    </div>
  </div>

  <!-- Monitoreo de los 4 micrófonos -->
  <div class="grid">
    <div class="card">
      <h3>Mic 1 (Delantero Izq)</h3>
      <div><span class="val" id="rms1">0</span><span class="unit">RMS</span></div>
      <div class="meter-bg"><div class="meter-fill" id="bar1"></div></div>
      <div class="details">Pico: <span id="pk1">0</span></div>
    </div>
    <div class="card">
      <h3>Mic 2 (Delantero Der)</h3>
      <div><span class="val" id="rms2">0</span><span class="unit">RMS</span></div>
      <div class="meter-bg"><div class="meter-fill" id="bar2"></div></div>
      <div class="details">Pico: <span id="pk2">0</span></div>
    </div>
    <div class="card">
      <h3>Mic 3 (Trasero Izq)</h3>
      <div><span class="val" id="rms3">0</span><span class="unit">RMS</span></div>
      <div class="meter-bg"><div class="meter-fill" id="bar3"></div></div>
      <div class="details">Pico: <span id="pk3">0</span></div>
    </div>
    <div class="card">
      <h3>Mic 4 (Trasero Der)</h3>
      <div><span class="val" id="rms4">0</span><span class="unit">RMS</span></div>
      <div class="meter-bg"><div class="meter-fill" id="bar4"></div></div>
      <div class="details">Pico: <span id="pk4">0</span></div>
    </div>
  </div>

  <h2>Estado del Sistema</h2>
  <div class="card">
    <div class="status-row"><span>Estado Dron:</span><strong id="drone-status">DESPEJADO</strong></div>
    <div class="status-row"><span>Modo Red:</span><strong id="net-mode">-</strong></div>
    <div class="status-row"><span>IP Local:</span><strong id="net-ip">-</strong></div>
    <div class="status-row"><span>Bus A (M1/M2):</span><strong id="bus-a">-</strong></div>
    <div class="status-row"><span>Bus B (M3/M4):</span><strong id="bus-b">-</strong></div>
  </div>

  <!-- Historial de Incursiones -->
  <div class="card" style="margin-top: 20px;">
    <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; flex-wrap: wrap; gap: 8px;">
      <h2 style="margin: 0; font-size: 1.2rem; color: var(--text-bright);">📋 Historial de Incursiones (Caja Negra)</h2>
      <div style="display: flex; gap: 8px; flex-wrap: wrap;">
        <a href="/api/export_csv" download="dronradar_historial.csv" class="btn" style="text-decoration: none; flex: 0; min-width: auto; background: #1f6feb; color: #fff; padding: 6px 12px; font-size: 0.85rem; display: inline-flex; align-items: center;">📥 Descargar Informe CSV</a>
        <button class="btn" style="flex: 0; min-width: auto; background: #21262d; border: 1px solid #30363d; color: #c9d1d9; padding: 6px 12px; font-size: 0.85rem;" onclick="clearHistory()">🗑️ Borrar Historial</button>
      </div>
    </div>
    <div style="overflow-x: auto;">
      <table style="width: 100%; border-collapse: collapse; font-size: 0.9rem;" id="history-table">
        <thead>
          <tr style="border-bottom: 2px solid #30363d; text-align: left; color: #8b949e;">
            <th style="padding: 8px;">Hora / Uptime</th>
            <th style="padding: 8px;">Clasificación Estimada</th>
            <th style="padding: 8px;">Frecuencia (f0)</th>
            <th style="padding: 8px;">Score Máx</th>
            <th style="padding: 8px;">Armónicos</th>
            <th style="padding: 8px;">Duración</th>
          </tr>
        </thead>
        <tbody id="history-body">
          <tr><td colspan="6" style="text-align: center; padding: 16px; color: #8b949e;">Sin detecciones registradas aún</td></tr>
        </tbody>
      </table>
    </div>
  </div>
</div>

<script>
async function setThreshold(val) {
  try {
    await fetch('/api/set_threshold?val=' + val, { method: 'POST' });
    updateStatus();
  } catch (e) {
    console.error(e);
  }
}

async function setState(s) {
  try {
    await fetch('/api/set_state?state=' + s, { method: 'POST' });
    updateStatus();
  } catch (e) {
    console.error(e);
  }
}

async function updateStatus() {
  try {
    const res = await fetch('/api/status');
    const data = await res.json();

    // Actualizar Banner
    const banner = document.getElementById('alert-banner');
    const title = document.getElementById('alert-title');
    const desc = document.getElementById('alert-desc');
    const droneStatus = document.getElementById('drone-status');

    if (data.state === 'ALERT') {
      banner.className = 'banner banner-alert';
      title.innerText = '🔴 ¡DRON LOCALIZADO!';
      desc.innerText = '¡' + (data.category || 'Dron') + ' detectado! Rotor a ' + data.freq_hz.toFixed(1) + ' Hz (score: ' + data.score.toFixed(2) + '). LED rojo activado.';
      droneStatus.innerText = 'LOCALIZADO (ROJO)';
      droneStatus.style.color = '#ff3344';
    } else if (data.state === 'DETECTING') {
      banner.className = 'banner banner-detecting';
      title.innerText = '🟡 DETECTANDO ACTIVIDAD SOSPECHOSA';
      desc.innerText = 'Posible ' + (data.category || 'Dron') + ' en torno a ' + data.freq_hz.toFixed(1) + ' Hz (score: ' + data.score.toFixed(2) + ').';
      droneStatus.innerText = 'DETECTANDO (AMARILLO)';
      droneStatus.style.color = '#ffaa00';
    } else {
      banner.className = 'banner banner-listening';
      title.innerText = '🟢 OPERATIVO (NO LOCALIZADO)';
      desc.innerText = 'Espacio aéreo despejado. Algoritmo armónico vigilando. LED en verde.';
      droneStatus.innerText = 'DESPEJADO (VERDE)';
      droneStatus.style.color = '#00ff66';
    }

    // Actualizar Telemetría Detector
    document.getElementById('det-score').innerText = data.score.toFixed(2);
    document.getElementById('det-freq').innerText = data.freq_hz.toFixed(1);
    document.getElementById('det-teeth').innerText = data.teeth;
    document.getElementById('det-chain').innerText = data.chain;
    if (document.getElementById('det-cat')) {
      document.getElementById('det-cat').innerText = data.category || 'Ninguno (Despejado)';
    }
    if (data.threshold) {
      document.getElementById('det-thr').innerText = data.threshold.toFixed(2);
      document.getElementById('det-thr-lbl').innerText = data.threshold.toFixed(2);
    }
    const scorePct = Math.min(100, (data.score / 2.5) * 100);
    document.getElementById('score-bar').style.width = scorePct + '%';

    // Actualizar vúmetros
    for (let i = 1; i <= 4; i++) {
      const rms = data.mics.rms[i-1];
      const pk = data.mics.peak[i-1];
      document.getElementById('rms' + i).innerText = Math.round(rms);
      document.getElementById('pk' + i).innerText = pk;
      const pct = Math.min(100, (rms / 600) * 100);
      document.getElementById('bar' + i).style.width = pct + '%';
    }

    if (data.time) {
      document.getElementById('clock-display').innerText = data.time + (data.time_synced ? '' : ' (uptime)');
    }

    // Actualizar Confianza Táctica
    const conf = data.confidence || 0;
    document.getElementById('conf-val').innerText = conf + '%';
    document.getElementById('conf-bar').style.width = conf + '%';

    // Actualizar Rumbo y Radar 360
    const az = data.azimuth || 0;
    const azValid = data.azimuth_valid && (conf >= 25);
    const bearingEl = document.getElementById('radar-bearing');
    const badgeEl = document.getElementById('target-bearing-badge');
    if (azValid) {
      const card = getCardinal(az);
      bearingEl.innerText = `${az}° (${card})`;
      bearingEl.style.color = (conf >= 65) ? '#ff3344' : '#ffcc00';
      badgeEl.innerText = `BLANCO: ${az}° ${card} (${conf}%)`;
      badgeEl.style.color = (conf >= 65) ? '#ff3344' : '#ffcc00';
      badgeEl.style.background = (conf >= 65) ? '#ff334422' : '#ffcc0022';
      badgeEl.style.border = (conf >= 65) ? '1px solid #ff3344' : '1px solid #ffcc00';
    } else {
      bearingEl.innerText = '--° (Sin objetivo)';
      bearingEl.style.color = '#8b949e';
      badgeEl.innerText = 'VIGILANDO 360°';
      badgeEl.style.color = '#8b949e';
      badgeEl.style.background = '#21262d';
      badgeEl.style.border = 'none';
    }

    drawRadar(az, conf, azValid);

    // Actualizar Cascada Waterfall
    if (data.spectrum) {
      addWaterfallRow(data.spectrum);
    }

    // Modo umbral y suelo de ruido
    document.getElementById('thr-mode-lbl').innerText = data.auto_thr ? 'Adaptativo Automático' : 'Fijo';
    document.getElementById('noise-floor-lbl').innerText = (data.noise_floor || 0).toFixed(2);
    const btnAuto = document.getElementById('btn-thr-auto');
    if (btnAuto) {
      btnAuto.style.boxShadow = data.auto_thr ? '0 0 12px #8957e5' : 'none';
      btnAuto.style.border = data.auto_thr ? '2px solid #fff' : 'none';
    }

    const badge = document.getElementById('net-badge');
    if (data.wifi_connected) {
      badge.className = 'badge badge-online';
      badge.innerText = 'WIFI: ' + data.ssid;
    } else {
      badge.className = 'badge badge-offline';
      badge.innerText = 'MODO LOCAL (AP)';
    }

    document.getElementById('net-mode').innerText = data.wifi_connected ? 'Estación WiFi' : 'Punto de Acceso (DronRadar-Node)';
    document.getElementById('net-ip').innerText = data.ip;
    document.getElementById('bus-a').innerText = data.mics.bus_a ? 'ACTIVO (OK)' : 'SIN SEÑAL';
    document.getElementById('bus-b').innerText = data.mics.bus_b ? 'ACTIVO (OK)' : 'SIN SEÑAL';
  } catch (e) {
    console.error(e);
  }
}

// ----------------------------------------------------------------------------
// RENDERIZADO DEL RADAR PPI 360°
// ----------------------------------------------------------------------------
let radarSweep = 0;
function drawRadar(azimuth, confidence, hasTarget) {
  const c = document.getElementById('radar-canvas');
  if (!c) return;
  const ctx = c.getContext('2d');
  const w = c.width, h = c.height;
  const cx = w / 2, cy = h / 2, r = cx - 14;

  ctx.fillStyle = '#0a1017';
  ctx.fillRect(0, 0, w, h);

  // Anillos concéntricos
  ctx.strokeStyle = '#1b3a2a';
  ctx.lineWidth = 1;
  for (let i = 1; i <= 4; i++) {
    ctx.beginPath();
    ctx.arc(cx, cy, (r / 4) * i, 0, Math.PI * 2);
    ctx.stroke();
  }

  // Ejes cardinales
  ctx.beginPath();
  ctx.moveTo(cx - r, cy); ctx.lineTo(cx + r, cy);
  ctx.moveTo(cx, cy - r); ctx.lineTo(cx, cy + r);
  ctx.stroke();

  // Puntos cardinales
  ctx.fillStyle = '#4ade80';
  ctx.font = '10px monospace';
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillText('N 0°', cx, cy - r + 8);
  ctx.fillText('S', cx, cy + r - 8);
  ctx.fillText('E', cx + r - 10, cy);
  ctx.fillText('W', cx - r + 10, cy);

  // Línea de barrido giratoria continua
  radarSweep = (radarSweep + 0.06) % (Math.PI * 2);
  ctx.strokeStyle = 'rgba(74, 222, 128, 0.45)';
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + r * Math.sin(radarSweep), cy - r * Math.cos(radarSweep));
  ctx.stroke();

  // Vector de amenaza si hay objetivo detectado
  if (hasTarget && confidence >= 25) {
    const azRad = (azimuth * Math.PI) / 180;
    const dist = r * (0.85 - (confidence / 100) * 0.4);
    const tx = cx + dist * Math.sin(azRad);
    const ty = cy - dist * Math.cos(azRad);

    ctx.strokeStyle = (confidence >= 65) ? 'rgba(255, 51, 68, 0.75)' : 'rgba(255, 204, 0, 0.75)';
    ctx.lineWidth = 2;
    ctx.setLineDash([3, 3]);
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(tx, ty);
    ctx.stroke();
    ctx.setLineDash([]);

    // Punto objetivo
    ctx.fillStyle = (confidence >= 65) ? '#ff3344' : '#ffcc00';
    ctx.beginPath();
    ctx.arc(tx, ty, 6, 0, Math.PI * 2);
    ctx.fill();

    ctx.strokeStyle = (confidence >= 65) ? '#ff3344' : '#ffcc00';
    ctx.beginPath();
    ctx.arc(tx, ty, 11, 0, Math.PI * 2);
    ctx.stroke();
  }
}

// ----------------------------------------------------------------------------
// RENDERIZADO DEL ESPECTROGRAMA EN CASCADA (WATERFALL)
// ----------------------------------------------------------------------------
function addWaterfallRow(spectrum) {
  const c = document.getElementById('waterfall-canvas');
  if (!c || !spectrum || spectrum.length === 0) return;
  const ctx = c.getContext('2d');
  const w = c.width, h = c.height;

  // Desplazar 2 píxeles hacia abajo
  const imgData = ctx.getImageData(0, 0, w, h - 2);
  ctx.putImageData(imgData, 0, 2);

  const binW = w / spectrum.length;
  for (let i = 0; i < spectrum.length; i++) {
    const v = spectrum[i];
    let r = 0, g = 0, b = 0;
    if (v < 64) {
      b = v * 4;
    } else if (v < 128) {
      g = (v - 64) * 4;
      b = 255 - g;
    } else if (v < 192) {
      r = (v - 128) * 4;
      g = 255;
    } else {
      r = 255;
      g = Math.max(0, 255 - (v - 192) * 4);
    }
    ctx.fillStyle = `rgb(${r},${g},${b})`;
    ctx.fillRect(i * binW, 0, binW + 1, 2);
  }
}

function getCardinal(deg) {
  const dirs = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
  const idx = Math.round(deg / 22.5) % 16;
  return dirs[idx];
}

async function setThresholdAuto() {
  try {
    await fetch('/api/set_threshold?auto=1', { method: 'POST' });
    updateStatus();
  } catch (e) {
    console.error(e);
  }
}

let hasAutoSynced = false;
async function syncBrowserTime() {
  try {
    const now = new Date();
    const epoch = Math.floor(now.getTime() / 1000);
    const tzOffset = -now.getTimezoneOffset() * 60;
    const body = 'epoch=' + epoch + '&tz=' + tzOffset;
    const res = await fetch('/api/set_time', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body
    });
    const d = await res.json();
    if (d.time) {
      document.getElementById('clock-display').innerText = d.time;
      hasAutoSynced = true;
    }
  } catch (e) {
    console.error('Error sincronizando hora con navegador:', e);
  }
}

async function clearHistory() {
  try {
    await fetch('/api/clear_history', { method: 'POST' });
    updateHistory();
  } catch (e) {
    console.error(e);
  }
}

async function updateHistory() {
  try {
    const res = await fetch('/api/history');
    const data = await res.json();
    const tbody = document.getElementById('history-body');
    if (!data.events || data.events.length === 0) {
      tbody.innerHTML = '<tr><td colspan="6" style="text-align: center; padding: 16px; color: #8b949e;">Sin detecciones registradas aún</td></tr>';
      return;
    }
    let html = '';
    for (let ev of data.events) {
      html += '<tr style="border-bottom: 1px solid #21262d;">' +
        '<td style="padding: 8px; font-family: monospace;">' + ev.time + '</td>' +
        '<td style="padding: 8px; font-weight: bold; color: #58a6ff;">' + ev.category + '</td>' +
        '<td style="padding: 8px;">' + ev.freq_hz.toFixed(1) + ' Hz</td>' +
        '<td style="padding: 8px; font-weight: bold; color: #ff3344;">' + ev.score.toFixed(2) + '</td>' +
        '<td style="padding: 8px;">' + ev.teeth + '/12</td>' +
        '<td style="padding: 8px;">' + ev.duration + ' s</td>' +
        '</tr>';
    }
    tbody.innerHTML = html;
  } catch (e) {
    console.error(e);
  }
}

// Auto-sincronizar hora del cliente inmediatamente
syncBrowserTime();
setInterval(updateStatus, 300);
updateStatus();
setInterval(updateHistory, 2000);
updateHistory();
</script>
</body>
</html>
)rawliteral";

// ============================================================================
// CONFIGURACIÓN Y RUTAS WEB
// ============================================================================
void setupRoutes() {
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", INDEX_HTML);
    });

    server.on("/api/status", HTTP_GET, []() {
        JsonDocument doc;
        doc["wifi_connected"] = wifiConnected;
        doc["ssid"] = WIFI_SSID;
        doc["ip"] = wifiConnected ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
        
        doc["state"] = (currentState == STATE_ALERT_T1 || currentState == STATE_ALERT_T2) ? "ALERT" :
                       ((currentState == STATE_ALERT_T3 || currentState == STATE_ALERT_T4) ? "DETECTING" : "LISTENING");

        doc["score"] = currentScore;
        doc["threshold"] = currentThreshold;
        doc["freq_hz"] = currentFreqHz;
        doc["teeth"] = g_current_teeth;
        doc["chain"] = g_current_chain;
        doc["category"] = (currentState == STATE_ALERT_T1 || currentState == STATE_ALERT_T3) ? classifyDrone(currentFreqHz) : "Ninguno (Despejado)";

        doc["confidence"] = (int)(currentConfidence * 100.0f);
        doc["azimuth"] = (int)currentAzimuth;
        doc["azimuth_valid"] = azimuthValid;
        doc["auto_thr"] = autoThresholdEnabled;
        doc["noise_floor"] = ambientNoiseFloor;

        JsonArray specArr = doc["spectrum"].to<JsonArray>();
        for (int b = 0; b < WATERFALL_BINS; b++) {
            specArr.add(currentSpectrum[b]);
        }

        char timeBuf[32];
        getNodeTimeString(timeBuf, sizeof(timeBuf));
        doc["time"] = timeBuf;
        doc["time_synced"] = g_time_synced;

        JsonObject mics = doc["mics"].to<JsonObject>();
        JsonArray r = mics["rms"].to<JsonArray>();
        JsonArray p = mics["peak"].to<JsonArray>();

        portENTER_CRITICAL(&micMux);
        for (int i = 0; i < 4; i++) {
            r.add(micMetrics.rms[i]);
            p.add(micMetrics.peak[i]);
        }
        mics["bus_a"] = micMetrics.busA_active;
        mics["bus_b"] = micMetrics.busB_active;
        portEXIT_CRITICAL(&micMux);

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/export_csv", HTTP_GET, []() {
        if (LittleFS.exists("/history.csv")) {
            File f = LittleFS.open("/history.csv", FILE_READ);
            server.sendHeader("Content-Disposition", "attachment; filename=\"dronradar_historial.csv\"");
            server.streamFile(f, "text/csv");
            f.close();
        } else {
            server.send(404, "text/plain", "Sin historial registrado");
        }
    });

    server.on("/api/set_time", HTTP_POST, []() {
        if (server.hasArg("epoch")) {
            time_t epoch = (time_t)server.arg("epoch").toInt();
            long tz = server.hasArg("tz") ? server.arg("tz").toInt() : 0;
            setNodeTime(epoch, tz);
        } else if (server.hasArg("time")) {
            String tStr = server.arg("time");
            int h = 0, m = 0, s = 0;
            if (sscanf(tStr.c_str(), "%d:%d:%d", &h, &m, &s) >= 2) {
                struct tm t = {0};
                t.tm_year = 126; // 2026
                t.tm_mon = 8;
                t.tm_mday = 23;
                t.tm_hour = h;
                t.tm_min = m;
                t.tm_sec = s;
                time_t raw = mktime(&t);
                setNodeTime(raw, 0);
            }
        }
        char tbuf[32];
        getNodeTimeString(tbuf, sizeof(tbuf));
        JsonDocument doc;
        doc["status"] = "ok";
        doc["time"] = tbuf;
        doc["time_synced"] = g_time_synced;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/history", HTTP_GET, []() {
        JsonDocument doc;
        JsonArray arr = doc["events"].to<JsonArray>();
        if (historyMutex && xSemaphoreTake(historyMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            for (int i = 0; i < eventCount; i++) {
                JsonObject ev = arr.add<JsonObject>();
                ev["time"] = eventHistory[i].time_str;
                ev["score"] = eventHistory[i].peak_score;
                ev["freq_hz"] = eventHistory[i].peak_freq;
                ev["teeth"] = eventHistory[i].teeth;
                ev["duration"] = eventHistory[i].duration_sec;
                ev["category"] = eventHistory[i].category;
            }
            xSemaphoreGive(historyMutex);
        }
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/clear_history", HTTP_POST, []() {
        if (historyMutex && xSemaphoreTake(historyMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            eventCount = 0;
            File f = LittleFS.open("/history.csv", FILE_WRITE);
            if (f) {
                f.println("Hora,Clasificacion,Frecuencia_Hz,Score_Max,Armonicos,Duracion_s");
                f.close();
            }
            xSemaphoreGive(historyMutex);
        }
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/set_threshold", HTTP_POST, []() {
        if (server.hasArg("auto")) {
            autoThresholdEnabled = (server.arg("auto") == "1" || server.arg("auto") == "true");
            Serial.printf("[UMBRAL] Modo adaptativo automático: %s\n", autoThresholdEnabled ? "ACTIVADO" : "DESACTIVADO");
        } else if (server.hasArg("val")) {
            autoThresholdEnabled = false;
            currentThreshold = server.arg("val").toFloat();
            g_detector_state.thr = currentThreshold;
            prefs.begin("dronradar", false);
            prefs.putFloat("thr", currentThreshold);
            prefs.end();
            Serial.printf("[DETECTOR] Umbral guardado en NVS: %.2f\n", currentThreshold);
        }
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });

    server.on("/api/set_state", HTTP_POST, []() {
        if (server.hasArg("state")) {
            String s = server.arg("state");
            if (s == "alert" || s == "red") {
                currentState = STATE_ALERT_T1;
                alertStartTime = millis();
                Serial.println("[WEB CMD] -> Alarma activada: DRON LOCALIZADO (Rojo)");
            } else if (s == "detecting" || s == "yellow") {
                currentState = STATE_ALERT_T3;
                alertStartTime = millis();
                Serial.println("[WEB CMD] -> Estado cambiado a: DETECTANDO (Amarillo)");
            } else {
                currentState = STATE_LISTENING;
                alertStartTime = 0;
                Serial.println("[WEB CMD] -> Estado cambiado a: OPERATIVO / DESPEJADO (Verde)");
            }
        }
        server.send(200, "application/json", "{\"status\":\"ok\"}");
    });
}

// ============================================================================
// CLIENTE MQTT
// ============================================================================
void reconnectMQTT() {
    if (!wifiConnected || mqttClient.connected()) return;
    
    if (strlen(appConfig.mqtt_broker) == 0) return;

    if (millis() - lastMqttReport < 5000) return;
    lastMqttReport = millis();

    String clientId = "DronRadar-" + WiFi.macAddress();
    Serial.printf("[MQTT] Conectando a %s:%d...\n", appConfig.mqtt_broker, appConfig.mqtt_port);

    bool ok = false;
    if (strlen(appConfig.mqtt_user) > 0) {
        ok = mqttClient.connect(clientId.c_str(), appConfig.mqtt_user, appConfig.mqtt_pass);
    } else {
        ok = mqttClient.connect(clientId.c_str());
    }

    if (ok) {
        Serial.println("[MQTT] Conectado exitosamente.");
    }
}

void publishState() {
    if (!mqttClient.connected()) return;

    JsonDocument doc;
    doc["state"] = (currentState == STATE_ALERT_T1 || currentState == STATE_ALERT_T2) ? "ALERT" :
                   ((currentState == STATE_ALERT_T3 || currentState == STATE_ALERT_T4) ? "DETECTING" : "LISTENING");
    doc["score"] = currentScore;
    doc["freq_hz"] = currentFreqHz;
    doc["teeth"] = g_current_teeth;
    doc["chain"] = g_current_chain;
    doc["category"] = (currentState == STATE_ALERT_T1 || currentState == STATE_ALERT_T3) ? classifyDrone(currentFreqHz) : "Ninguno (Despejado)";

    char buf[256];
    serializeJson(doc, buf);
    String topic = String(appConfig.topic_prefix) + "/state";
    mqttClient.publish(topic.c_str(), buf);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    // Pines de LED
    pinMode(PIN_LED_DISCRETE, OUTPUT);
    digitalWrite(PIN_LED_DISCRETE, LOW);

    // Destello de arranque
    updateLedHardware(true, 0, 180, 20);
    delay(200);
    updateLedHardware(false);

    Serial.println("\n=======================================================");
    Serial.println("     DRONRADAR VOLANTI - INICIO DE SISTEMA             ");
    Serial.println("=======================================================");

    // Crear mutex para historial de detecciones
    historyMutex = xSemaphoreCreateMutex();

    // Inicializar memoria Flash LittleFS y restaurar histórico (Caja Negra)
    if (LittleFS.begin(true)) {
        Serial.println("[LITTLEFS OK] Sistema de archivos Flash montado.");
        loadHistoryFromFlash();
    } else {
        Serial.println("[LITTLEFS ERROR] Falló al montar LittleFS.");
    }

    // Cargar umbral guardado en NVS
    prefs.begin("dronradar", false);
    currentThreshold = prefs.getFloat("thr", 1.20f);
    prefs.end();

    // Inicializar I2S para micrófonos primero
    initI2SMicrophones();

    // Inicializar detector matemático de dron (VolAnti)
    esp_err_t det_err = detector_init();
    Serial.printf("[DETECTOR] Tablas FFT/Comb inicializadas: %s\n", det_err == ESP_OK ? "OK" : "ERROR");
    detector_state_reset(&g_detector_state, currentThreshold);
    g_detector_state.trk_family = true;
    g_detector_state.veto_voice = true;
    g_detector_state.nf_gate = true;
    g_detector_state.trk_v2 = true;
    g_detector_ready = (det_err == ESP_OK);
    Serial.printf("[DETECTOR] Umbral Alerta: %.2f (Test Altavoz/YouTube) | Algoritmo VolAnti: ACTIVO\n", currentThreshold);

    // Lanzar tarea de lectura de micrófonos en Core 0
    xTaskCreatePinnedToCore(
        micAudioTask,
        "micAudioTask",
        10240,
        NULL,
        2,  // Prioridad superior
        NULL,
        0   // Core 0
    );

    // Conexión WiFi no bloqueante
    Serial.printf("[WIFI] Intentando conectar a '%s'...\n", WIFI_SSID);
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 12) { // 6 segundos máximo
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\n[WIFI OK] ¡Conectado a la red de casa!");
        Serial.printf("[WIFI] IP Asignada: http://%s\n", WiFi.localIP().toString().c_str());
    } else {
        wifiConnected = false;
        Serial.println("\n[WIFI AVISO] No se detectó la red 'CERDOVOLADOR'.");
        Serial.println("[WIFI] Continuando en MODO LOCAL (sin bloqueo).");
        
        WiFi.disconnect(true);
        delay(100);
        WiFi.mode(WIFI_AP);
        delay(100);
        bool ap_ok = WiFi.softAP("DronRadar-Node", "dronradar123", 1, 0, 4);
        Serial.printf("[AP] Estado de Punto de Acceso: %s\n", ap_ok ? "LEVANTADO Y EMITIENDO" : "ERROR AL CREAR");
        Serial.printf("[AP] Red: 'DronRadar-Node'  | Password: 'dronradar123'\n");
        Serial.printf("[AP] Conéctate y abre: http://%s\n", WiFi.softAPIP().toString().c_str());
    }

    setupRoutes();
    server.begin();
    Serial.println("[HTTP] Servidor Web iniciado correctamente.");

    // Cargar configuración NVS
    auto safe_strncpy = [](char *dst, const char *src, size_t size) {
        if (size == 0) return;
        strncpy(dst, src, size - 1);
        dst[size - 1] = '\0';
    };
    prefs.begin("dronradar", false);
    String b = prefs.getString("broker", "192.168.1.100");
    safe_strncpy(appConfig.mqtt_broker, b.c_str(), sizeof(appConfig.mqtt_broker));
    appConfig.mqtt_port = prefs.getInt("port", 1883);
    String u = prefs.getString("user", "");
    safe_strncpy(appConfig.mqtt_user, u.c_str(), sizeof(appConfig.mqtt_user));
    String p = prefs.getString("pass", "");
    safe_strncpy(appConfig.mqtt_pass, p.c_str(), sizeof(appConfig.mqtt_pass));
    String tp = prefs.getString("topic", "dronradar/node1");
    safe_strncpy(appConfig.topic_prefix, tp.c_str(), sizeof(appConfig.topic_prefix));
    prefs.end();

    mqttClient.setServer(appConfig.mqtt_broker, appConfig.mqtt_port);

    Serial.println("\n=======================================================");
    Serial.println("  SISTEMA OPERATIVO Y LISTO PARA DETECTAR DRONES       ");
    Serial.println("=======================================================\n");
}

// ============================================================================
// LOOP PRINCIPAL (CORE 1)
// ============================================================================
void loop() {
    server.handleClient();

    if (wifiConnected) {
        reconnectMQTT();
        mqttClient.loop();
    }

    // Gestionar parpadeos y color del LED
    handleLedStatus();

    // Comandos por consola serie
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'w' || c == 'W' || c == '?' || c == 's' || c == 'S') {
            Serial.println("\n-------------------------------------------------------");
            Serial.println("               ESTADO DE RED Y AP                      ");
            Serial.println("-------------------------------------------------------");
            Serial.printf("Modo WiFi: %s\n", (WiFi.getMode() == WIFI_AP_STA) ? "AP+STA (Híbrido)" : ((WiFi.getMode() == WIFI_AP) ? "Solo AP" : "STA"));
            Serial.printf("SSID AP: DronRadar-Node\n");
            Serial.printf("Contraseña AP: dronradar123\n");
            Serial.printf("IP del Punto de Acceso: http://%s\n", WiFi.softAPIP().toString().c_str());
            Serial.printf("Clientes conectados al AP: %d\n", WiFi.softAPgetStationNum());
            Serial.printf("Estado STA (Casa): %s\n", wifiConnected ? "CONECTADO" : "DESCONECTADO (Sin red)");
            Serial.println("-------------------------------------------------------\n");
        } else if (c == 'r' || c == 'R') {
            currentState = STATE_ALERT_T1;
            alertStartTime = millis();
            Serial.println("[ESTADO] -> 🔴 ROJO (DRON LOCALIZADO)");
        } else if (c == 'y' || c == 'Y') {
            currentState = STATE_ALERT_T3;
            alertStartTime = millis();
            Serial.println("[ESTADO] -> 🟡 AMARILLO (DETECTANDO)");
        } else if (c == 'g' || c == 'G') {
            currentState = STATE_LISTENING;
            alertStartTime = 0;
            Serial.println("[ESTADO] -> 🟢 VERDE (OPERATIVO / DESPEJADO)");
        } else if (c == '+') {
            currentThreshold = 0.85f;
            g_detector_state.thr = currentThreshold;
            Serial.printf("[UMBRAL] Sensibilidad MÁXIMA activada: %.2f\n", currentThreshold);
        } else if (c == '0') {
            currentThreshold = 0.95f;
            g_detector_state.thr = currentThreshold;
            Serial.printf("[UMBRAL] Sensibilidad TEST ALTAVOZ/YOUTUBE activada: %.2f\n", currentThreshold);
        } else if (c == '-') {
            currentThreshold = 1.70f;
            g_detector_state.thr = currentThreshold;
            Serial.printf("[UMBRAL] Sensibilidad EXTERIOR VOLANTI activada: %.2f\n", currentThreshold);
        } else if (c == 't' || c == 'T') {
            String rest = Serial.readStringUntil('\n');
            rest.trim();
            if (rest.length() > 0) {
                if (rest.startsWith(":")) rest = rest.substring(1);
                rest.trim();
                int h = 0, m = 0, s = 0;
                if (sscanf(rest.c_str(), "%d:%d:%d", &h, &m, &s) >= 2) {
                    struct tm t = {0};
                    t.tm_year = 126; // 2026
                    t.tm_mon = 8;
                    t.tm_mday = 23;
                    t.tm_hour = h;
                    t.tm_min = m;
                    t.tm_sec = s;
                    time_t raw = mktime(&t);
                    setNodeTime(raw, 0);
                } else {
                    time_t raw = (time_t)rest.toInt();
                    if (raw > 10000000) {
                        setNodeTime(raw, 0);
                    } else {
                        Serial.println("[RELOJ ERROR] Formato esperado: 'T 19:45:00' o 'T:19:45'");
                    }
                }
            } else {
                char tbuf[32];
                getNodeTimeString(tbuf, sizeof(tbuf));
                Serial.printf("[RELOJ] Hora actual del nodo: %s (%s)\n", tbuf, g_time_synced ? "Sincronizada" : "Uptime");
                Serial.println("[RELOJ] Para cambiarla: envía 'T 19:45:00' o abre la web para auto-sincronizar.");
            }
        }
    }

    unsigned long now = millis();

    // Imprimir niveles y telemetría por Serial cada 500ms
    if (now - lastConsoleReport >= 500) {
        lastConsoleReport = now;

        float r[4];
        int16_t p[4];
        portENTER_CRITICAL(&micMux);
        for (int i = 0; i < 4; i++) {
            r[i] = micMetrics.rms[i];
            p[i] = micMetrics.peak[i];
        }
        portEXIT_CRITICAL(&micMux);

        Serial.print("[MIC] ");
        printVuMeter("M1", r[0], p[0]);
        printVuMeter("M2", r[1], p[1]);
        printVuMeter("M3", r[2], p[2]);
        printVuMeter("M4", r[3], p[3]);
        Serial.printf(" | smp=%lu\n", micMetrics.sample_count);

        char tbuf[16];
        getNodeTimeString(tbuf, sizeof(tbuf));
        Serial.printf("[%s] [DETECTOR] Score: %4.2f (thr=%.2f) | Freq: %5.1f Hz | Teeth: %2d/12 | Chain: %2d | Conf: %2d%% | Rumbo: %3d° | [%s] -> %s\n",
                      tbuf, currentScore, currentThreshold, currentFreqHz, g_current_teeth, g_current_chain,
                      (int)(currentConfidence * 100.0f), (int)currentAzimuth,
                      (currentState == STATE_ALERT_T1 ? "ROJO-LOCALIZADO" : (currentState == STATE_ALERT_T3 ? "AMARILLO-DETECTANDO" : "VERDE-OPERATIVO")),
                      (currentState == STATE_ALERT_T1 || currentState == STATE_ALERT_T3) ? classifyDrone(currentFreqHz) : "Despejado");

        // Auto-retorno a Verde si pasaron 10 segundos desde la alerta forzada sin detección
        if ((currentState >= STATE_ALERT_T1 && currentState <= STATE_ALERT_T4) && alertStartTime > 0 && (now - alertStartTime > 10000)) {
            currentState = STATE_LISTENING;
            alertStartTime = 0;
            recordEventEnd();
            Serial.println("[ESTADO] Alerta caducada -> Retornando a VERDE (Operativo)");
        }

        publishState();
    }

    delay(2);
}
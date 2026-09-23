# 🛰️ DronRadar - Detector Acústico de Drones (ESP32-S3 + VolAnti)

**DronRadar** es un sistema autónomo de detección acústica, clasificación y localización tridimensional (DoA 3D: Azimut 360° + Elevación) de vehículos aéreos no tripulados (UAV / Drones) basado en el microcontrolador **ESP32-S3** con matriz de **4 micrófonos I2S (INMP441)** y el algoritmo de filtro en peine FFT de **VolAnti**.

Diseñado para despliegue en exteriores dentro de cajas estancas (mástiles y tejados), incluye **actualización remota de firmware por WiFi (Web OTA)**, **interconexión en malla multi-nodo**, **capa preparada para radio LoRa** y un panel web militar táctico en tiempo real.

---

## ⚡ Características Principales

- **Algoritmo FFT Comb Filter (VolAnti)**: Detección armónica de frecuencia fundamental ($f_0$) y sobretonos de palas de rotor en rangos de 100 Hz a 1000 Hz.
- **Localización Acústica 3D (DoA 360° + Elevación)**:
  - Estimación de azimut horizontal mediante desfase ortogonal ($M_1-M_2$ en eje X, $M_3-M_4$ en eje Y).
  - Estimación del ángulo de elevación vertical ($0^\circ - 90^\circ$) y nivel de altitud (*Baja / Rasante*, *Media Altitud*, *Cenital / Sobre la vertical*).
- **Clasificador de Firma Espectral & Vector Doppler**:
  - Identificación del perfil de dron (*DJI Mini/Mavic*, *FPV Racing/Freestyle*, *Dron Pesado/Octocóptero*).
  - Cálculo de la derivada temporal $\Delta f_0 / \Delta t$ para alertar si la aeronave **se aproxima** ⚠️, **se aleja** 🛫 o se mantiene en **hover estático** ⏸️.
- **Integrador de Confianza Temporal (Anti-Flicker)**: Filtro de persistencia temporal (0-100%) que acumula evidencia acústica continua, discriminando ráfagas transitorias de viento, tráfico o aves, y estabilizando el nivel de alerta (Tier 1 a 4).
- **Mantenimiento Remoto Web OTA (`/update`)**: Actualización inalámbrica del firmware mediante drag & drop desde el navegador con barra de progreso y auto-reinicio, sin necesidad de abrir la caja estanca ni conectar cables.
- **Red Multi-Nodo (WiFi Mesh Broadcast)**: Protocolo de telemetría inter-nodo en red local (UDP broadcast puerto 4210) que permite a múltiples radares descubrirse mutuamente y coordinar rumbos cruzados para triangulación perimetral.
- **Predisposición para Radio LoRa (SX1262 / SX1276)**: Trama binaria ultra-compacta de 8 bytes con CRC-8 para enlaces de largo alcance y bajo consumo donde no haya cobertura WiFi.
- **Modo WiFi Híbrido con AP de Emergencia**: Si la red WiFi exterior falla o no está disponible, el nodo levanta automáticamente su propio punto de acceso `DronRadar-<NodeID>` (IP `192.168.4.1`) para configuración directa en campo.
- **Espectrograma en Cascada (Live Waterfall)**: Cascada de frecuencia en tiempo real (48 bins, canvas HTML5) que visualiza la firma acústica del dron.
- **Caja Negra en LittleFS & Exportación CSV**: Registro histórico de incursiones con fecha, hora, azimut, frecuencia, umbral y duración persistido en memoria flash interna. Botón de descarga directa `informe_incursiones.csv`.
- **Integración IoT / Home Assistant**: Publicación de telemetría y **Auto-Discovery MQTT** que crea automáticamente el dispositivo y sus sensores en Home Assistant.

---

## 📐 Esquema de Conexión Hardware

El firmware utiliza **dos buses I2S independientes** en el ESP32-S3 para capturar 4 canales de audio simultáneos a 16 kHz:

![Esquema de Cableado ESP32-S3](docs/images/esquema_cableado_esp32s3.jpg)

### Pinout ESP32-S3 DevKitC-1 N8 / N16R8

| Dispositivo / Función | Pin ESP32-S3 | Conexión / Detalle |
| :--- | :--- | :--- |
| **Bus I2S 0 (Micrófonos M1 y M2)** | | |
| SCK / BCLK (Bit Clock) | **GPIO 4** | M1 BCLK & M2 BCLK |
| WS / LRCLK (Word Select) | **GPIO 5** | M1 WS & M2 WS |
| SD (Serial Data) | **GPIO 6** | M1 L/R a GND (Left), M2 L/R a 3V3 (Right) |
| **Bus I2S 1 (Micrófonos M3 y M4)** | | |
| SCK / BCLK (Bit Clock) | **GPIO 7** | M3 BCLK & M4 BCLK |
| WS / LRCLK (Word Select) | **GPIO 8** | M3 WS & M4 WS |
| SD (Serial Data) | **GPIO 9** | M3 L/R a GND (Left), M4 L/R a 3V3 (Right) |
| **Alimentación Micrófonos** | **3.3V / GND** | Todos los VDD a 3.3V, todos los GND compartidos |
| **LED RGB Direccionable** | **GPIO 16 / 48** | Indicador de estado de alerta |
| **Interfaz SPI para Radio LoRa (SX1262)** | **GPIO 10 (CS), 14 (RST), 13 (DIO1)** | Enlace de largo alcance opcional |
| **Pantalla e-Paper (Opcional)** | SPI | Waveshare 2.13" para estado autónomo |

---

## 🌧️ Protección de Micrófonos para Intemperie (IP65 / IP67)

Para instalaciones exteriores (mástiles, tejados, perímetros), los sensores acústicos INMP441 requieren protección contra lluvia, humedad y viento minimizando la atenuación de frecuencias:

![Protección de Micrófonos](docs/images/proteccion_microfonos_intemperie.jpg)

### Recomendaciones de Fabricación:
1. **Prensaestopas IP67 con cavidad acústica**: Orificio exterior de 6–10 mm con orientación inclinada o hacia abajo para evitar acumulación de agua.
2. **Membrana hidrofóbica permeable al sonido**: Malla acústica tipo *Knowles AcoustiGuard* o membrana ePTFE permeable al aire pero impermeable al agua.
3. **Filtro de espuma de celda abierta**: Protección contra ruido de viento superficial.
4. **Respuesta en frecuencia**: Atenuación inferior a 1–3 dB en la banda crítica del dron (100 Hz – 1000 Hz).

---

## 📡 Protocolo LoRa & Trama Binaria Compacta (8 Bytes)

Para enlaces sin WiFi perimetral, la estructura binaria optimizada minimiza el *AirTime* de emisión:

```
+----------+----------+----------+----------+----------+----------+----------+----------+
| Byte 0   | Byte 1   | Byte 2   | Byte 3   | Byte 4   | Byte 5   | Byte 6   | Byte 7   |
| Magic    | NodeID   | Status   | Azimuth  | Elev     | Freq LSB | Freq MSB | CRC-8    |
| (0xAA)   | (1-255)  | (Tier+D) | (Az / 2) | (0-90°)  | (f0 Hz)  | (f0 Hz)  | (Dallas) |
+----------+----------+----------+----------+----------+----------+----------+----------+
```

---

## 🚀 Instalación y Puesta en Marcha

### Requisitos
- [PlatformIO](https://platformio.org/)
- ESP32-S3 DevKitC-1 (8MB Flash o superior)
- 4x Micrófonos MEMS I2S INMP441

### 1. Clonar el repositorio
```bash
git clone https://github.com/ipsec84/DronRadar.git
cd DronRadar
```

### 2. Configurar Credenciales
Copia la plantilla de credenciales en `wifi_test/src/`:
```bash
cp wifi_test/src/wifi_credentials.h.example wifi_test/src/wifi_credentials.h
```
Edita `wifi_credentials.h` con tu SSID WiFi y contraseña.

### 3. Compilar y Subir Firmware
```bash
cd wifi_test
pio run --target upload
```

### 4. Actualización Remota por Web OTA
Una vez instalado el nodo en el mástil o caja estanca, puedes compilar una nueva versión:
```bash
pio run
```
Y subir el archivo generado `.pio/build/esp32s3/firmware.bin` directamente a través del navegador en:
```
http://<IP_DEL_ESP32>/
```
en la sección **⚡ Mantenimiento Remoto & Web OTA**.

---

## 📜 Licencia y Agradecimientos

- Motor de detección armónica basado en la investigación abierta de **VolAnti**.
- Licencia MIT.

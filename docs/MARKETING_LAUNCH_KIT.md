# 🚀 DronRadar - Kit de Difusión, Lanzamiento y Atracción de Usuarios

Este kit contiene los textos, publicaciones, títulos y estrategias de difusión listos para publicar en las principales comunidades tecnológicas, de defensa y de código abierto para dar a conocer **DronRadar** y conseguir estrellas, usuarios y colaboradores en GitHub.

---

## 📌 Enlaces Oficiales del Proyecto
- **Repositorio Público:** [https://github.com/ipsec84/DronRadar](https://github.com/ipsec84/DronRadar)
- **Vídeo de Demostración (HD):** [https://github.com/ipsec84/DronRadar/blob/main/docs/dronradar_demo.mp4](https://github.com/ipsec84/DronRadar/blob/main/docs/dronradar_demo.mp4)
- **GIF Animado del Radar Táctico:** `https://raw.githubusercontent.com/ipsec84/DronRadar/main/docs/dronradar_demo.gif`

---

## 1. 🌐 Publicaciones Listas para Reddit

### Post A: Para `r/esp32` (70k+ desarrolladores embedded)
**Subreddit:** `r/esp32`  
**Flair:** Project / Showcase  
**Título:**  
`I built an open-source Acoustic 3D Drone Radar using an ESP32-S3 and 4x I2S MEMS microphones (Real-time Harmonic Comb + DoA + Web HUD)`

**Cuerpo:**
```markdown
Hey everyone! 👋

I wanted to share an open-source project I've been developing: **DronRadar** — an autonomous acoustic drone detection and 3D direction-of-arrival (DoA) sentinel node running entirely on an ESP32-S3.

### Why Acoustic instead of RF?
Commercial and FPV drones (like the DJI Avata or fiber-optic drones) can fly under complete radio silence, frequency hopping, or encrypted protocols where standard RF scanners fail. Propeller physics can't be switched off: 4 spinning blades produce an unmistakable harmonic comb (f0 fundamental frequency and integer overtones) that cuts through background noise.

### Architecture Highlights:
- **Dual Core Execution:** Core 0 handles a continuous DMA audio task across two independent I2S buses (4x INMP441 mics at 16kHz). Core 1 runs the FFT front-end, harmonic comb classifier, and Doppler tracking.
- **3D Direction of Arrival (DoA):** Uses phase cross-correlation between orthogonal mic pairs to compute 360° horizontal azimuth and vertical elevation.
- **Live Tactical Web HUD:** Embedded HTML5 Canvas PPI radar, real-time audio VU-meters, and 48-bin spectral waterfall.
- **Home Assistant Native:** Zero-configuration MQTT Auto-Discovery.
- **Web OTA Maintenance:** Full drag-and-drop firmware flashing via browser.

The project is completely free and open-source under the MIT license. Check it out, test it on your ESP32-S3, and let me know your feedback!

⭐ GitHub Repository: https://github.com/ipsec84/DronRadar
Demo GIF: https://raw.githubusercontent.com/ipsec84/DronRadar/main/docs/dronradar_demo.gif
```

---

### Post B: Para `r/homeassistant` (350k+ entusiastas domótica y seguridad)
**Subreddit:** `r/homeassistant`  
**Flair:** Project / Share Your Projects  
**Título:**  
`Add real-time Drone Detection to Home Assistant with a $25 ESP32-S3 array (MQTT Auto-Discovery + Bearing + Drone Classifier)`

**Cuerpo:**
```markdown
Hi all!

If you're looking to upgrade your outdoor perimeter security, I've created **DronRadar**, an open-source acoustic drone detector built for the ESP32-S3 that integrates automatically with Home Assistant via MQTT Auto-Discovery.

### What it exposes to Home Assistant:
- `binary_sensor.dronradar_state`: Real-time Drone Alert (ON/OFF).
- `sensor.dronradar_azimuth`: Compass bearing to target (0° - 360°).
- `sensor.dronradar_elevation`: Vertical angle (Low/Grazing, Mid, Overhead).
- `sensor.dronradar_type`: Estimated category (FPV Racing, DJI Mavic/Air, Heavy-Lift).
- `sensor.dronradar_doppler`: Approaching / Receding / Hovering indicator.

Hardware cost is under $25 (ESP32-S3 DevKit + 4x INMP441 microphones). No custom YAML needed — enter your MQTT broker IP in the device's web portal and all entities appear automatically.

Code, schematics, and full setup guide:
https://github.com/ipsec84/DronRadar
```

---

### Post C: Para `r/drones` y `r/fpv`
**Subreddit:** `r/drones` / `r/fpv`  
**Título:**  
`Tested acoustic detection against a DJI Avata 2: Here is how an ESP32-S3 locks onto propeller harmonics at 100+ meters`

**Cuerpo:**
```markdown
Hey pilots,

I've been working on an open-source passive acoustic detection project called DronRadar. We recently benchmarked it against a DJI Avata 2 and 5" FPV quads.

Propeller chopping frequencies create a unique spectral fingerprint (e.g. 244 Hz fundamental on the Avata 2). By analyzing 12 harmonic teeth with an FFT comb filter, the system can distinguish between quadcopters, fixed-wing, and ambient wind/traffic noise.

Video demo and full open-source repo:
https://github.com/ipsec84/DronRadar
```

---

## 2. 🐦 Hilo para X (Twitter) / LinkedIn

**Tweet 1 (Gancho):**
```text
🚨 Presenting DronRadar: An open-source 3D Acoustic Drone Detection Array built on the #ESP32-S3.

Listen to the unavoidable sound of drone propellers up to 104 meters away.

⚡ 100% Passive (Zero RF emissions)
🎯 3D Azimuth & Elevation Tracking
🏠 Native @home_assistant MQTT Integration

Demo 👇 [Adjuntar dronradar_demo.gif]
```

**Tweet 2:**
```text
Why acoustics over RF?
Modern FPV drones and tethered UAVs can fly in total radio silence. But spinning blades cannot switch off physics.

DronRadar analyzes the harmonic comb (f0 + integer overtones) using a 4x I2S MEMS array running dual-core DSP on ESP32-S3.
```

**Tweet 3:**
```text
Completely free, open-source (MIT), and built for less than $25 in components.

⭐ Star the repository on GitHub:
https://github.com/ipsec84/DronRadar

#DroneDefense #IoT #OpenSource #CyberSecurity #Hardware #Embedded
```

---

## 3. 🛠️ Envío a Hackaday.io / Hackster.io

- **Project Name:** DronRadar: ESP32-S3 Acoustic UAV Sentry Array
- **Tagline:** Low-cost passive acoustic drone detection, 3D DoA localization, and tactical mesh array.
- **Hardware Components:**
  1. ESP32-S3 DevKitC-1 N16R8
  2. 4x INMP441 Omnidirectional Digital I2S MEMS Microphones
  3. WS2812B RGB Tactical Indicator LED
  4. IP67 Weatherproof outdoor housing with acoustic vents
- **Software:** PlatformIO, ESP-IDF FreeRTOS Dual-Core, VolAnti Comb Filter Engine, Home Assistant MQTT.
- **Project URL:** `https://github.com/ipsec84/DronRadar`

---

## 4. 📈 Consejos para Ganar Tracción Rápida

1. **Publicar en horas pico de EE.UU. y Europa:**  
   Martes o Miércoles entre las 14:00 y las 17:00 UTC (máxima visibilidad en Reddit y Hacker News).
2. **Subir a *Show HN* en Hacker News:**  
   Título: `Show HN: DronRadar – Open-source acoustic drone detection on ESP32-S3`  
   URL: `https://github.com/ipsec84/DronRadar`
3. **Compartir en canales de Discord de Home Assistant y ESP32.**

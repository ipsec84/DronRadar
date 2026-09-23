# 🛰️ DRONRADAR SENTRY - DOSSIER TÉCNICO Y PRESENTACIÓN EJECUTIVA A4

**Sistema Autónomo de Detección, Clasificación y Geolocalización Acústica 3D de UAVs**  
*Clasificación:* Documento Técnico A4 | *Plataforma:* ESP32-S3 + Matriz 4x I2S | *Versión:* v2.1 Operativa  

---

## 🚨 EL FACTOR DECISIVO: DRONES INMUNES A INHIBIDORES (FIBRA ÓPTICA Y VUELO AUTÓNOMO)

> **EL PROBLEMA ACTUAL:** Los sistemas anti-dron convencionales basados en escaneo de radiofrecuencia (RF) y pistolas inhibidoras (*jammers*) quedan **100% inútiles** ante la nueva generación de drones de combate guiados por **Fibra Óptica desenrollable** o drones con navegación autónoma inercial por GPS desconectado.  
> Al no emitir señales electromagnéticas de radio ni depender de GPS, son totalmente invisibles para la guerra electrónica.  
> 
> **LA SOLUCIÓN DRONRADAR:** Ningún dron puede anular las leyes de la física: sus rotores y hélices son acústicamente ineludibles. **DronRadar detecta, clasifica y geolocaliza la amenaza en 360° y 3D sin importar que esté controlado por fibra óptica, radiofrecuencia o piloto automático.**

---

## 🧭 CAPACIDADES Y ARQUITECTURA TÉCNICA

- **Filtro en Peine FFT (Comb Filter)**: Discriminación armónica del tono fundamental de pala ($f_0$ entre 100 y 1000 Hz) aislando el objetivo de ruidos continuos (tráfico, viento, lluvia).
- **Geolocalización 3D (DoA 360° + Elevación)**: Matriz tetra-micrófono ortogonal que calcula en tiempo real el rumbo horizontal (0-360°) y el ángulo de elevación cenital (0-90°) con categoría de altitud (*Rasante / Media / Cenital*).
- **Clasificador de Firma & Doppler**: Identifica el perfil de amenaza (*DJI Mini/Mavic*, *FPV Racing*, *Dron Pesado*) y la tasa de velocidad Doppler (*Aproximándose ⚠️ / Alejándose 🛫 / Estático ⏸️*).
- **Malla Inter-Nodo (Mesh Broadcast UDP)**: Comunicación automática entre estaciones perimetrales para cálculo de rumbos cruzados y triangulación perimetral.
- **Predisposición LoRa (8 Bytes)**: Trama binaria ultraligera para enlaces perimetrales de kilómetros sin WiFi ni cables.
- **Actualización Web OTA Remota (`/update`)**: Flasheo de firmware inalámbrico por WiFi dentro de la caja estanca sin desmontar mástiles ni usar cables USB.

---

## ⚖️ VENTAJAS E INCONVENIENTES

### ✅ Ventajas Tácticas y Operativas
1. **Indetectable y Pasivo (Zero Emission)**: Al no emitir radar ni microondas, no puede ser localizado por contramedidas enemigas.
2. **Inmune a Jamming y Spoofing**: Inmune a inhibidores de radiofrecuencia, interferencias electromagnéticas o cegado de GPS.
3. **Detección de Drones de Fibra Óptica**: Detecta drones tethered, guiados por cable óptico y planeadores en estricto silencio de radio.
4. **Coste Fraccionario (< 60€ por estación)**: Permite desplegar mallas de decenas de nodos para cerrar fincas o instalaciones críticas frente a radares militares de >50.000€.
5. **Autonomía y Despliegue Exterior**: Preparado para caja estanca IP67, alimentación solar/PoE y actualización inalámbrica OTA.

### ⚠️ Limitaciones e Inconvenientes
1. **Alcance acústico finito**: 30 a 60 m para drones ultra-silenciosos (<250g); 100 a 250 m para drones comerciales o industriales (físicamente limitado por la atenuación del aire).
2. **Vientos extremos**: Vientos superiores a 50 km/h elevan el suelo de ruido en la membrana (mitigable con pantallas cónicas y espuma cortavientos).
3. **Línea de visión acústica**: Muros de hormigón densos amortiguan el sonido; requiere instalación en altura con visión despejada al cielo.

---

## 📊 TABLA DE RENDIMIENTO DE DETECCIÓN (Micrófonos Cenitales)

| Categoría de Amenaza | Frecuencia Fundamental ($f_0$) | Alcance Día (Urbano) | Alcance Noche (Rural) | Detección RF / Jammer | Detección DronRadar |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **DJI Mini 2/3/4 (<250g)** | 200 – 260 Hz | 30 – 45 m | 50 – 65 m | Inhibible si usa RF | **100% Acústico (OK)** |
| **DJI Mavic 3 / Comercial** | 170 – 220 Hz | 70 – 100 m | 110 – 140 m | Inhibible si usa RF | **100% Acústico (OK)** |
| **FPV Racing / Dron Kamikaze** | 300 – 550 Hz | 90 – 130 m | 150 – 190 m | Inhibible solo con RF | **100% Acústico (OK)** |
| **Dron Guiado por Fibra Óptica** | 180 – 350 Hz | **70 – 120 m** | **120 – 160 m** | **INMUNE AL JAMMER (0%)** | **100% Acústico (OK)** |
| **Dron Pesado / Carga / Octocóptero** | 90 – 160 Hz | 160 – 240 m | 250 – 350+ m | Variable | **100% Acústico (OK)** |

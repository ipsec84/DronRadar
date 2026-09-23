# 📐 Guía de Ingeniería: Separación Óptima de Micrófonos, Diseño PCB 1:1 y Alternativas de Alta Potencia

Este documento detalla la fundamentación matemática para el diseño de la placa de circuito impreso (PCB) de **DronRadar**, la geometría espacial de la matriz de 4 micrófonos para localización 3D (DoA) y el análisis de micrófonos de alto rendimiento para extender el alcance de detección.

---

## 1. 📏 Separación Óptima entre Micrófonos: Física Acústica

Para que un radar acústico pueda calcular el azimut horizontal (0–360°) y la elevación vertical (0–90°) mediante diferencias de fase ($\Delta\phi$), la distancia $d$ entre los micrófonos debe satisfacer dos restricciones físicas contrapuestas:

```
[M1] <────────────── d = 70.0 mm ──────────────> [M2]
  ▲                                                ▲
  │                                                │
  │                     [ESP32-S3]                 │
  │                                                │
  ▼                                                ▼
[M3] <────────────── d = 70.0 mm ──────────────> [M4]
```

### A. Condición de Nyquist Espacial (Límite Superior: Evitar Aliasing)
Si la distancia $d$ entre dos micrófonos es mayor que la mitad de la longitud de onda de la señal más aguda ($\lambda_{\min} / 2$), se produce **ambigüedad espacial (Spatial Aliasing)**: dos direcciones angulares distintas producirán exactamente la misma diferencia de fase, haciendo imposible saber por dónde viene el dron.
$$\lambda = \frac{c}{f} \quad \implies \quad d \le \frac{c}{2 \cdot f_{\max}}$$
Donde $c \approx 343\text{ m/s}$ (velocidad del sonido en el aire).
* Para abarcar hasta el 3er y 4º armónico de las hélices ($f_{\max} \approx 2400\text{ Hz}$):
  $$d_{\max} = \frac{343\text{ m/s}}{2 \times 2400\text{ Hz}} = 0.0714\text{ m} \approx \mathbf{71.4\text{ mm}}$$

### B. Relación Señal-Ruido a Bajas Frecuencias (Límite Inferior)
A frecuencias bajas (fundamental de 120 Hz de un dron pesado o 200 Hz de un DJI Mini), la longitud de onda es muy grande ($\lambda \approx 1.7\text{ m}$). Si los micrófonos estuvieran a solo 20 mm de distancia, el desfase acústico sería de apenas 4 grados, confundiéndose con el ruido térmico del sensor.

### 🎯 Conclusión del Diseño: Matriz Cuadrada de 70.0 mm
* **Separación ortogonal (M1 ↔ M2 y M1 ↔ M3)**: **$70.0\text{ mm}$**
* **Frecuencia límite de ambigüedad**: **$2.450\text{ Hz}$** (Cubre el 100% del rango de rotores de drones sin aliasing).
* **Diagonal entre opuestos (M1 ↔ M4)**: $\sqrt{70^2 + 70^2} = \mathbf{98.99\text{ mm}}$ ($\approx 10\text{ cm}$).
* **Tamaño total de la PCB**: **$120 \times 120\text{ mm}$** (Permite encajar en cajas estancas comerciales estándar IP65/IP67 de $150 \times 150\text{ mm}$).

---

## 2. 🖨️ Plano de la Placa PCB a Escala 1:1 (Milimétrica Real)

Se ha generado el plano técnico acotado a escala real 1:1 en formato vectorial SVG:
📁 [docs/PLANO_PCB_TAMANO_REAL_1A1.svg](file:///c:/Users/Alberto_G/SynologyDrive/DATOS/PROXMOX/DRONRADAR/docs/PLANO_PCB_TAMANO_REAL_1A1.svg)

### Cómo imprimirlo a tamaño real 1:1 en papel:
1. Abre el archivo `PLANO_PCB_TAMANO_REAL_1A1.svg` en Google Chrome, Edge o Inkscape.
2. Pulsa `Ctrl + P` (Imprimir).
3. **MUY IMPORTANTE**: En los ajustes de impresión, selecciona **Escala: 100% (o Tamaño Real)**. No marques "Ajustar a página".
4. El folio impreso tendrá exactamente las dimensiones reales para usarlo como **plantilla física de taladrado y montaje** o referencia para fabricación en KiCad / JLCPCB.

---

## 3. 🎙️ ¿Existen Micrófonos Más Potentes? Comparativa Técnica

El micrófono actual (**INMP441**) ofrece una excelente relación calidad/precio (~1,50 €), pero su sensibilidad está limitada por su suelo de ruido interno. Existen alternativas superiores según el objetivo:

| Micrófono / Solución | Tipo / Bus | SNR (Relación Señal/Ruido) | Suelo de Ruido Propio (SPL) | Mejora de Alcance Frente a INMP441 | Observaciones |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **INMP441 (Actual)** | Digital I2S | **61 dBA** | ~33 dBA | Baseline (1.0x) | Económico, probado, disponible en módulos. |
| **ICS-43434 (TDK InvenSense)** | Digital I2S | **65 dBA** (+4 dB) | ~29 dBA | **+35% a +45%** | **Sustituto directo (Pin a pin)**. Mismo bus I2S y código, capta armónicos más lejanos. |
| **Infineon IM73A135V01 (XENSIV)** | Digital I2S / PDM | **73 dBA** (+12 dB) | **21 dBA** | **+70% a +90%** | Nivel profesional. Suelo de ruido ultra-bajo; requiere diseño PCB específico para soldadura SMD. |
| **Primo EM272 + ADC Externo (ES7210 / PCM1808)** | Analógico + I2S ADC | **80 dBA** (+19 dB) | **14 dBA** | **+120% a +150%** | Utilizado en bioacústica profesional militar. Capta murmullos a gran distancia. Requiere circuitería analógica. |

### 🚀 Ganancia Pasiva Gratis: Reflectores Acústicos Cónicos (+6 a +10 dB)
Antes de cambiar de micrófono, la física permite multiplicar el alcance de forma pasiva:
* Montar cada micrófono en la base de un **pequeño cono o bocina exponencial impresa en 3D** (ángulo de apertura de 45° a 60° orientado al cielo).
* Esto concentra la presión sonora de las ondas incidentes cenitales proporcionando entre **+6 dB y +10 dB de ganancia acústica real** (equivalente a triplicar la potencia del sensor) sin añadir ni un milivatio de consumo ni ruido electrónico.

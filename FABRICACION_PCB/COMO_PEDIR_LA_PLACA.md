# 🛠️ Guía Paso a Paso: Cómo Mandar a Fabricar la Placa de Circuitos de DronRadar

Para que una fábrica especializada te fabrique la placa física con **todas las pistas de cobre, orificios metalizados y pads listos para soldar los componentes**, se utiliza un formato estándar internacional llamado **Archivos Gerber**.

En tu proyecto ya tienes el paquete de fabricación 100% terminado y listo para enviar.

---

## 📁 Archivos de Fabricación Preparados
Los tienes en la carpeta:
`DRONRADAR\FABRICACION_PCB\`

1. **`VolAnti_revA2_gerbers.zip`** (1.8 MB) ➜ **ESTE ES EL ARCHIVO PRINCIPAL**. Contiene todas las capas de cobre (4 capas), máscaras de soldadura, serigrafía y perforaciones de taladro.
2. **`bom.csv`** (Lista de materiales con códigos LCSC) *(Solo si quieres que la fábrica suelde los chips por ti)*.
3. **`pick-and-place.csv`** (Coordenadas X-Y de las máquinas robóticas) *(Solo si quieres montaje automático)*.

---

## 🏭 Dónde encargarla
Los fabricantes más rápidos, económicos y estándar del mercado son:
* **JLCPCB** ([https://jlcpcb.com](https://jlcpcb.com)) *(Recomendado por precio: ~4€ por 5 placas)*
* **PCBWay** ([https://www.pcbway.com](https://www.pcbway.com))
* **AISLER** ([https://aisler.net](https://aisler.net)) *(Fabricación europea en Alemania/Holanda)*

---

## 🚀 Proceso de Pedido en JLCPCB (3 minutos)

### Paso 1: Subir el archivo
1. Entra en [jlcpcb.com](https://jlcpcb.com).
2. En la página principal verás un botón azul que dice **"Add Gerber file"** (o arrastra directamente el archivo).
3. Arrastra y suelta el archivo **`VolAnti_revA2_gerbers.zip`**.
4. El sistema lo analizará en 5 segundos y te mostrará una **vista previa 3D interactiva** de la placa (las pistas de cobre, los pads y la serigrafía).

### Paso 2: Configuración de la Placa (La web lo autodetecta)
Verifica que los parámetros seleccionados coincidan con el diseño:
* **Dimensions:** 84 mm × 84 mm (Automático)
* **Layers (Capas):** 4 Layers
* **PCB Qty (Cantidad):** 5 unidades (es el mínimo estándar)
* **Thickness (Grosor):** 1.6 mm
* **PCB Color:** Verde clásico, Negro mate, Azul o el que prefieras.
* **Surface Finish (Acabado de pads):**
  * *Económico:* **HASL (with lead)** o **Lead-free HASL** (estaño plateado).
  * *Recomendado militar/profesional:* **ENIG (Electroless Nickel Immersion Gold)** (los pads vienen bañados en oro para máxima durabilidad y mejor soldadura de micros MEMS).

---

## 🎯 ¿Qué opción elegir según cómo quieras soldar?

### OPCIÓN A: "Solo quiero la placa física para soldar yo todo" (Bare PCB)
* En la parte inferior, deja **desactivada** la casilla *"PCB Assembly"*.
* Pulsa **"Save to Cart"** (Guardar en el carrito) y tramita el envío.
* **Coste aproximado:** ~5 € a 15 € por 5 placas (según acabado ENIG o HASL) + gastos de envío.
* **Qué recibes:** 5 placas de circuito impreso de fibra de vidrio FR-4 con todas las pistas de cobre internas y externas, pistas blindadas de audio, barniz protector y pads estañados/dorados listos para que apliques estaño y tus propios componentes.

> ⚠️ **Aviso técnico sobre los micrófonos:**
> Los 4 micrófonos del diseño VolAnti Rev A2 son tipo MEMS SMD de puerto inferior (*bottom-port*). No tienen patillas pasantes, sino bolitas de estaño por debajo. Para soldarlos a mano necesitas **pasta de soldar y tobera de aire caliente** (o placa calefactora). Si no dispones de aire caliente, mira la Opción B.

---

### OPCIÓN B: "Que la fábrica me suelde los chips difíciles y yo solo sueldo los conectores" (SMT Assembly)
* En la web de JLCPCB, **activa** el interruptor **"PCB Assembly"**.
* En el paso siguiente te pedirá dos archivos:
  * BOM: Sube **`bom.csv`**
  * CPL / Centroid: Sube **`pick-and-place.csv`**
* La máquina colocará y soldará con láser/horno el procesador ESP32-S3, los 4 micrófonos MEMS calibrados, el regulador de energía y el chip LoRa.
* Solo dejarán sin soldar 4 conectores grandes (antena externa, zumbador y pines) para que los sueldes tú con un soldador normal de punta.
* **Coste aproximado:** ~25 € - 35 € en total por 5 placas completamente montadas con chips originales.
* **Plazo de entrega:** ~10 a 14 días en la puerta de tu casa.

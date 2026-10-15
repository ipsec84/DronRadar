import cv2
import numpy as np
import math
import os
from PIL import Image, ImageDraw, ImageFont

def render_demo():
    width = 1280
    height = 720
    fps = 30
    duration_sec = 18
    total_frames = fps * duration_sec
    
    out_dir = os.path.dirname(os.path.abspath(__file__))
    video_path = os.path.join(out_dir, "dronradar_demo.mp4")
    gif_path = os.path.join(out_dir, "dronradar_demo.gif")
    poster_path = os.path.join(out_dir, "images", "dronradar_preview.jpg")
    os.makedirs(os.path.join(out_dir, "images"), exist_ok=True)
    
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    out = cv2.VideoWriter(video_path, fourcc, float(fps), (width, height))
    
    print(f"Generando video de demostración: {video_path} ({total_frames} frames)...")
    
    # Intentar cargar fuentes estándar de Windows
    font_large = None
    font_mid = None
    font_small = None
    font_mono = None
    try:
        font_large = ImageFont.truetype("arialbd.ttf", 24)
        font_mid = ImageFont.truetype("arialbd.ttf", 18)
        font_small = ImageFont.truetype("arial.ttf", 14)
        font_mono = ImageFont.truetype("consola.ttf", 15)
        font_huge = ImageFont.truetype("arialbd.ttf", 32)
    except:
        font_large = font_mid = font_small = font_mono = font_huge = ImageFont.load_default()

    frames_for_gif = []

    for frame_idx in range(total_frames):
        t = frame_idx / fps
        img = Image.new('RGB', (width, height), color=(10, 14, 20))
        draw = ImageDraw.Draw(img)
        
        # 1. ENCABEZADO SUPERIOR TÁCTICO
        draw.rectangle([(0, 0), (width, 55)], fill=(16, 22, 34), outline=(35, 45, 65))
        draw.text((20, 14), "DRONRADAR C4ISR", fill=(0, 255, 120), font=font_large)
        draw.text((250, 18), "| ACOUSTIC SENTINEL & TACTICAL RADAR ARRAY [ESP32-S3]", fill=(180, 200, 220), font=font_small)
        
        # Reloj y estado de red
        node_status = "NODE: DR-01 | MESH: 4 PEERS | MQTT: CONNECTED"
        draw.text((880, 18), node_status, fill=(100, 180, 255), font=font_mono)
        
        # 2. RADAR CIRCULAR TÁCTICO (LADO IZQUIERDO)
        rcx, rcy = 280, 390
        radius = 260
        
        # Fondo del radar
        draw.ellipse([(rcx - radius, rcy - radius), (rcx + radius, rcy + radius)], fill=(8, 16, 12), outline=(30, 80, 50), width=2)
        
        # Anillos de rango: 25m, 50m, 75m, 100m
        for i, dist in enumerate([25, 50, 75, 100], 1):
            r = int((radius / 4) * i)
            draw.ellipse([(rcx - r, rcy - r), (rcx + r, rcy + r)], outline=(20, 60, 35), width=1)
            draw.text((rcx + 6, rcy - r + 3), f"{dist}m", fill=(60, 140, 80), font=font_small)
            
        # Cruz cardinal
        draw.line([(rcx - radius, rcy), (rcx + radius, rcy)], fill=(20, 70, 40), width=1)
        draw.line([(rcx, rcy - radius), (rcx, rcy + radius)], fill=(20, 70, 40), width=1)
        draw.text((rcx - 6, rcy - radius + 8), "N", fill=(0, 255, 120), font=font_mid)
        draw.text((rcx - 6, rcy + radius - 24), "S", fill=(0, 255, 120), font=font_mid)
        draw.text((rcx + radius - 20, rcy - 10), "E", fill=(0, 255, 120), font=font_mid)
        draw.text((rcx - radius + 8, rcy - 10), "W", fill=(0, 255, 120), font=font_mid)
        
        # Haz giratorio del radar (360° en 3 segundos)
        sweep_angle = (t * 2.5) % (2 * math.pi)
        sweep_len = radius
        sx = rcx + int(sweep_len * math.cos(sweep_angle))
        sy = rcy + int(sweep_len * math.sin(sweep_angle))
        
        # Determinar fase de la simulación
        # 0s - 4s: Búsqueda activa (Verde)
        # 4s - 8s: Detección preliminar (Amarillo)
        # 8s - 15s: Alarma fijada (Rojo)
        # 15s - 18s: Outro / Malla
        
        if t < 4.0:
            phase = "LISTENING"
            state_color = (0, 255, 100)
            state_text = "🟢 ESCUCHANDO (DESPEJADO)"
            score = 0.55 + 0.15 * math.sin(t * 3)
            conf = 0
            teeth = 4
            freq = 1500 + 50 * math.sin(t * 2)
            target_visible = False
        elif t < 8.0:
            phase = "DETECTING"
            state_color = (255, 215, 0)
            state_text = "🟡 PRECAUCIÓN: DETECTANDO FIRMA"
            progress = (t - 4.0) / 4.0
            score = 0.75 + 0.25 * progress
            conf = int(progress * 50)
            teeth = int(6 + 4 * progress)
            freq = 244.0 + 10 * math.sin(t * 4)
            target_visible = True
            target_alpha = progress
        elif t < 15.0:
            phase = "ALERT"
            state_color = (255, 50, 50)
            state_text = "🚨 ALERTA CONFIRMADA: DRON LOCALIZADO"
            score = 1.15 + 0.10 * math.sin(t * 5)
            conf = min(95, int(50 + (t - 8.0) * 10))
            teeth = 12
            freq = 244.0 + 3 * math.sin(t * 6)
            target_visible = True
            target_alpha = 1.0
        else:
            phase = "MESH"
            state_color = (50, 160, 255)
            state_text = "📡 RED MALLA & HOME ASSISTANT SINCRONIZADO"
            score = 1.10
            conf = 95
            teeth = 12
            freq = 244.0
            target_visible = True
            target_alpha = 1.0

        # Dibujar haz de barrido (color según estado)
        sweep_c = (state_color[0]//2, state_color[1]//2, state_color[2]//2)
        draw.line([(rcx, rcy), (sx, sy)], fill=sweep_c, width=2)
        
        # Objetivo en el radar (Rumbo 170°, Rango ~22m)
        target_dist_px = int(radius * 0.35)
        target_rad = math.radians(170)
        tx = rcx + int(target_dist_px * math.sin(target_rad))
        ty = rcy + int(target_dist_px * math.cos(target_rad))
        
        if target_visible:
            pulse = (math.sin(t * 10) + 1.0) / 2.0
            p_radius = int(8 + 6 * pulse)
            # Retícula de blanco
            draw.ellipse([(tx - p_radius, ty - p_radius), (tx + p_radius, ty + p_radius)], outline=state_color, width=2)
            draw.line([(tx - p_radius - 4, ty), (tx + p_radius + 4, ty)], fill=state_color, width=2)
            draw.line([(tx, ty - p_radius - 4), (tx, ty + p_radius + 4)], fill=state_color, width=2)
            
            # Etiqueta de blanco
            draw.text((tx + 16, ty - 18), "OBJETIVO #1: FPV DRONE", fill=(255, 255, 255), font=font_small)
            draw.text((tx + 16, ty - 2), f"Rumbo: 170° | Rango: 22m", fill=state_color, font=font_mono)
            draw.text((tx + 16, ty + 14), f"f0: {freq:.1f} Hz | Conf: {conf}%", fill=(200, 220, 255), font=font_mono)

        # 3. PANEL DERECHO - TELEMETRÍA Y FIRMA ARMÓNICA
        px = 590
        
        # Card 1: Estado del Sensor y Clasificación
        draw.rectangle([(px, 75), (width - 25, 205)], fill=(16, 22, 34), outline=(40, 50, 75), width=1)
        draw.text((px + 18, 90), state_text, fill=state_color, font=font_large)
        
        if phase == "LISTENING":
            d_class = "Ninguno (Espacio aéreo despejado)"
            d_doppler = "Estático / Sin señal"
        else:
            d_class = "Clase 1: FPV / Alta Velocidad (DJI Avata 2)"
            d_doppler = "Aproximación Vectorial (+0.8 Hz/s)"
            
        draw.text((px + 18, 125), f"Clasificación: {d_class}", fill=(240, 245, 255), font=font_mid)
        draw.text((px + 18, 150), f"Frecuencia Fundamental: {freq:.1f} Hz  |  Dientes Armónicos: {teeth}/12", fill=(180, 200, 220), font=font_mono)
        draw.text((px + 18, 172), f"Score Comb: {score:.2f} (Umbral: 0.85)  |  Certeza: {conf}%  |  Doppler: {d_doppler}", fill=(180, 200, 220), font=font_mono)
        
        # Card 2: 4 Micrófonos I2S (VU Meters)
        draw.rectangle([(px, 220), (width - 25, 375)], fill=(16, 22, 34), outline=(40, 50, 75), width=1)
        draw.text((px + 18, 232), "ARRAY DE 4 MICRÓFONOS I2S (INMP441)", fill=(100, 180, 255), font=font_mid)
        
        mic_names = ["M1 (Front Left)", "M2 (Front Right)", "M3 (Rear Left)", "M4 (Rear Right)"]
        for m_idx in range(4):
            my = 265 + m_idx * 24
            draw.text((px + 18, my), mic_names[m_idx], fill=(180, 195, 215), font=font_small)
            
            # Barra VU Meter
            if phase == "LISTENING":
                m_level = 0.15 + 0.08 * math.sin(t * 4 + m_idx)
            elif phase == "DETECTING":
                m_level = 0.45 + 0.15 * math.sin(t * 8 + m_idx)
            else:
                m_level = 0.78 + 0.18 * math.sin(t * 12 + m_idx)
                
            bw = int(320 * m_level)
            # Fondo barra
            draw.rectangle([(px + 170, my + 2), (px + 490, my + 14)], fill=(20, 28, 42))
            # Relleno barra con gradiente simulado
            b_col = (0, 220, 100) if m_level < 0.6 else ((255, 200, 0) if m_level < 0.8 else (255, 60, 60))
            draw.rectangle([(px + 170, my + 2), (px + 170 + bw, my + 14)], fill=b_col)
            # Valor RMS
            rms_val = int(m_level * 1800)
            draw.text((px + 505, my - 1), f"RMS:{rms_val:4d}", fill=(150, 175, 200), font=font_mono)

        # Card 3: Espectro Armónico (Peine de 12 Armónicos)
        draw.rectangle([(px, 390), (width - 25, 545)], fill=(16, 22, 34), outline=(40, 50, 75), width=1)
        draw.text((px + 18, 402), "ESPECTRO EN TIEMPO REAL (PEINE ARMÓNICO FFT VOLANTI)", fill=(100, 180, 255), font=font_mid)
        
        # Dibujar 32 barras espectrales
        for b in range(32):
            bx = px + 25 + b * 19
            if phase == "LISTENING":
                bh = int(10 + 20 * math.sin(t * 2 + b * 0.5)**2)
                bc = (40, 100, 70)
            elif phase == "DETECTING":
                is_harmonic = (b % 4 == 2)
                bh = int((55 if is_harmonic else 15) + 15 * math.sin(t * 5 + b)**2)
                bc = (255, 200, 0) if is_harmonic else (60, 80, 100)
            else:
                is_harmonic = (b % 3 == 1)
                bh = int((85 if is_harmonic else 12) + 10 * math.sin(t * 8 + b)**2)
                bc = (255, 50, 50) if is_harmonic else (60, 80, 100)
                
            draw.rectangle([(bx, 520 - bh), (bx + 14, 520)], fill=bc)
        draw.text((px + 25, 525), "70 Hz               Frecuencia Fundamental (f0)               2000 Hz", fill=(100, 130, 160), font=font_small)

        # Card 4: Barra Inferior de Integración Táctica
        draw.rectangle([(px, 560), (width - 25, 680)], fill=(16, 22, 34), outline=(40, 50, 75), width=1)
        if phase != "MESH":
            draw.text((px + 18, 575), "INTEGRACIONES & TELEMETRÍA AUTOMÁTICA:", fill=(200, 220, 240), font=font_mid)
            draw.text((px + 18, 605), "• Home Assistant (MQTT Auto-Discovery): DronDetectado ON, Azimut, Elevación", fill=(100, 255, 150), font=font_small)
            draw.text((px + 18, 627), "• Malla Inter-Nodo (Mesh Broadcast UDP): Triangulación distribuida multi-sensor", fill=(100, 200, 255), font=font_small)
            draw.text((px + 18, 649), "• Enlace LoRa (8 Bytes Trama Compacta): Cobertura perimetral hasta 15 km", fill=(255, 200, 100), font=font_small)
        else:
            draw.text((px + 18, 575), "PROYECTO DEFENSA OPEN SOURCE:", fill=(0, 255, 120), font=font_large)
            draw.text((px + 18, 610), "GitHub: https://github.com/ipsec84/DronRadar", fill=(255, 255, 255), font=font_mid)
            draw.text((px + 18, 640), "¡Estrella el repo, pruébalo en tu ESP32-S3 y colabora!", fill=(255, 215, 0), font=font_small)

        # Convertir imagen PIL a OpenCV BGR
        cv_img = cv2.cvtColor(np.array(img), cv2.COLOR_RGB2BGR)
        out.write(cv_img)
        
        # Guardar poster del frame de alerta (t = 11s)
        if frame_idx == int(11.0 * fps):
            img.save(poster_path, quality=95)
            print(f"Poster guardado en: {poster_path}")
            
        # Muestrear frames para el GIF animado (cada 4 frames para que no sea muy pesado)
        if frame_idx % 4 == 0:
            frames_for_gif.append(img.resize((640, 360), Image.Resampling.LANCZOS))
            
    out.release()
    print("Video MP4 generado exitosamente.")
    
    # Guardar GIF animado
    if frames_for_gif:
        print(f"Generando GIF animado: {gif_path}...")
        frames_for_gif[0].save(
            gif_path,
            save_all=True,
            append_images=frames_for_gif[1:],
            optimize=True,
            duration=133,  # ~7.5 fps
            loop=0
        )
        print("GIF generado exitosamente.")

if __name__ == "__main__":
    render_demo()

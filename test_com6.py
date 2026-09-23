import serial
import time

PORT = 'COM6'
BAUD = 115200

print(f"Abriendo {PORT}...")
try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
except Exception as e:
    print(f"Error abriendo puerto: {e}")
    exit(1)

# Probar si el ESP32 está corriendo VolAnti o firmware arduino
time.sleep(0.5)
# Leer buffer previo
while ser.in_waiting:
    print(ser.read(ser.in_waiting).decode('utf-8', errors='replace'), end='')

# Enviar comando 'I' (Information en VolAnti firmware) y '\n'
ser.write(b"I\n")
time.sleep(1)

t0 = time.time()
while time.time() - t0 < 3:
    if ser.in_waiting:
        data = ser.read(ser.in_waiting)
        print(data.decode('utf-8', errors='replace'), end='', flush=True)
    time.sleep(0.1)

ser.close()
print("\n[FIN LECTURA]")

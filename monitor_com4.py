import sys
import serial
import threading

PORT = "COM4"
BAUD = 115200

def read_from_port(ser):
    while True:
        try:
            line = ser.readline()
            if line:
                print(line.decode("utf-8", errors="replace"), end="", flush=True)
        except Exception as e:
            print(f"\n[Error de lectura]: {e}")
            break

def main():
    print(f"--- Conectando a {PORT} @ {BAUD} baudios --- (Presiona Ctrl+C para salir)")
    try:
        ser = serial.Serial(PORT, BAUD, timeout=1)
    except Exception as e:
        print(f"No se pudo abrir el puerto {PORT}: {e}")
        sys.exit(1)

    t = threading.Thread(target=read_from_port, args=(ser,), daemon=True)
    t.start()

    try:
        while True:
            cmd = sys.stdin.readline()
            if not cmd:
                break
            ser.write(cmd.encode("utf-8"))
    except KeyboardInterrupt:
        print("\nDesconectado.")
    finally:
        ser.close()

if __name__ == "__main__":
    main()

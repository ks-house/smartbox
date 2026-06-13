import serial
import time
import sys
import os

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
    log_path = os.path.join(os.path.dirname(__file__), 'serial_output.log')

    print(f"Starting serial capture on {port} for {duration}s...")
    try:
        ser = serial.Serial(port, 115200, timeout=1)
        print(f"Opened {port} successfully.")

        # Reset ESP32 by toggling DTR/RTS to capture setup logs from boot
        ser.setDTR(False)
        ser.setRTS(True)
        time.sleep(0.1)
        ser.setRTS(False)
        time.sleep(0.5)

        end_time = time.time() + duration
        lines = []

        while time.time() < end_time:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore')
                print(line, end='')
                lines.append(line)
            time.sleep(0.01)

        ser.close()

        with open(log_path, 'w', encoding='utf-8') as f:
            f.write(f"=== SERIAL OUTPUT ({port}) ===\n")
            f.writelines(lines)
        print(f"\nSerial capture saved to: {log_path}")

    except Exception as e:
        with open(log_path, 'w', encoding='utf-8') as f:
            f.write(f"Error: {str(e)}\n")
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()

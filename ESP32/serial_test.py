
import serial
import time

PORT = "COM3"
BAUD_RATE = 115200

try:
    esp = serial.Serial(PORT, BAUD_RATE, timeout=1)
    print(f"Connected to ESP32 on {PORT}")
    time.sleep(2)

    print("Waiting for ESP32 serial output...")

    while True:
        if esp.in_waiting:
            data = esp.readline().decode("utf-8", errors="replace").strip()
            if data:
                print("ESP32:", data)

except KeyboardInterrupt:
    print("\nSerial monitoring stopped.")

except serial.SerialException as e:
    print("Serial error:", e)

finally:
    if "esp" in locals() and esp.is_open:
        esp.close()

import serial
import hashlib
import time

# File to send
file_path = 'data.txt'
ser = serial.Serial('COM5', 115200)  # Change to your ESP32 COM port
time.sleep(2)  # Wait for ESP32 to boot

with open(file_path, 'rb') as f:
    file_data = f.read()

# Generate SHA256 hash
file_hash = hashlib.sha256(file_data).hexdigest()

# Send hash
ser.write(f"HASH:{file_hash}\n".encode())
time.sleep(0.5)

# Send file content
ser.write(file_data)
time.sleep(0.5)

# Send END marker
ser.write(b"\nEND\n")
print("✅ Hash, file, and END sent.")

import serial
import wave
import os
PORT = "/dev/cu.usbserial-140"
BAUD = 460800
session_name = "bg1"

ser = serial.Serial(PORT, BAUD, timeout=5)
ser.reset_input_buffer()     
buffer = bytearray()
target_dir = os.path.join("/Users/ryanchu/Documents/TranslatorWebpage/trainingVAD/data", session_name)
os.makedirs(target_dir, exist_ok=True)
OUT_RAW = os.path.join(target_dir, "capture.raw")
while True:
    chunk = ser.read(256)
    print("looking for Start")
    buffer.extend(chunk)
    idx = buffer.find(b"CAPTURE_START")
    if idx != -1:
        buffer = buffer[idx + len("CAPTURE_START"):]
        break


marker_bytes = b"CAPTURE_END"
while True:
      chunk = ser.read(1024)
      if not chunk:
           raise RuntimeError("Timed out reading PCM data")
      buffer.extend(chunk)
      idx = buffer.find(marker_bytes)
      if idx != -1:
        pcm_bytes = buffer[:idx]
        break
      
with open(OUT_RAW, "wb") as f:
    f.write(pcm_bytes)

print(f"Wrote {len(pcm_bytes)} bytes to {OUT_RAW}")
ser.close()

WAV_FILE = os.path.join(target_dir, "out.wav")
SAMPLE_RATE = 16000
CHANNELS = 1
BITS = 16

with open(OUT_RAW, "rb") as src:
    pcm = src.read()

with wave.open(WAV_FILE, "wb") as wav:
    wav.setnchannels(CHANNELS)
    wav.setsampwidth(BITS // 8)
    wav.setframerate(SAMPLE_RATE)
    wav.writeframes(pcm)

print("Saved", WAV_FILE)
import serial
import serial.tools.list_ports
import numpy as np
from PIL import Image
import time
from datetime import datetime

def capture_image(port=None, baudrate=9600, timeout=30):
    """Passive listener — waits for UART data and saves first complete frame"""
    
    if port is None:
        ports = serial.tools.list_ports.comports()
        for p in ports:
            if 'Arduino' in p.description or 'USB Serial' in p.description or 'CH340' in p.description:
                port = p.device
                break
    
    if port is None:
        print("Could not find device.")
        return False
    
    print(f"Listening on {port}...")
    ser = serial.Serial(port, baudrate, timeout=timeout)
    time.sleep(2)
    
    raw_values = []
    upscaled_values = []
    reading_raw = False
    reading_upscaled = False
    
    print("Waiting for data...")
    start = time.time()
    
    while (len(raw_values) < 12 or len(upscaled_values) < 100) and (time.time() - start) < timeout:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            if not line:
                continue
            
            if line == "RAW_3x4":
                reading_raw = True
                reading_upscaled = False
                print("Raw data started")
            elif line == "UPSCALED_10x10":
                reading_upscaled = True
                reading_raw = False
                print("Upscaled data started")
            else:
                try:
                    value = int(line)
                    if reading_raw and len(raw_values) < 12:
                        raw_values.append(value)
                    elif reading_upscaled and len(upscaled_values) < 100:
                        upscaled_values.append(value)
                except ValueError:
                    pass  # ignore non-numeric lines
    
    ser.close()
    
    if len(raw_values) == 12 and len(upscaled_values) == 100:
        import os
        os.makedirs("captures", exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        img_array = np.array(upscaled_values, dtype=np.uint8).reshape(10, 10)
        img_scaled = np.repeat(np.repeat(img_array, 40, axis=0), 40, axis=1)
        img = Image.fromarray(img_scaled, mode='L')
        filename = f"captures/image_{timestamp}.bmp"
        img.save(filename)
        
        print(f"Image saved: {filename}")
        return True
    else:
        print(f"Incomplete: raw={len(raw_values)}, upscaled={len(upscaled_values)}")
        return False

if __name__ == "__main__":
    capture_image()

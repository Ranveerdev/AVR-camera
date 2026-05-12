# simple_camera_capture.py - Simplified version
import serial
import serial.tools.list_ports
import numpy as np
from PIL import Image
import time
from datetime import datetime

def capture_image(port=None, baudrate=9600):
    """Simple function to capture one image and save as BMP"""
    
    # Find port if not specified
    if port is None:
        ports = serial.tools.list_ports.comports()
        for p in ports:
            if 'Arduino' in p.description or 'USB Serial' in p.description:
                port = p.device
                break
    
    if port is None:
        print("Could not find Arduino. Please specify port manually.")
        return False
    
    print(f"Connecting to {port}...")
    ser = serial.Serial(port, baudrate, timeout=10)
    time.sleep(2)
    
    # Send capture command
    ser.write(b'2')
    time.sleep(0.1)
    
    # Read data
    raw_values = []
    upscaled_values = []
    
    reading_raw = False
    reading_upscaled = False
    
    print("Capturing image...")
    
    start_time = time.time()
    timeout = 30  # 30 second timeout
    
    while (len(raw_values) < 12 or len(upscaled_values) < 100) and (time.time() - start_time) < timeout:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            if line == "RAW_3x4":
                reading_raw = True
                reading_upscaled = False
            elif line == "UPSCALED_10x10":
                reading_upscaled = True
                reading_raw = False
            elif line.isdigit() or (line and line[0] == '-' and line[1:].isdigit()):
                value = int(line)
                if reading_raw and len(raw_values) < 12:
                    raw_values.append(value)
                elif reading_upscaled and len(upscaled_values) < 100:
                    upscaled_values.append(value)
    
    ser.close()
    
    if len(raw_values) == 12 and len(upscaled_values) == 100:
        # Create output directory
        import os
        os.makedirs("captures", exist_ok=True)
        
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        # Save upscaled image as BMP
        img_array = np.array(upscaled_values, dtype=np.uint8).reshape(10, 10)
        img_scaled = np.repeat(np.repeat(img_array, 40, axis=0), 40, axis=1)
        img = Image.fromarray(img_scaled, mode='L')
        filename = f"captures/image_{timestamp}.bmp"
        img.save(filename)
        
        print(f"Image saved: {filename}")
        return True
    else:
        print(f"Capture failed: Raw={len(raw_values)}, Upscaled={len(upscaled_values)}")
        return False

if __name__ == "__main__":
    capture_image()
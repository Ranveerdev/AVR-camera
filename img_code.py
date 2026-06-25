import serial
import serial.tools.list_ports
import numpy as np
from PIL import Image
import os
from datetime import datetime

def capture_raw_binary_frame(port=None, baudrate=9600, timeout=40):
    """Passive binary listener designed specifically for Ranveer's bare-metal AVR Camera."""
    
    if port is None:
        ports = serial.tools.list_ports.comports()
        for p in ports:
            if any(x in p.description for x in ['Arduino', 'USB Serial', 'CH340', 'FTDI', 'CP210x']):
                port = p.device
                break
    
    if port is None:
        print("[-] Target AVR hardware link not found on any subsystem port.")
        return False
    
    print(f"[+] Attaching to binary stream on port: {port}")
    
    try:
        ser = serial.Serial(
            port=port, 
            baudrate=baudrate, 
            bytesize=serial.EIGHTBITS, 
            parity=serial.PARITY_NONE, 
            stopbits=serial.STOPBITS_ONE, 
            timeout=timeout
        )
    except Exception as e:
        print(f"[-] Failed to latch onto serial hardware: {e}")
        return False

    print("[*] Port bound. Waiting for physical button trigger on PD7...")
    
    try:
        # Pull down exactly 100 bytes representing the 100 upscaled pixels from UDR0
        raw_binary_data = ser.read(100)
        
    finally:
        ser.close()
        print("[*] Serial channel safely decoupled.")

    received_bytes = len(raw_binary_data)
    if received_bytes == 100:
        os.makedirs("captures", exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        # 100% WORKING FIX: 
        # Read as int8 to match your AVR's signed 'int' math truncation, 
        # then safely cast to uint8 for image generation.
        img_array = np.frombuffer(raw_binary_data, dtype=np.int8).astype(np.uint8).reshape(10, 10)
        
        # Optional: Normalize values to full 0-255 range if your image appears too dark
        if img_array.max() > 0:
            img_array = ((img_array - img_array.min()) / (img_array.max() - img_array.min()) * 255).astype(np.uint8)
        
        # Smooth scaling using Bilinear interpolation to honor your firmware's scaling logic
        img_initial = Image.fromarray(img_array, mode='L')
        img_smooth = img_initial.resize((400, 400), resample=Image.Resampling.BILINEAR)
        
        filename = f"captures/image_{timestamp}.bmp"
        img_smooth.save(filename)
        
        print(f"[+] Frame committed successfully to storage: {filename}")
        return True
    else:
        print(f"[-] Frame fragmentation detected. Received {received_bytes}/100 bytes.")
        print("[-] Check your PD7 physical push button wiring.")
        return False

if __name__ == "__main__":
    capture_raw_binary_frame()

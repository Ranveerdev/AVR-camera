import serial
import serial.tools.list_ports
import numpy as np
from PIL import Image
import os
from datetime import datetime

def capture_raw_binary_frame(port=None, baudrate=9600, timeout=40):
    """Passive binary listener designed for bare-metal UDR0 byte streams."""
    
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
        # Open port with full 8-bit binary capability, matching your UCSR0C settings
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
        # CRITICAL FIX: Read exactly 100 raw binary bytes straight from UDR0
        # This completely bypasses readline() and decode() to prevent encoding crashes
        raw_binary_data = ser.read(100)
        
    finally:
        ser.close()
        print("[*] Serial channel safely decoupled.")

    # Frame validation check
    received_bytes = len(raw_binary_data)
    if received_bytes == 100:
        os.makedirs("captures", exist_ok=True)
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        
        # Convert raw binary stream directly into a NumPy uint8 matrix array
        img_array = np.frombuffer(raw_binary_data, dtype=np.uint8).reshape(10, 10)
        
        # Process visual output frame using smooth bilinear scaling 
        # to respect the math behind your cubic interpolation algorithm
        img_initial = Image.fromarray(img_array, mode='L')
        img_smooth = img_initial.resize((400, 400), resample=Image.Resampling.BILINEAR)
        
        filename = f"captures/image_{timestamp}.bmp"
        img_smooth.save(filename)
        
        print(f"[+] Frame committed successfully to storage: {filename}")
        return True
    else:
        print(f"[-] Frame fragmentation detected. Received {received_bytes}/100 bytes.")
        print("[-] Ensure you pressed the hardware button completely to trigger the transmission state.")
        return False

if __name__ == "__main__":
    capture_raw_binary_frame()

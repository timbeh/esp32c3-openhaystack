#!/usr/bin/env python3
import sys
import os
import argparse
import subprocess
import base64
import binascii
import serial.tools.list_ports
import glob

def list_available_ports():
    """Return a list of available serial ports"""
    ports = list(serial.tools.list_ports.comports())
    return ports

def select_port(ports):
    """Let the user select a port from a list"""
    if not ports:
        print("No serial ports found. Please connect your ESP32C3 device.")
        sys.exit(1)
    
    if len(ports) == 1:
        selected_port = ports[0].device
        print(f"Automatically selected the only available port: {selected_port}")
        return selected_port
    
    print("Available serial ports:")
    for i, port in enumerate(ports):
        print(f"[{i}] {port.device} - {port.description}")
    
    try:
        choice = int(input("Select port number: "))
        if 0 <= choice < len(ports):
            return ports[choice].device
        else:
            print("Invalid selection. Please try again.")
            return select_port(ports)
    except ValueError:
        print("Please enter a number.")
        return select_port(ports)

def setup_directories():
    """Create input and deployed directories if they don't exist"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    input_dir = os.path.join(script_dir, 'input')
    deployed_dir = os.path.join(script_dir, 'deployed')
    
    # Create directories if they don't exist
    os.makedirs(input_dir, exist_ok=True)
    os.makedirs(deployed_dir, exist_ok=True)
    
    return input_dir, deployed_dir

def find_keyfile(input_dir):
    """Find the keyfile in the input directory"""
    keyfiles = glob.glob(os.path.join(input_dir, '*_keyfile'))
    
    if not keyfiles:
        print(f"Error: No keyfile found in the {input_dir} directory. Please place a file ending with '_keyfile' in the input directory.")
        sys.exit(1)
    
    if len(keyfiles) > 1:
        print("Multiple keyfiles found. Please ensure only one keyfile exists in the input directory.")
        for kf in keyfiles:
            print(f"- {os.path.basename(kf)}")
        sys.exit(1)
    
    return keyfiles[0]

def parse_args():
    parser = argparse.ArgumentParser(description='Write keys to ESP32C3 key partition')
    parser.add_argument('--port', '-p', help='Serial port of ESP32C3 (if not provided, will auto-detect)')
    return parser.parse_args()

def is_binary_file(file_path):
    """Check if file is binary by reading first few bytes"""
    try:
        with open(file_path, 'r', encoding='utf-8') as f:
            f.read(4096)
        return False
    except UnicodeDecodeError:
        return True

def process_binary_keyfile(file_path):
    """Process a binary keyfile directly from OpenHaystack"""
    try:
        with open(file_path, 'rb') as f:
            key_data = f.read()
        
        # OpenHaystack keys are 28 bytes each
        if len(key_data) < 28:
            print(f"Error: Key file too short ({len(key_data)} bytes), expected at least 28 bytes")
            return None
        
        # Check if it's a single key or multiple keys
        if len(key_data) == 28:
            # Single key
            return [key_data]
        else:
            # First byte might be key count, check if it makes sense
            key_count = key_data[0]
            data_size = len(key_data) - 1
            
            if key_count > 0 and data_size % 28 == 0 and data_size // 28 == key_count:
                # Format matches key count + keys
                keys = []
                for i in range(key_count):
                    start = 1 + (i * 28)
                    end = start + 28
                    keys.append(key_data[start:end])
                return keys
            else:
                # Just split the data into 28-byte chunks
                return [key_data[i:i+28] for i in range(0, len(key_data), 28) if i+28 <= len(key_data)]
    except Exception as e:
        print(f"Error processing binary key file: {e}")
        return None

def process_text_keyfile(file_path):
    """Process a text file with base64 encoded keys"""
    keys = []
    try:
        with open(file_path, 'r') as f:
            for line in f:
                line = line.strip()
                if line:
                    try:
                        key_bytes = base64.b64decode(line)
                        if len(key_bytes) == 28:
                            keys.append(key_bytes)
                        else:
                            print(f"Warning: Key has incorrect length {len(key_bytes)} bytes, expected 28 bytes")
                    except Exception as e:
                        print(f"Error decoding key {line}: {e}")
        return keys
    except Exception as e:
        print(f"Error processing text key file: {e}")
        return None

def write_key_partition(port, keys_file, deployed_dir):
    # Check if the file is binary or text
    is_binary = is_binary_file(keys_file)
    
    if is_binary:
        print(f"Detected binary key file: {keys_file}")
        keys = process_binary_keyfile(keys_file)
    else:
        print(f"Detected text key file: {keys_file}")
        keys = process_text_keyfile(keys_file)
    
    if not keys or len(keys) == 0:
        print("No valid keys found in the file. Aborting.")
        return
    
    print(f"Found {len(keys)} keys in the file")
    
    # Create a binary file with the key count and all keys
    with open('keys.bin', 'wb') as f:
        # First byte is the key count
        f.write(bytes([len(keys)]))
        
        # Write each key (28 bytes per key)
        for key_bytes in keys:
            f.write(key_bytes)
            print(f"Added key: {binascii.hexlify(key_bytes).decode()}")
    
    # Use esptool to write the keys to the key partition
    cmd = [
        "python3", "-m", "esptool",
        "--chip", "esp32c3",
        "--port", port,
        "--baud", "921600",
        "write_flash",
        "0x310000",  # Address of the key partition
        "keys.bin"
    ]
    
    print("Running command:", " ".join(cmd))
    try:
        subprocess.run(cmd, check=True)
        print("Keys have been written to the device")
        
        # Move the keyfile to deployed directory after successful deployment
        keyfile_name = os.path.basename(keys_file)
        new_keyfile_path = os.path.join(deployed_dir, keyfile_name)
        
        os.rename(keys_file, new_keyfile_path)
        print(f"Keyfile has been moved to: {new_keyfile_path}")
        
    except subprocess.CalledProcessError as e:
        print(f"Error: Failed to write keys to the device: {e}")
    except FileNotFoundError:
        print("Error: esptool module not found. Please install it with 'pip install esptool'")
    except Exception as e:
        print(f"Error moving keyfile: {e}")
    
    # Clean up
    if os.path.exists('keys.bin'):
        os.remove('keys.bin')

if __name__ == '__main__':
    args = parse_args()
    
    # Setup directories and find the keyfile
    input_dir, deployed_dir = setup_directories()
    keys_file = find_keyfile(input_dir)
    print(f"Found keyfile: {os.path.basename(keys_file)}")
    
    # If port is not specified, auto-detect
    if not args.port:
        ports = list_available_ports()
        selected_port = select_port(ports)
    else:
        selected_port = args.port
    
    write_key_partition(selected_port, keys_file, deployed_dir) 
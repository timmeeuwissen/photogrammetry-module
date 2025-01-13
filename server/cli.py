#!/usr/bin/env python3
import requests
import json
import sys
import time
from typing import Optional, Dict, Any

class PhotogrammetryCLI:
    def __init__(self):
        self.base_url = "http://localhost:8888/api"
        self.menu_options = {
            "1": ("Check Status", self.check_status),
            "2": ("Start Scan", self.start_scan),
            "3": ("Abort Scan", self.abort_scan),
            "4": ("Monitor Progress", self.monitor_progress),
            "5": ("Update LCD Display", self.update_lcd),
            "6": ("Take Single Photo", self.capture_single),
            "7": ("Control Motor", self.control_motor),
            "q": ("Quit", self.quit_program)
        }

    def make_request(self, method: str, endpoint: str, data: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
        url = f"{self.base_url}/{endpoint}"
        try:
            if method == "GET":
                response = requests.get(url)
            elif method == "POST":
                response = requests.post(url, json=data)
            else:
                raise ValueError(f"Unsupported HTTP method: {method}")

            response.raise_for_status()
            return response.json()
        except requests.exceptions.ConnectionError:
            print("Error: Could not connect to server. Is it running?")
            return {"error": "Connection failed"}
        except requests.exceptions.HTTPError as e:
            print(f"Error: Server {url} returned {e.response.status_code}")
            try:
                error_data = e.response.json()
                return {"error": error_data.get("error", str(e))}
            except ValueError:  # Includes JSONDecodeError
                return {"error": e.response.text if e.response.text else str(e)}
        except Exception as e:
            print(f"Error: {str(e)}")
            return {"error": str(e)}

    def display_menu(self):
        print("\nPhotogrammetry Control Menu")
        print("=" * 25)
        for key, (name, _) in self.menu_options.items():
            print(f"{key}. {name}")
        print("=" * 25)

    def check_status(self):
        result = self.make_request("GET", "status")
        if "error" not in result:
            print("\nSystem Status:")
            print(f"Camera: {result['camera']}")
            print(f"Controller: {result['controller']}")
            print(f"Scan Status: {result['scan_status']}")
        return result

    def start_scan(self):
        status = self.check_status()
        if "error" in status:
            return

        if status["scan_status"] == "scanning":
            print("Error: Scan already in progress")
            return
        
        if status["camera"] != "connected" or status["controller"] != "connected":
            print("Error: Not all devices are connected")
            return

        confirm = input("\nReady to start scan. Proceed? [y/N] ").lower()
        if confirm != 'y':
            print("Scan cancelled")
            return

        result = self.make_request("POST", "start")
        if "error" not in result:
            print("Scan started successfully")
            self.monitor_progress()

    def abort_scan(self):
        status = self.check_status()
        if "error" in status:
            return

        if status["scan_status"] != "scanning":
            print("No scan in progress")
            return

        confirm = input("\nAre you sure you want to abort the scan? [y/N] ").lower()
        if confirm != 'y':
            print("Abort cancelled")
            return

        result = self.make_request("POST", "abort")
        if "error" not in result:
            print("Scan aborted successfully")
            if "errors" in result:
                print("Warnings:")
                for error in result["errors"]:
                    print(f"- {error}")

    def monitor_progress(self):
        try:
            print("\nMonitoring scan progress (Ctrl+C to stop monitoring)")
            print("=" * 50)
            while True:
                status = self.check_status()
                if "error" in status:
                    break
                
                if status["scan_status"] != "scanning":
                    print("\nScan completed or stopped")
                    break
                
                sys.stdout.write("\033[K")  # Clear line
                print(f"\rStatus: {status['scan_status']}", end="", flush=True)
                time.sleep(1)
        except KeyboardInterrupt:
            print("\nStopped monitoring")

    def update_lcd(self):
        print("\nUpdate LCD Display")
        print("=" * 25)
        print("Enter text for display (press Enter without text to skip line 2):")
        
        # Get line 1 with validation
        line1 = input("Line 1: ").strip()
        if not line1:
            print("Error: Line 1 cannot be empty")
            return
        if len(line1) > 16:  # LCD is 16 characters wide
            print("Warning: Line 1 will be truncated (max 16 characters)")
            line1 = line1[:16]
            
        # Get optional line 2
        line2 = input("Line 2 (optional): ").strip()
        if line2 and len(line2) > 16:
            print("Warning: Line 2 will be truncated (max 16 characters)")
            line2 = line2[:16]
        
        # Prepare lines array
        lines = [line1]
        if line2:
            lines.append(line2)
        
        # Check controller status before attempting update
        status = self.check_status()
        if "error" in status:
            print("Error: Could not verify controller status")
            return
        if status["controller"] != "connected":
            print("Error: Controller is not connected")
            return
            
        # Attempt to update LCD
        result = self.make_request("POST", "lcd", {"lines": lines})
        if "error" in result:
            error_msg = result.get("error", "Unknown error")
            if "Connection failed" in error_msg:
                print("Error: Could not connect to server. Is it running?")
            elif "401" in error_msg:
                print("Error: Not authorized to update LCD")
            elif "400" in error_msg:
                print("Error: Invalid request format")
            else:
                print(f"Error updating LCD: {error_msg}")
        else:
            print("LCD updated successfully")
            print(f"Displayed text:")
            print(f"Line 1: {line1}")
            if line2:
                print(f"Line 2: {line2}")

    def capture_single(self):
        # Check camera status first
        status = self.check_status()
        if "error" in status:
            return
        if status["camera"] != "connected":
            print("Error: Camera not connected")
            return

        # Confirm action
        confirm = input("\nReady to take a photo. Proceed? [y/N] ").lower()
        if confirm != 'y':
            print("Photo cancelled")
            return

        # Take photo
        result = self.make_request("POST", "capture_single")
        if "error" in result:
            print(f"Error taking photo: {result['error']}")
        else:
            print("Photo captured successfully")

    def control_motor(self):
        print("\nMotor Control")
        print("=" * 25)
        print("Enter angle (0-359) or +/- for relative movement")
        
        # Check controller status first
        status = self.check_status()
        if "error" in status:
            return
        if status["controller"] != "connected":
            print("Error: Controller not connected")
            return
            
        # Get angle input
        angle_input = input("Angle: ").strip()
        if not angle_input:
            print("Operation cancelled")
            return
            
        # Parse angle input
        try:
            if angle_input.startswith(('+', '-')):
                angle = int(angle_input)
                is_relative = True
            else:
                angle = int(angle_input)
                is_relative = False
                
            # Validate angle
            if (not is_relative and (angle < 0 or angle >= 360)) or \
               (is_relative and (angle < -360 or angle > 360)):
                print("Error: Invalid angle. Must be 0-359 for absolute or -360 to +360 for relative")
                return
                
            # Confirm action
            direction = "relative" if is_relative else "absolute"
            confirm = input(f"\nMove motor to {angle}° ({direction}). Proceed? [y/N] ").lower()
            if confirm != 'y':
                print("Operation cancelled")
                return
                
            # Send motor control command
            result = self.make_request("POST", "motor", {
                "angle": angle,
                "relative": is_relative
            })
            
            if "error" in result:
                print(f"Error controlling motor: {result['error']}")
            else:
                print(f"Motor moved to {result.get('angle', angle)}°")
                
        except ValueError:
            print("Error: Invalid angle format")
            return

    def quit_program(self):
        print("\nGoodbye!")
        sys.exit(0)

    def run(self):
        while True:
            self.display_menu()
            choice = input("\nEnter your choice: ").lower()
            
            if choice in self.menu_options:
                _, func = self.menu_options[choice]
                func()
            else:
                print("Invalid choice")

if __name__ == "__main__":
    cli = PhotogrammetryCLI()
    cli.run()

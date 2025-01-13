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
            print(f"Error: Server returned {e.response.status_code}")
            return {"error": e.response.json().get("error", str(e))}
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

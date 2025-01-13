import os
from typing import Optional
import requests
from board_manager import BoardManager

class ScanManager:
    def __init__(self, board_manager: BoardManager):
        self.board_manager = board_manager
        self.UPLOAD_FOLDER = './uploads'
        self.PHOTOGRAMMETRY_OUTPUT = './output'
        self.SCAN_STATUS_FILE = '.scan_status'
        
        # Ensure folders exist
        os.makedirs(self.UPLOAD_FOLDER, exist_ok=True)
        os.makedirs(self.PHOTOGRAMMETRY_OUTPUT, exist_ok=True)

    def get_status(self) -> str:
        if not os.path.exists(self.SCAN_STATUS_FILE):
            return "idle"
        with open(self.SCAN_STATUS_FILE, 'r') as f:
            return f.read().strip()

    def set_status(self, status: str) -> None:
        with open(self.SCAN_STATUS_FILE, 'w') as f:
            f.write(status)

    def start_scan(self) -> tuple[bool, Optional[str]]:
        if self.get_status() == "scanning":
            return False, "Scan already in progress"

        if not self.board_manager.camera_board or not self.board_manager.controller_board:
            return False, "Not all boards connected"

        if not (self.board_manager.camera_board.is_alive() and 
                self.board_manager.controller_board.is_alive()):
            return False, "One or more boards not responding"

        self.set_status("scanning")
        self.board_manager.update_lcd("Scan Starting...")
        
        # Start the scanning process
        try:
            response = requests.post(
                f"http://{self.board_manager.controller_board.ip_address}/start_rotation",
                headers={"Authorization": f"Bearer {self.board_manager.controller_board.token}"},
                timeout=5
            )
            if response.status_code != 200:
                self.set_status("idle")
                self.board_manager.update_lcd("Start Failed")
                return False, "Failed to start controller"
        except Exception as e:
            self.set_status("idle")
            self.board_manager.update_lcd("Start Failed")
            return False, f"Controller error: {str(e)}"

        return True, None

    def handle_capture_complete(self, step: int) -> None:
        self.board_manager.update_lcd(f"Photo {step} OK", 1)

    def handle_rotation_complete(self, step: int) -> tuple[bool, Optional[str]]:
        try:
            response = requests.post(
                f"http://{self.board_manager.camera_board.ip_address}/capture",
                json={"step": step},
                headers={"Authorization": f"Bearer {self.board_manager.camera_board.token}"},
                timeout=5
            )
            if response.status_code != 200:
                self.board_manager.update_lcd(f"Capture Failed", 1)
                return False, "Failed to trigger capture"
        except Exception as e:
            self.board_manager.update_lcd(f"Capture Failed", 1)
            return False, f"Camera error: {str(e)}"

        return True, None

    def handle_scan_complete(self) -> None:
        self.set_status("idle")
        self.board_manager.update_lcd("Scan Complete")
        
        # Start photogrammetry processing
        print("Starting photogrammetry processing...")
        os.system(f"photogrammetry-tool --input {self.UPLOAD_FOLDER} --output {self.PHOTOGRAMMETRY_OUTPUT}")
        
        self.board_manager.update_lcd("Process Done", 1)

    def abort_scan(self) -> tuple[bool, list[str]]:
        if self.get_status() != "scanning":
            return False, ["No scan in progress"]

        result = self.board_manager.send_abort()
        self.set_status("idle")
        self.board_manager.update_lcd("Scan Aborted")

        return True, result.get("errors", [])

    def save_photo(self, filename: str, file_data) -> None:
        save_path = os.path.join(self.UPLOAD_FOLDER, filename)
        file_data.save(save_path)
        print(f"Image {filename} uploaded successfully.")

import secrets
from typing import Optional, Dict
import requests
from models import Board

class BoardManager:
    def __init__(self):
        self.camera_board: Optional[Board] = None
        self.controller_board: Optional[Board] = None

    def generate_token(self) -> str:
        return secrets.token_urlsafe(32)

    def register_board(self, board_type: str, ip_address: str) -> Board:
        token = self.generate_token()
        new_board = Board(ip_address=ip_address, token=token, last_seen=0)
        
        if board_type == "camera":
            self.camera_board = new_board
        elif board_type == "controller":
            self.controller_board = new_board
        else:
            raise ValueError(f"Invalid board type: {board_type}")
        
        return new_board

    def update_heartbeat(self, token: str) -> bool:
        import time
        current_time = time.time()

        if self.camera_board and token == self.camera_board.token:
            self.camera_board.last_seen = current_time
            return True
        elif self.controller_board and token == self.controller_board.token:
            self.controller_board.last_seen = current_time
            return True
        
        return False

    def get_board_by_token(self, token: str) -> Optional[Board]:
        if self.camera_board and token == self.camera_board.token:
            return self.camera_board
        elif self.controller_board and token == self.controller_board.token:
            return self.controller_board
        return None

    def update_lcd(self, message: str, line: int = 0) -> bool:
        if not self.controller_board or not self.controller_board.is_alive():
            print(f"Controller not connected, can't update LCD: {message}")
            return False
        
        try:
            response = requests.post(
                f"http://{self.controller_board.ip_address}/lcd",
                json={"message": message, "line": line},
                headers={"Authorization": f"Bearer {self.controller_board.token}"},
                timeout=5
            )
            return response.status_code == 200
        except Exception as e:
            print(f"Failed to update LCD: {e}")
            return False

    def get_status(self) -> Dict[str, str]:
        return {
            "camera": "connected" if self.camera_board and self.camera_board.is_alive() else "disconnected",
            "controller": "connected" if self.controller_board and self.controller_board.is_alive() else "disconnected"
        }

    def send_abort(self) -> Dict[str, list]:
        errors = []
        
        if self.controller_board and self.controller_board.is_alive():
            try:
                requests.post(
                    f"http://{self.controller_board.ip_address}/abort",
                    headers={"Authorization": f"Bearer {self.controller_board.token}"},
                    timeout=5
                )
            except Exception as e:
                errors.append(f"Controller abort failed: {str(e)}")

        if self.camera_board and self.camera_board.is_alive():
            try:
                requests.post(
                    f"http://{self.camera_board.ip_address}/abort",
                    headers={"Authorization": f"Bearer {self.camera_board.token}"},
                    timeout=5
                )
            except Exception as e:
                errors.append(f"Camera abort failed: {str(e)}")

        return {"errors": errors}

from flask import Flask, request, jsonify, Response
import os
import requests
import secrets
import time
from dataclasses import dataclass
from typing import Optional, Dict

app = Flask(__name__)

UPLOAD_FOLDER = './uploads'
PHOTOGRAMMETRY_OUTPUT = './output'
SCAN_STATUS_FILE = '.scan_status'

# Ensure folders exist
os.makedirs(UPLOAD_FOLDER, exist_ok=True)
os.makedirs(PHOTOGRAMMETRY_OUTPUT, exist_ok=True)

@dataclass
class Board:
    ip_address: str
    token: str
    last_seen: float
    status: str = "idle"

# Store connected boards
camera_board: Optional[Board] = None
controller_board: Optional[Board] = None

# Generate secure tokens for boards
def generate_token() -> str:
    return secrets.token_urlsafe(32)

def is_board_alive(board: Board) -> bool:
    return time.time() - board.last_seen < 30  # Consider board dead after 30s

def update_lcd(message: str, line: int = 0) -> bool:
    if not controller_board or not is_board_alive(controller_board):
        print(f"Controller not connected, can't update LCD: {message}")
        return False
    
    try:
        response = requests.post(
            f"http://{controller_board.ip_address}/lcd",
            json={"message": message, "line": line},
            headers={"Authorization": f"Bearer {controller_board.token}"},
            timeout=5
        )
        return response.status_code == 200
    except Exception as e:
        print(f"Failed to update LCD: {e}")
        return False

def get_scan_status():
    if not os.path.exists(SCAN_STATUS_FILE):
        return "idle"
    with open(SCAN_STATUS_FILE, 'r') as f:
        return f.read().strip()

def set_scan_status(status):
    with open(SCAN_STATUS_FILE, 'w') as f:
        f.write(status)

@app.route('/api/register', methods=['POST'])
def register_board():
    data = request.get_json()
    if not data or 'type' not in data or 'ip' not in data:
        return jsonify({"error": "Missing board type or IP"}), 400

    global camera_board, controller_board
    board_type = data['type']
    ip_address = data['ip']
    token = generate_token()
    new_board = Board(ip_address=ip_address, token=token, last_seen=time.time())

    if board_type == 'camera':
        camera_board = new_board
        print(f"Camera board registered at {ip_address}")
    elif board_type == 'controller':
        controller_board = new_board
        print(f"Controller board registered at {ip_address}")
        update_lcd("System Ready")
    else:
        return jsonify({"error": "Invalid board type"}), 400

    return jsonify({
        "token": token,
        "message": f"{board_type} registered successfully"
    })

@app.route('/api/heartbeat', methods=['POST'])
def heartbeat():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not token:
        return jsonify({"error": "No token provided"}), 401

    global camera_board, controller_board
    current_time = time.time()

    if camera_board and token == camera_board.token:
        camera_board.last_seen = current_time
        return jsonify({"status": "ok"})
    elif controller_board and token == controller_board.token:
        controller_board.last_seen = current_time
        return jsonify({"status": "ok"})
    
    return jsonify({"error": "Invalid token"}), 401

@app.route('/api/status', methods=['GET'])
def check_status():
    camera_status = "connected" if camera_board and is_board_alive(camera_board) else "disconnected"
    controller_status = "connected" if controller_board and is_board_alive(controller_board) else "disconnected"
    scan_status = get_scan_status()

    return jsonify({
        "scan_status": scan_status,
        "camera": camera_status,
        "controller": controller_status
    })

@app.route('/api/start', methods=['POST'])
def start_scan():
    if get_scan_status() == "scanning":
        return jsonify({"error": "Scan already in progress"}), 409

    if not camera_board or not controller_board:
        return jsonify({"error": "Not all boards connected"}), 503

    if not is_board_alive(camera_board) or not is_board_alive(controller_board):
        return jsonify({"error": "One or more boards not responding"}), 503

    set_scan_status("scanning")
    update_lcd("Scan Starting...")
    
    # Start the scanning process
    try:
        response = requests.post(
            f"http://{controller_board.ip_address}/start_rotation",
            headers={"Authorization": f"Bearer {controller_board.token}"},
            timeout=5
        )
        if response.status_code != 200:
            set_scan_status("idle")
            update_lcd("Start Failed")
            return jsonify({"error": "Failed to start controller"}), 500
    except Exception as e:
        set_scan_status("idle")
        update_lcd("Start Failed")
        return jsonify({"error": f"Controller error: {str(e)}"}), 500

    return jsonify({"message": "Scan started"})

@app.route('/api/capture_complete', methods=['POST'])
def handle_capture_complete():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not camera_board or token != camera_board.token:
        return jsonify({"error": "Unauthorized"}), 401

    data = request.get_json()
    step = data.get('step', 0)
    update_lcd(f"Photo {step} OK", 1)
    
    return jsonify({"status": "ok"})

@app.route('/api/rotation_complete', methods=['POST'])
def handle_rotation_complete():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not controller_board or token != controller_board.token:
        return jsonify({"error": "Unauthorized"}), 401

    data = request.get_json()
    step = data.get('step', 0)
    
    try:
        response = requests.post(
            f"http://{camera_board.ip_address}/capture",
            json={"step": step},
            headers={"Authorization": f"Bearer {camera_board.token}"},
            timeout=5
        )
        if response.status_code != 200:
            update_lcd(f"Capture Failed", 1)
            return jsonify({"error": "Failed to trigger capture"}), 500
    except Exception as e:
        update_lcd(f"Capture Failed", 1)
        return jsonify({"error": f"Camera error: {str(e)}"}), 500

    return jsonify({"status": "ok"})

@app.route('/api/scan_complete', methods=['POST'])
def handle_scan_complete():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not controller_board or token != controller_board.token:
        return jsonify({"error": "Unauthorized"}), 401

    set_scan_status("idle")
    update_lcd("Scan Complete")
    
    # Start photogrammetry processing
    print("Starting photogrammetry processing...")
    os.system(f"photogrammetry-tool --input {UPLOAD_FOLDER} --output {PHOTOGRAMMETRY_OUTPUT}")
    
    update_lcd("Process Done", 1)
    return jsonify({"status": "ok"})

@app.route('/api/abort', methods=['POST'])
def abort_scan():
    if get_scan_status() != "scanning":
        return jsonify({"error": "No scan in progress"}), 409

    # Send abort command to both boards
    errors = []
    
    if controller_board and is_board_alive(controller_board):
        try:
            requests.post(
                f"http://{controller_board.ip_address}/abort",
                headers={"Authorization": f"Bearer {controller_board.token}"},
                timeout=5
            )
        except Exception as e:
            errors.append(f"Controller abort failed: {str(e)}")

    if camera_board and is_board_alive(camera_board):
        try:
            requests.post(
                f"http://{camera_board.ip_address}/abort",
                headers={"Authorization": f"Bearer {camera_board.token}"},
                timeout=5
            )
        except Exception as e:
            errors.append(f"Camera abort failed: {str(e)}")

    set_scan_status("idle")
    update_lcd("Scan Aborted")

    if errors:
        return jsonify({
            "message": "Scan aborted with errors",
            "errors": errors
        }), 500

    return jsonify({"message": "Scan aborted successfully"})

@app.route('/api/upload', methods=['POST'])
def upload_image():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not camera_board or token != camera_board.token:
        return jsonify({"error": "Unauthorized"}), 401

    if 'image' not in request.files:
        return jsonify({"error": "No image file provided"}), 400

    image = request.files['image']
    filename = image.filename
    save_path = os.path.join(UPLOAD_FOLDER, filename)
    image.save(save_path)

    print(f"Image {filename} uploaded successfully.")
    return jsonify({"message": f"Image {filename} uploaded successfully"}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8888)

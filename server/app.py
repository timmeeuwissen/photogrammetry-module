from flask import Flask, request, jsonify
from board_manager import BoardManager
from scan_manager import ScanManager

app = Flask(__name__)
board_manager = BoardManager()
scan_manager = ScanManager(board_manager)

@app.route('/api/register', methods=['POST'])
def register_board():
    data = request.get_json()
    if not data or 'type' not in data or 'ip' not in data:
        return jsonify({"error": "Missing board type or IP"}), 400

    try:
        board = board_manager.register_board(data['type'], data['ip'])
        if data['type'] == 'controller':
            board_manager.update_lcd("System Ready")
        return jsonify({
            "token": board.token,
            "message": f"{data['type']} registered successfully"
        })
    except ValueError as e:
        return jsonify({"error": str(e)}), 400

@app.route('/api/heartbeat', methods=['POST'])
def heartbeat():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not token:
        return jsonify({"error": "No token provided"}), 401

    if board_manager.update_heartbeat(token):
        return jsonify({"status": "ok"})
    
    return jsonify({"error": "Invalid token"}), 401

@app.route('/api/status', methods=['GET'])
def check_status():
    board_status = board_manager.get_status()
    board_status["scan_status"] = scan_manager.get_status()
    return jsonify(board_status)

@app.route('/api/start', methods=['POST'])
def start_scan():
    success, error = scan_manager.start_scan()
    if not success:
        return jsonify({"error": error}), 409 if error == "Scan already in progress" else 503
    return jsonify({"message": "Scan started"})

@app.route('/api/capture_complete', methods=['POST'])
def handle_capture_complete():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not board_manager.camera_board or token != board_manager.camera_board.token:
        return jsonify({"error": "Unauthorized"}), 401

    data = request.get_json()
    step = data.get('step', 0)
    scan_manager.handle_capture_complete(step)
    return jsonify({"status": "ok"})

@app.route('/api/rotation_complete', methods=['POST'])
def handle_rotation_complete():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not board_manager.controller_board or token != board_manager.controller_board.token:
        return jsonify({"error": "Unauthorized"}), 401

    data = request.get_json()
    step = data.get('step', 0)
    
    success, error = scan_manager.handle_rotation_complete(step)
    if not success:
        return jsonify({"error": error}), 500

    return jsonify({"status": "ok"})

@app.route('/api/scan_complete', methods=['POST'])
def handle_scan_complete():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not board_manager.controller_board or token != board_manager.controller_board.token:
        return jsonify({"error": "Unauthorized"}), 401

    scan_manager.handle_scan_complete()
    return jsonify({"status": "ok"})

@app.route('/api/abort', methods=['POST'])
def abort_scan():
    success, errors = scan_manager.abort_scan()
    if not success:
        return jsonify({"error": errors[0]}), 409

    if errors:
        return jsonify({
            "message": "Scan aborted with errors",
            "errors": errors
        }), 500

    return jsonify({"message": "Scan aborted successfully"})

@app.route('/lcd', methods=['POST'])
@app.route('/api/lcd', methods=['POST'])
def handle_lcd_update():
    data = request.get_json()
    if not data or 'lines' not in data:
        return jsonify({"error": "Missing lines array"}), 400
        
    lines = data['lines']
    if not isinstance(lines, list) or len(lines) == 0 or len(lines) > 2:
        return jsonify({"error": "Expected array of 1 or 2 lines"}), 400
        
    if not all(isinstance(line, str) for line in lines):
        return jsonify({"error": "All lines must be strings"}), 400
        
    # Truncate lines to 16 characters if needed
    lines = [line[:16] for line in lines]
    
    # Update LCD using board manager
    if board_manager.update_lcd(lines[0], lines[1] if len(lines) > 1 else None):
        return jsonify({"message": "LCD updated successfully"})
    else:
        return jsonify({"error": "Failed to update LCD"}), 500

@app.route('/api/capture_single', methods=['POST'])
def capture_single():
    # Check if camera is connected
    if not board_manager.camera_board or not board_manager.camera_board.is_alive():
        return jsonify({"error": "Camera not connected"}), 503

    try:
        # Request capture from camera
        response = requests.post(
            f"http://{board_manager.camera_board.ip_address}/capture",
            json={"step": 0},  # Use step 0 for single shots
            headers={"Authorization": f"Bearer {board_manager.camera_board.token}"},
            timeout=5
        )
        if response.status_code != 200:
            return jsonify({"error": "Failed to capture photo"}), 500
            
        return jsonify({"message": "Photo captured successfully"})
    except Exception as e:
        return jsonify({"error": f"Camera error: {str(e)}"}), 500

@app.route('/api/upload', methods=['POST'])
def upload_image():
    token = request.headers.get('Authorization', '').replace('Bearer ', '')
    if not board_manager.camera_board or token != board_manager.camera_board.token:
        return jsonify({"error": "Unauthorized"}), 401

    if 'image' not in request.files:
        return jsonify({"error": "No image file provided"}), 400

    image = request.files['image']
    scan_manager.save_photo(image.filename, image)
    return jsonify({"message": f"Image {image.filename} uploaded successfully"}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8888)

from flask import Flask, request, jsonify, Response
import os

app = Flask(__name__)

UPLOAD_FOLDER = './uploads'
PHOTOGRAMMETRY_OUTPUT = './output'
SCAN_STATUS_FILE = '.scan_status'

# Ensure folders exist
os.makedirs(UPLOAD_FOLDER, exist_ok=True)
os.makedirs(PHOTOGRAMMETRY_OUTPUT, exist_ok=True)

def get_scan_status():
    if not os.path.exists(SCAN_STATUS_FILE):
        return "idle"
    with open(SCAN_STATUS_FILE, 'r') as f:
        return f.read().strip()

def set_scan_status(status):
    with open(SCAN_STATUS_FILE, 'w') as f:
        f.write(status)

@app.route('/api/upload', methods=['POST'])
def upload_image():
    if 'image' not in request.files:
        return jsonify({"error": "No image file provided"}), 400

    image = request.files['image']
    filename = image.filename
    save_path = os.path.join(UPLOAD_FOLDER, filename)
    image.save(save_path)

    print(f"Image {filename} uploaded successfully.")
    return jsonify({"message": f"Image {filename} uploaded successfully"}), 200

@app.route('/api/start', methods=['POST'])
def start_scan():
    if get_scan_status() == "scanning":
        return jsonify({"error": "Scan already in progress"}), 409
    
    set_scan_status("scanning")
    return jsonify({"message": "Scan started"}), 200

@app.route('/api/status', methods=['GET'])
def check_status():
    return jsonify({"status": get_scan_status()}), 200

@app.route('/api/upload/complete', methods=['GET'])
def process_photogrammetry():
    print("Photogrammetry process started.")
    # Call photogrammetry tool (replace with your tool's command)
    os.system(f"photogrammetry-tool --input {UPLOAD_FOLDER} --output {PHOTOGRAMMETRY_OUTPUT}")

    set_scan_status("idle")
    return jsonify({"message": "Photogrammetry process completed"}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8888)

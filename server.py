from flask import Flask, request, jsonify
import os

app = Flask(__name__)

UPLOAD_FOLDER = './uploads'
PHOTOGRAMMETRY_OUTPUT = './output'

# Ensure upload folder exists
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

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

@app.route('/api/upload/complete', methods=['GET'])
def process_photogrammetry():
    print("Photogrammetry process started.")
    # Call photogrammetry tool (replace with your tool's command)
    os.system(f"photogrammetry-tool --input {UPLOAD_FOLDER} --output {PHOTOGRAMMETRY_OUTPUT}")

    return jsonify({"message": "Photogrammetry process completed"}), 200

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8888)

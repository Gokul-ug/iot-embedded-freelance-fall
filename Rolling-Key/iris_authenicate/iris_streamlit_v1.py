from flask import Flask, request, jsonify, render_template_string
import cv2
import numpy as np
from tensorflow.keras.models import load_model

app = Flask(__name__)

# ===== Load trained Iris ML model =====
model = load_model("iris_classifier_model.h5")
IMG_SIZE = (128, 128)

# Demo page for access granted
DEMO_PAGE = """
<!DOCTYPE html>
<html>
<head><title>File Folder Demo</title>
<style>
body{font-family:Arial,sans-serif;text-align:center;padding:30px;background:#f0f2f5;}
h2{color:green;}
ul{list-style:none;padding:0;}
li{margin:5px 0;padding:5px;background:#fff;border-radius:5px;display:inline-block;}
</style>
</head>
<body>
<h2>✅ Access Granted!</h2>
<p>Here is your demo file/folder content:</p>
<ul>
<li>file1.txt</li>
<li>file2.pdf</li>
<li>image1.bmp</li>
</ul>
</body>
</html>
"""

# Iris upload page for ESP8266
UPLOAD_PAGE = """
<!DOCTYPE html>
<html>
<head><title>Iris Verification</title>
<style>
body{font-family:Arial,sans-serif;text-align:center;padding:30px;background:linear-gradient(135deg,#1e3c72,#2a5298);color:white;}
.container{background:#fff;color:#333;width:400px;padding:30px;border-radius:15px;margin:auto;margin-top:50px;}
button{padding:10px 20px;background:#2a5298;color:white;border:none;border-radius:8px;cursor:pointer;}
button:hover{background:#1e3c72;}
input{padding:10px;width:80%;margin:10px 0;border-radius:8px;border:1px solid #aaa;}
#status{margin-top:20px;font-weight:bold;}
</style>
</head>
<body>
<div class="container">
<h2>Step 3: Upload Iris Image</h2>
<form id="irisForm" enctype="multipart/form-data">
<input type="file" name="iris" accept="image/*" required><br>
<button type="submit">Verify Iris</button>
</form>
<div id="status"></div>

<script>
const form = document.getElementById('irisForm');
form.addEventListener('submit', function(e){
    e.preventDefault();
    const statusDiv = document.getElementById('status');
    statusDiv.innerHTML="⏳ Verifying Iris...";
    const fileInput = form.querySelector('input[name=iris]');
    const file = fileInput.files[0];
    const formData = new FormData();
    formData.append('iris', file);

    fetch('/verify_iris',{method:'POST', body: formData})
    .then(resp => resp.json())
    .then(data=>{
        if(data.status==1){
            statusDiv.innerHTML="<h3 style='color:green'>✅ Access Granted</h3>";
            setTimeout(()=>{ window.location.href='/demo'; },1000);
        } else {
            statusDiv.innerHTML="<h3 style='color:red'>❌ Access Denied</h3>";
        }
    }).catch(err=>statusDiv.innerHTML="❌ Error: "+err);
});
</script>
</div>
</body>
</html>
"""

@app.route("/")
def home():
    return UPLOAD_PAGE

@app.route("/verify_iris", methods=["POST"])
def verify_iris():
    try:
        if "iris" not in request.files:
            return jsonify({"status": 0})
        file = request.files["iris"]
        img_bytes = file.read()

        # Decode image
        nparr = np.frombuffer(img_bytes, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_GRAYSCALE)
        if img is None: return jsonify({"status": 0})

        # Preprocess
        img = cv2.resize(img, IMG_SIZE)
        img = img.astype("float32") / 255.0
        img = np.expand_dims(img, axis=(0,-1))

        # Predict
        pred = model.predict(img)
        iris_verified = int(pred[0][0] > 0.5)
        print("Iris verification result:", iris_verified)
        return jsonify({"status": iris_verified})
    except Exception as e:
        print("Error:", e)
        return jsonify({"status":0})

@app.route("/demo")
def demo_page():
    return DEMO_PAGE

if __name__=="__main__":
    app.run(host="0.0.0.0", port=8501)

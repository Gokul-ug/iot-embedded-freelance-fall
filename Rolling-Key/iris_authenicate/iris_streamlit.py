# =========================================
# Iris Authentication - Streamlit App
# =========================================

import streamlit as st
import tensorflow as tf
import numpy as np
import cv2
import os

# === Load Model ===
model_path = r"C:\Users\user\Desktop\iris_authenicate\iris_authenicate\iris_classifier_model.h5"
model = tf.keras.models.load_model(model_path, compile=False)  # ← compile=False

# === Configuration ===
img_size = 128
categories = ['Access Granted', 'Access Denied']
access_labels = {'Access Granted': "✅ Access Granted", 
                 'Access Denied': "❌ Access Denied"}
access_binary = {'Access Granted': 1, 'Access Denied': 0}

# === Streamlit UI ===
st.set_page_config(page_title="Iris Authentication", layout="centered")
st.title("🧿 Iris Authentication")

uploaded_file = st.file_uploader("📁 Upload an Iris Image", type=['png', 'jpg', 'jpeg', 'bmp'])

if uploaded_file is not None:
    # Convert uploaded file to numpy array
    file_bytes = np.frombuffer(uploaded_file.read(), np.uint8)
    img = cv2.imdecode(file_bytes, cv2.IMREAD_GRAYSCALE)
    
    if img is None:
        st.error("❌ Failed to read image")
    else:
        img_resized = cv2.resize(img, (img_size, img_size))
        img_array = img_resized.reshape(1, img_size, img_size, 1) / 255.0

        # Predict
        pred = model.predict(img_array)
        class_index = np.argmax(pred)
        class_name = categories[class_index]
        st.success(access_labels[class_name])

        # Print binary for IoT
        print(f"IoT Output: {access_binary[class_name]}")

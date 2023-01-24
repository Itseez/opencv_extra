# Use this script to generate test data for dnn module and TFLite models
import os
import numpy as np
import tensorflow as tf

import cv2 as cv

testdata = os.environ['OPENCV_TEST_DATA_PATH']

def run_model(model_name, inp_size):
    interpreter = tf.lite.Interpreter(model_name + ".tflite",
                                      experimental_preserve_all_tensors=True)
    interpreter.allocate_tensors()

    # Get input and output tensors.
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()

    # Run model
    image = cv.imread(os.path.join(testdata, "cv", "shared", "lena.png"))
    inp = cv.resize(image, inp_size)
    inp = cv.cvtColor(inp, cv.COLOR_BGR2RGB)
    inp = np.expand_dims(inp, 0)
    inp = inp.astype(np.float32) / 255  # NHWC

    interpreter.set_tensor(input_details[0]['index'], inp)

    interpreter.invoke()

    for details in output_details:
        out = interpreter.get_tensor(details['index'])  # Or use an intermediate layer index
        out_name = details['name']
        np.save(f"{model_name}_out_{out_name}.npy", out)

run_model("face_landmark", (192, 192))
run_model("face_detection_short_range", (128, 128))

import mediapipe as mp

with mp.solutions.selfie_segmentation.SelfieSegmentation(model_selection=0) as selfie_segmentation:
    image = cv.imread(os.path.join(testdata, "cv", "shared", "lena.png"))
    inp = cv.resize(image, (256, 256))
    inp = cv.cvtColor(inp, cv.COLOR_BGR2RGB)
    results = selfie_segmentation.process(inp)
    np.save(f"selfie_segmentation_out_activation_10.npy", results.segmentation_mask)

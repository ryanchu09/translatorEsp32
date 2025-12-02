import tensorflow as tf

model = tf.lite.Interpreter(model_path="trainingVAD/model_int8.tflite")
model.allocate_tensors()
print("output details:", model.get_output_details())
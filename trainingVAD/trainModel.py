import os
import wave
base_dir = "trainingVAD/data"
import numpy as np
import tensorflow as tf
from sklearn.model_selection import train_test_split

frame_length_ms = 30
frame_hop_ms = 15
sample_rate = 16000
frame_length = int(sample_rate * frame_length_ms /1000)
frame_hop = int(sample_rate * frame_hop_ms / 1000)
N_MELS = 16

def load_wav_as_float(path):

    with wave.open(path, "rb") as wav:
        assert wav.getframerate() == sample_rate
        assert wav.getnchannels() == 1
        data = wav.readframes(wav.getnframes())
        pcm = np.frombuffer(data, dtype=np.int16)
        pcm = pcm.astype(np.float32) / 32768.0
    return pcm

def log_mel_frames(audio):
    audio_tensor = tf.convert_to_tensor(audio, dtype=tf.float32)
    stfts = tf.signal.stft(
        audio_tensor,
        frame_length=frame_length,
        frame_step =frame_hop,
        fft_length =frame_length,
        window_fn=tf.signal.hann_window
    )
    magnitude =tf.abs(stfts)
    mel_matrix = tf.signal.linear_to_mel_weight_matrix(
        num_mel_bins =N_MELS,
        num_spectrogram_bins=frame_length//2 + 1,
        sample_rate=sample_rate
    )
    mel = tf.tensordot(magnitude, mel_matrix, axes=1)
    log_mel = tf.math.log(mel + 1e-6)
    return log_mel.numpy()


AUDIO_FOLDERS = {
    "speech" : 1,
    "bg" : 0
}
all_labels = []
all_frames =  []
X=[] #features
y=[] #labels

for folder, label in AUDIO_FOLDERS.items():
    folder_path = os.path.join(base_dir, folder)
    print(folder_path)
    for entry in os.listdir(folder_path):

        wav_path = os.path.join(folder_path, entry)
        if os.path.exists(wav_path):
            normalized_pcm_data = load_wav_as_float(wav_path)
            mel_data = log_mel_frames(normalized_pcm_data)
            X.append(mel_data)
            y.extend([label]*len(mel_data))
            print(mel_data.shape)
        else:
            print(f"Skipping {entry}: missing wav")

X = np.vstack(X)
y = np.array(y, dtype=np.int32)

X_train, X_temp, y_train, y_temp = train_test_split(X, y, test_size=0.3, stratify=y, random_state=42)
X_val, X_test,   y_val, y_test   = train_test_split(X_temp, y_temp, test_size=0.5, stratify=y_temp, random_state=42)

num_features = N_MELS
model = tf.keras.Sequential([
    tf.keras.Input(shape=(num_features,)),
    tf.keras.layers.Dense(32, activation="relu"),
    tf.keras.layers.Dense(16, activation="relu"),
    tf.keras.layers.Dense(2, activation="softmax"),
])

model.compile(
    optimizer=tf.keras.optimizers.Adam(1e-3),
    loss="sparse_categorical_crossentropy",
    metrics=["accuracy"],
)



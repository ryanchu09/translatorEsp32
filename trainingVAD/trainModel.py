import os
import wave
base_dir = "trainingVAD/data"
import numpy as np
import librosa


frame_length_ms = 30
frame_hop_ms = 15
sample_rate = 16000
frame_length = int(sample_rate * frame_length_ms /1000)
frame_hop = int(sample_rate * frame_hop_ms / 1000)

def load_wav_as_float(path):
    y, sr = librosa.load(path)
    
    with wave.open(path, "rb") as wav:
        assert wav.getframerate() == sample_rate
        assert wav.getnchannels() == 1
        data = wav.readframes(wav.getnframes())
        pcm = np.frombuffer(data, dtype=np.int16)
        pcm = pcm.astype(np.float32) / 32768.0
    return pcm

def make_frames(signal):
    frames = []
    #if signal had 100 frames in it, each chunk has 10 frames, and frame_hope was 5,
    #then it would collect 0-9, 5-14 until 90-99
    for start in range(0, len(signal) - frame_length + 1, frame_hop):
        end = start + frame_length
        frames.append(signal[start:end])
    return np.array(frames, dtype = signal.dtype)

AUDIO_FOLDERS = {
    "speech" : 1,
    "background" : 0
}
all_labels = []
all_frames =  []
for folder, label in AUDIO_FOLDERS.items():
    folder_path = os.path.join(base_dir, folder)
    for entry in os.listdir(folder_path):
        sample_dir = os.path.join(base_dir, entry)
        if not os.path.isdir(sample_dir):
            continue

        raw_path = os.path.join(sample_dir, f"{entry}.raw")
        wav_path = os.path.join(sample_dir, f"{entry}.wav")

        if os.path.exists(raw_path) and os.path.exists(wav_path):

            print(f"Processing {entry}")
            with open(raw_path, "rb") as raw, open(wav_path, "rb") as wav:
                raw_data = raw.read()
                wav_data = wav.read()
            normalized_pcm_data = load_wav_as_float(wav_data)
            frames = make_frames(normalized_pcm_data)
            all_frames.append(frames)
            all_labels.append([label] * len(frames))
        else:
            print(f"Skipping {entry}: missing raw or wav")
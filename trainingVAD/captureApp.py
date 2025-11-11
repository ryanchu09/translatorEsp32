from flask import Flask, request, jsonify, send_file, abort
from openai import OpenAI  # noqa: F401  # Placeholder import for future use
import os
import json
import struct
import time
import re
import shutil
# from translator_app.STT import process_audio, LANGUAGE_MEMORY

app = Flask(__name__)



BASE_DIR = os.path.abspath(os.path.dirname(__file__))
DATA_DIR = os.path.join(BASE_DIR, "data")
SPEECH_DIR = os.path.join(DATA_DIR, "speech")
BACKGROUND_DIR = os.path.join(DATA_DIR, "bg")
os.makedirs(SPEECH_DIR, exist_ok=True)
os.makedirs(BACKGROUND_DIR, exist_ok=True)


# Checks to make sure the SID only contains valid symbols
TEXT_PATTERN = re.compile(r"^[A-Za-z0-9_.-]+$")
META_FILENAME = "meta.json"
RAW_FILENAME = "audio.raw"
WAV_FILENAME = "audio.wav"
SESSIONS = {}

def wav_header(data_bytes: int, sample_rate: int, bits_per_sample: int, channels: int) -> bytes:
    """Construct a basic PCM WAV header."""
    byte_rate = sample_rate * channels * bits_per_sample // 8
    block_align = channels * bits_per_sample // 8
    header = b"RIFF"
    header += struct.pack("<I", 36 + data_bytes)
    header += b"WAVEfmt "
    header += struct.pack("<I", 16)  # fmt chunk size
    header += struct.pack("<H", 1)   # PCM format
    header += struct.pack("<H", channels)
    header += struct.pack("<I", sample_rate)
    header += struct.pack("<I", byte_rate)
    header += struct.pack("<H", block_align)
    header += struct.pack("<H", bits_per_sample)
    header += b"data"
    header += struct.pack("<I", data_bytes)
    return header


def _extract_last_flag(args) -> bool:
    """Parse the `last` flag from the query string."""
    raw = args.get("last", "")
    norm = str(raw).strip().lower()
    if norm in ("", "1", "true", "yes", "y", "on"):
        return True
    if norm in ("0", "false", "no", "off"):
        return False
    return False

def next_file_name(label_dir, prefix):
    # os.makedirs(label_dir, exist_ok = True)
    count = 0
    for name in os.listdir(label_dir):
        if name.startswith(prefix) and name.endswith(".wav"):
            count += 1
    return (os.path.join(label_dir, f"{prefix}{count+1}.wav"), os.path.join(label_dir, f"{prefix}{count+1}.raw"))

def _data_paths(label, sid):
    if sid in SESSIONS:
        return SESSIONS[sid]
    
    if TEXT_PATTERN.match(label) and label == "speech": 
        wav_path, raw_path = next_file_name(SPEECH_DIR, "speech")
    elif TEXT_PATTERN.match(label) and label == "background": 
        wav_path, raw_path = next_file_name(BACKGROUND_DIR, "background")   
    # returns a dict o all the dictionaries that hold the data of an audio chunk
    else:
        raise ValueError("unsupported label")
    
    SESSIONS[sid] = { "file_name": wav_path, "raw_name": raw_path}
    return SESSIONS[sid]

# audio-chunk route
@app.route("/audio-chunk", methods=["POST"])
def receive_audio_chunk():
    sid = request.args.get("sid")
    #catches errors with the sid
    if not sid:
        return jsonify({"error": "missing sid"}), 400
    if not TEXT_PATTERN.match(sid):
        return jsonify({"error": "invalid sid"}), 400

    # gets the seq of the current chunk
    seq_str = request.args.get("seq")
    if seq_str is None:
        return jsonify({"error": "missing seq"}), 400
    try:
        seq = int(seq_str)
    except ValueError:
        return jsonify({"error": "seq must be an integer"}), 400
    if seq < 0:
        return jsonify({"error": "seq must be non-negative"}), 400
    
    label = request.args.get("label")
    if label is None:
        return jsonify({"error": "missing label"}), 400
    if not TEXT_PATTERN.match(label):
        return jsonify({"error": "invalid label"}), 400
    if label not in ("speech", "background"):
        return jsonify({"error":"incorrect label"}), 400
    # gets the data to build the meta file
    sample_rate = request.args.get("sr", 16000, type=int) or 16000
    bits_per_sample = request.args.get("bits", 16, type=int) or 16
    channels = request.args.get("ch", 1, type=int) or 1
    if sample_rate <= 0 or bits_per_sample <= 0 or channels <= 0:
        return jsonify({"error": "invalid audio parameters"}), 400

    chunk = request.get_data(cache=False)
    if not chunk:
        return jsonify({"error": "empty payload"}), 400

    last_flag = _extract_last_flag(request.args)
    try:
        download_path = _data_paths(label, sid)
    except ValueError as exc:
        return jsonify({"error": str(exc)}), 400

    
    # writes the raw pcm data
    with open(download_path["raw_name"], "ab") as raw_out:
        raw_out.write(chunk)


    # creates a reponse dict that helps debug
    response = {
        "status": "ok",
        "sid": sid,
        "seq": seq,
        "last": last_flag,
        "bytes_received": len(chunk),
    }

    # handles the case when the last chunk has been sent
    if last_flag:
        # reads the raw pcm_bytes
        with open(download_path["raw_name"], "rb") as raw_in:
            pcm_bytes = raw_in.read()
        # creates the wav header combined with the pcm data
        wav_bytes = wav_header(len(pcm_bytes), sample_rate, bits_per_sample, channels) + pcm_bytes
        # writes the wav data into a file
        with open(download_path["file_name"], "wb") as wav_out:
            wav_out.write(wav_bytes)
        response["total_bytes"] = len(wav_bytes)
        try:
            os.remove(download_path["raw_name"])
        except FileNotFoundError:
            pass

        print(response)
    return jsonify(response), 200


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000, debug=True)

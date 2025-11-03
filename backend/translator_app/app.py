from flask import Flask, request, jsonify, send_file, abort
from openai import OpenAI  # noqa: F401  # Placeholder import for future use
import os
import json
import struct
import time
import re
import shutil
from translator_app.STT import process_audio, LANGUAGE_MEMORY

app = Flask(__name__)

BASE_DIR = os.path.abspath(os.path.dirname(__file__))
SESS_DIR = os.path.join(BASE_DIR, "sessions")
os.makedirs(SESS_DIR, exist_ok=True)


def cleanup_sessions():
    """Delete any lingering session artifacts from previous runs."""
    for name in os.listdir(SESS_DIR):
        path = os.path.join(SESS_DIR, name)
        try:
            if os.path.isdir(path):
                shutil.rmtree(path)
            else:
                os.remove(path)
        except OSError as exc:
            app.logger.warning("Failed to remove %s: %s", path, exc)


cleanup_sessions()

# Checks to make sure the SID only contains valid symbols
SID_PATTERN = re.compile(r"^[A-Za-z0-9_.-]+$")
META_FILENAME = "meta.json"
RAW_FILENAME = "audio.raw"
WAV_FILENAME = "audio.wav"


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


def _session_paths(sid: str):
    session_dir = os.path.join(SESS_DIR, sid)
    # returns a dict of all the dictionaries that hold the data of an audio chunk
    return {
        "session": session_dir,
        "meta": os.path.join(session_dir, META_FILENAME),
        "raw": os.path.join(session_dir, RAW_FILENAME),
        "wav": os.path.join(session_dir, WAV_FILENAME),
    }

# audio-chunk route
@app.route("/audio-chunk", methods=["POST"])
def receive_audio_chunk():
    sid = request.args.get("sid")

    #catches errors with the sid
    if not sid:
        return jsonify({"error": "missing sid"}), 400
    if not SID_PATTERN.match(sid):
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
    paths = _session_paths(sid)
    os.makedirs(paths["session"], exist_ok=True)

    meta = {}
    if os.path.exists(paths["meta"]):
        # tries to load any current meta data, if there is non start a new meta dict
        with open(paths["meta"], "r", encoding="utf-8") as meta_in:
            try:
                meta = json.load(meta_in)
            except json.JSONDecodeError:
                meta = {}
    #check if starting new meta file
    starting_new = seq == 0 or not meta
    if starting_new:
        meta = {
            "sid": sid,
            "sample_rate": sample_rate,
            "bits_per_sample": bits_per_sample,
            "channels": channels,
            "next_seq": 0,
            "created_at": time.time(),
            "language1" : None,
            "language2" : None
        }
        with open(paths["raw"], "wb") as raw_reset:
            raw_reset.truncate(0)
    else:
        # checks if meta parameters changed
        if (
            meta.get("sample_rate") != sample_rate
            or meta.get("bits_per_sample") != bits_per_sample
            or meta.get("channels") != channels
        ):
            return jsonify({"error": "audio parameters changed mid-stream"}), 400

    #checks if seq has changed
    expected_seq = meta.get("next_seq", 0)
    if seq != expected_seq:
        return jsonify({"error": "unexpected seq", "expected": expected_seq, "received": seq}), 409

    # writes the raw pcm data
    with open(paths["raw"], "ab") as raw_out:
        raw_out.write(chunk)

    # updates meta parameters
    meta["next_seq"] = seq + 1
    meta["updated_at"] = time.time()

    # creates a reponse dict that helps debug
    response = {
        "status": "ok",
        "sid": sid,
        "seq": seq,
        "next_seq": meta["next_seq"],
        "last": last_flag,
        "bytes_received": len(chunk),
        "languages": LANGUAGE_MEMORY
    }

    # handles the case when the last chunk has been sent
    if last_flag:
        meta["complete"] = True
        # reads the raw pcm_bytes
        with open(paths["raw"], "rb") as raw_in:
            pcm_bytes = raw_in.read()
        # creates the wav header combined with the pcm data
        wav_bytes = wav_header(len(pcm_bytes), sample_rate, bits_per_sample, channels) + pcm_bytes
        # writes the wav data into a file
        with open(paths["wav"], "wb") as wav_out:
            wav_out.write(wav_bytes)
        meta["wav_path"] = paths["wav"]
        response["wav_file"] = paths["wav"]
        response["total_bytes"] = len(wav_bytes)
        sample_wav = paths["wav"]
        print(response)
        #, "/Users/ryanchu/Documents/TranslatorFlask/TranslatorWebpage/testing1.wav"
        result = process_audio(sample_wav, voice=None)
        print("Detected:", result["source_language"])
        print("Transcript:", result["transcript"])
        print("Translation:", result["translation"])
        if result["synthesized_wav"]:
            print("Spoken translation saved to:", result["synthesized_wav"])





    # writes the meta data
    with open(paths["meta"], "w", encoding="utf-8") as meta_out:
        json.dump(meta, meta_out, indent=2)

    return jsonify(response), 200


@app.route("/audio-wav", methods=["GET"])
def get_audio_wav():
    sid = request.args.get("sid")
    if not sid or not SID_PATTERN.match(sid):
        abort(400)
    paths = _session_paths(sid)
    if not os.path.exists(paths["wav"]):
        abort(404)
    return send_file(paths["wav"], mimetype="audio/wav", as_attachment=True, download_name=f"{sid}.wav")

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000, debug=True)

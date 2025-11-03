from __future__ import annotations

import os
from pathlib import Path
from typing import Dict, List, Optional

from openai import OpenAI


client = OpenAI()

# Track the first two distinct languages we hear during a session.
LANGUAGE_MEMORY: List[str] = []


def transcribe_with_detection(wav_path: str) -> Dict[str, str]:
    """
    Run Whisper transcription with language detection.

    Returns a dict containing `language` (ISO code) and `text` (transcript).
    """
    with open(wav_path, "rb") as audio_file:
        result = client.audio.transcriptions.create(
            model="gpt-4o-mini-transcribe",
            file=audio_file,
            response_format="json",
            temperature=0,
        )
    payload = result.model_dump()
    language = (
        payload.get("language")
        or payload.get("detected_language")
        or payload.get("metadata", {}).get("language")
        or getattr(result, "language", "")
        or getattr(result, "detected_language", "")
        or ""
    )
    text = payload.get("text", getattr(result, "text", ""))
    language = _language_detection(text)
    return {"language": language, "text": text}


def _language_detection(text: str) -> str:
    """
    Use a lightweight model prompt to guess the ISO 639-1 language code.
    """
    prompt = (
        "Identify the language of the following text. "
        "Return only the two-letter ISO 639-1 code (e.g. en, es, zh). "
        "If you cannot determine it, respond with 'und'.\n\n"
        f"{text}"
    )
    try:
        detect = client.responses.create(
            model="gpt-4o-mini",
            input=[
                {"role": "system", "content": "You are a language detector."},
                {"role": "user", "content": prompt},
            ],
            temperature=0,
        )
        parts = []
        for output in detect.output:
            if output.type == "message":
                for content in output.content:
                    if content.type == "output_text":
                        parts.append(content.text.strip())
        code = "".join(parts).lower()
        print(f"output {detect.output}")
        if len(code) == 2:
            return code
    except Exception:
        pass
    return ""


def _remember_language(lang: str) -> None:
    """Keep track of up to two distinct languages that have been spoken."""
    lang = (lang or "").lower()
    if not lang:
        return
    if lang not in LANGUAGE_MEMORY:
        if len(LANGUAGE_MEMORY) < 2:
            LANGUAGE_MEMORY.append(lang)


def choose_target_language(source_lang: str) -> str:
    """
    Pick the target language based on the first two distinct languages detected.

    Returns an empty string until at least two unique languages have been registered.
    """
    source_lang = (source_lang or "").lower()
    if not source_lang:
        return ""

    _remember_language(source_lang)

    if len(LANGUAGE_MEMORY) < 2:
        return ""

    if source_lang == LANGUAGE_MEMORY[0]:
        return LANGUAGE_MEMORY[1]
    if source_lang == LANGUAGE_MEMORY[1]:
        return LANGUAGE_MEMORY[0]

    # More than two languages were spoken; default to translating into the first registered language.
    return LANGUAGE_MEMORY[0]


def translate_text(text: str, source_lang: str, target_lang: str) -> str:
    """
    Use an OpenAI text model to translate between languages.
    """
    if not text:
        return ""
    response = client.responses.create(
        model="gpt-4o-mini",
        input=[
            {
                "role": "system",
                "content": f"You are a translation engine. Detected source language is {source_lang or 'unknown'}."
            },
            {
                "role": "user",
                "content": f"Translate the following text into {target_lang}:\n{text}"
            },
        ],
        temperature=0,
    )
    # Collect all text outputs from the response payload.
    parts = []
    for output in response.output:
        if output.type == "message":
            for content in output.content:
                if content.type == "output_text":
                    parts.append(content.text)
    return "\n".join(parts).strip()


def synthesize_speech(text: str, output_path: Path, voice: str = "alloy") -> Path:
    """
    Convert text back into speech and save it as a WAV file.
    """
    output_path = output_path.with_suffix(".wav")
    with client.audio.speech.with_streaming_response.create(
        model="gpt-4o-mini-tts",
        voice=voice,
        input=text,
        format="wav",
    ) as stream:
        stream.stream_to_file(output_path)
    return output_path


def process_audio(
    wav_path: str,
    output_dir: Optional[str | Path] = None,
    voice: Optional[str] = None,
) -> Dict[str, Optional[str]]:
    """
    End-to-end helper: transcribe, translate, and optionally synthesize speech.
    """
    wav_path = str(wav_path)
    transcript_payload = transcribe_with_detection(wav_path)
    print(transcript_payload)
    source_lang = transcript_payload["language"]
    transcript_text = transcript_payload["text"]

    target_lang = choose_target_language(source_lang)
    translated_text = translate_text(transcript_text, source_lang, target_lang) if target_lang else ""

    synthesized_path = None
    if voice and translated_text:
        out_dir = Path(output_dir) if output_dir else Path(wav_path).parent
        out_dir.mkdir(parents=True, exist_ok=True)
        speech_path = out_dir / f"{Path(wav_path).stem}_{target_lang}"
        synthesized_path = str(synthesize_speech(translated_text, speech_path, voice=voice))

    return {
        "source_language": source_lang,
        "transcript": transcript_text,
        "target_language": target_lang,
        "translation": translated_text,
        "synthesized_wav": synthesized_path,
    }


# if __name__ == "__main__":
#     # Example usage: run the full pipeline on a sample WAV file.
#     sample_wav = os.environ.get(
#         "TEST_WAV",
#         "/Users/ryanchu/Documents/TranslatorFlask/TranslatorWebpage/testing1.wav",
#     )
#     result = process_audio(sample_wav, voice=None)
#     print("Detected:", result["source_language"])
#     print("Transcript:", result["transcript"])
#     print("Translation:", result["translation"])
#     if result["synthesized_wav"]:
#         print("Spoken translation saved to:", result["synthesized_wav"])

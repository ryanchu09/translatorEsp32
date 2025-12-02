#pragma once
#include <cstdint>
#include <driver/gpio.h>

constexpr char SSID[] = "TELUS8535";
constexpr char PASS[] = "pchu4$$$";
// constexpr char SSID[] = "SD38_Managed";
// constexpr char PASS[] = "je334an-coach-be4m-lucio-help$r-pier-causing-DEEDS";
constexpr char SERVER_IP[] = "192.168.1.138";
constexpr uint16_t SERVER_PORT = 8000;
constexpr char SERVER_PATH_BASE[] = "/audio-chunk";

constexpr int SAMPLE_RATE = 16000;
constexpr int CHANNELS = 1;
constexpr int BITS_PER_SAMPLE = 16;
constexpr int FRAMES_PER_CHUNK = 1024;
constexpr gpio_num_t I2S_WS = static_cast<gpio_num_t>(12);
constexpr gpio_num_t I2S_SCK = static_cast<gpio_num_t>(13);
constexpr gpio_num_t I2S_SD = static_cast<gpio_num_t>(22);
constexpr float RMS_START_THRESHOLD = 0.04f;
constexpr float RMS_END_THRESHOLD = 0.02f;
constexpr uint32_t SILENCE_THRESHOLD_MS = 300;
constexpr uint32_t MAX_CHUNKS_PER_UTTERANCE = static_cast<uint32_t>(10000.0f / (1000.0f * FRAMES_PER_CHUNK / SAMPLE_RATE));


// const char* SSID = "SD38_Managed";
// const char* PASS = "je334an-coach-be4m-lucio-help$r-pier-causing-DEEDS";
// const char* SERVER_IP = "10.9.23.112";
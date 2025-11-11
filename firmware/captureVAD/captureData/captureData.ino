#include <WiFi.h>
// #include <WiFiClient.h>
#include "driver/i2s.h"
const char* SSID = "TELUS8535";
const char* PASS = "pchu4$$$";
const char* SERVER_IP = "192.168.1.138";
#include <esp_system.h>

// const char* SSID = "SD38_Managed";
// const char* PASS = "je334an-coach-be4m-lucio-help$r-pier-causing-DEEDS";
// const char* SERVER_IP = "10.9.21.25";
const uint16_t SERVER_PORT = 8000;
// const char* SERVER_PATH = "/upload-wav";
const char* SERVER_PATH_BASE = "/audio-chunk";

//I2S PINS
#define I2S_WS 12
#define I2S_SCK 13
#define I2S_SD 22

#define I2S_BCLK_SPEAKER 25
#define I2S_LRC 26
#define I2S_DOUT 27


// PCM Header parameters
#define SAMPLE_RATE       16000
#define SECONDS_TO_RECORD 2
#define CHANNELS          1
#define BITS_PER_SAMPLE   16  // we'll convert to 16-bit
#define I2S_READ_BITS     32  // INMP441 gives 24-bit in 32-bit frames
#define FRAMES_PER_CHUNK 1024

const float CHUNK_DURATION = 1000.0f * FRAMES_PER_CHUNK/SAMPLE_RATE;

static int16_t buf[FRAMES_PER_CHUNK];


//tells the ESP32 what i2s port to use (0 or 1)
static const i2s_port_t I2S_RX_PORT = I2S_NUM_0;
 
// Creates a i2s_config struct that contains the data that 
// tells the computer what parameters to use for the I2C communication
static const i2s_config_t i2s_config1_mic = {
  // Tells the computer the mode of the I2S port (master or slave, RX or TX)
  .mode = (i2s_mode_t) (I2S_MODE_MASTER | I2S_MODE_RX),
  // sample rate of the I2S (16k is standard for speech)
  .sample_rate = 16000,
  // says the bits per sample
  // Using 32bit, because inmp441 outputs 24 bit, so 32 bit contains it nicely
  .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
  // tells the computer which channles are being used (only left, because LR is connected to ground)
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  // Standard phillips communication
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  // Sets the level of the interrupt flag when the DMA buffer is full
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  // number of DMA Buffers
  .dma_buf_count = 8,
  // number of a samples per buffer
  .dma_buf_len = 256,
  // whether or not to use apll, which is a more precise clock
  .use_apll = false,
  // whetehr or not to clear the buffer when there isn't enough data(relevant for tx, not rx)
  .tx_desc_auto_clear = false,
  // automatically calculate the master clock
  .fixed_mclk = 0
};


static const i2s_pin_config_t pin_config1_mic = {
  // set up pins
  .bck_io_num = I2S_SCK,
  .ws_io_num = I2S_WS,
  .data_out_num = I2S_PIN_NO_CHANGE,
  .data_in_num = I2S_SD
};


bool read_block_int16(int16_t* out, size_t frames) {
  size_t br;
  int32_t s32;
  float RMS = 0.0;
  for (size_t i = 0; i < frames; i++) {
    if (i2s_read(I2S_RX_PORT, &s32, sizeof(s32), &br, portMAX_DELAY) != ESP_OK || br != sizeof(s32)) {
      return false;
    }
    int16_t s16 = constrain((int16_t)(s32 >> 11), -32767, 32767);
    out[i] = s16;
    float sample = float(s16) / 32768.0f;
    
  }
  
  return true;
}

bool post_chunk(const char* sid, uint32_t seq, const uint8_t* data, size_t len, bool last, const char* label) {
  WiFiClient client;
  if (!client.connect(SERVER_IP, SERVER_PORT)) {
    Serial.println("HTTP connect failed");
    return false;
  }

  // generates the url path. contains the session id(sid), chunk sequence number(seq) as well as other parameters about the data
  String path = String(SERVER_PATH_BASE) + 
                "?sid=" + sid + 
                "&seq=" + seq + 
                "&last=" + (last ? "1" : "0") + 
                "&sr=" + SAMPLE_RATE + 
                "&bits=" + BITS_PER_SAMPLE + 
                "&ch=" + CHANNELS + "&label=" + label;
  // Starts the http request, tells it it's post, and subsistues the path as a string
  client.printf("POST %s HTTP/1.1\r\n", path.c_str());
  client.printf("Host: %s:%u\r\n", SERVER_IP, SERVER_PORT);
  client.printf("Content-Type: application/octet-stream\r\n");
  client.printf("Content-Length: %u\r\n", (unsigned)len);
  client.print("Connection:close\r\n\r\n");
  client.write(data,len);

    // Read a short response (optional)
  uint32_t t0 = millis();
  while (client.connected() || client.available()) {
    if (client.available()) Serial.write(client.read());
    if (millis() - t0 > 3000) break;
  }
  client.stop();

  return true;
}



// String sid;
unsigned long startMicros = 0;
const unsigned long intervalMicros = 10000000;
const char* speech = "speech";
const char* background = "background";
void setup() {
  // put your setup code here, to run once:
  // pinMode(13, OUTPUT);
  // digitalWrite(13, HIGH)
  i2s_driver_install(I2S_RX_PORT, &i2s_config1_mic, 0, NULL);
  i2s_set_pin(I2S_RX_PORT, &pin_config1_mic);
  Serial.begin(115200);

  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());
  // sid = make_session_id();
  

}

String make_session_id() {
  String s = String((uint32_t)ESP.getEfuseMac(), HEX);
  s += "-";
  s += String(esp_random(), HEX);   // hardware RNG → hex string
  return s;
}

void loop() {
  startMicros = micros();
  // put your main code here, to run repeatedly:
  // simple_read_function();
  // const uint32_t chunks_to_send = (SAMPLE_RATE * SECONDS_TO_RECORD) / FRAMES_PER_CHUNK;
  String sid = make_session_id();
  bool last=false;
  uint32_t seq = 0;
  while (!last) {
    if (!read_block_int16(buf, FRAMES_PER_CHUNK)) {
      Serial.println("read failed");
      break;
    }
    unsigned long now = micros();
    last = (now - startMicros) >= intervalMicros;
    if (!post_chunk(sid.c_str(), seq++, (uint8_t*)buf, FRAMES_PER_CHUNK * sizeof(int16_t), last, speech)) {
      Serial.println("didn't connect");
    }
  }
  while(true){delay(1000);};

  
}


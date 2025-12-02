#include <algorithm>
#include <cmath>
#include <cstdint>
#include <climits>
#include <cstdio>
#include <cstring>
#include <inttypes.h>
#include <string>

#include <driver/i2s_std.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include <esp_wifi_default.h>
#include <esp_err.h>
#include <esp_system.h>

#include "constant.h"
#include "streamer.h"

static const char* TAG = "streamer";

static i2s_chan_handle_t g_i2s_rx_chan = nullptr;
static constexpr uint32_t kI2sReadTimeoutMs = UINT32_MAX;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "Wi-Fi started, connecting...");
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGW(TAG, "Wi-Fi disconnected, reconnecting...");
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = static_cast<ip_event_got_ip_t *>(event_data);
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init_sta()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    wifi_config_t wifi_config = {};
    std::memcpy(wifi_config.sta.ssid, SSID, sizeof(wifi_config.sta.ssid));
    std::memcpy(wifi_config.sta.password, PASS, sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void i2s_init()
{
    if (g_i2s_rx_chan != nullptr) {
        return;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &g_i2s_rx_chan));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SCK,
            .ws = I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_SD,
            .invert_flags = {false, false, false},
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(g_i2s_rx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(g_i2s_rx_chan));
}

bool read_block_int16(int16_t *out, size_t frames)
{
    size_t br = 0;
    int32_t s32 = 0;
    float RMS = 0.0f;

    for (size_t i = 0; i < frames; ++i) {
        if (g_i2s_rx_chan == nullptr ||
            i2s_channel_read(g_i2s_rx_chan, &s32, sizeof(s32), &br, kI2sReadTimeoutMs) != ESP_OK ||
            br != sizeof(s32)) {
            return false;
        }
        int32_t sample32 = s32 >> 11;
        int32_t clamped = std::clamp<int32_t>(sample32, -32767, 32767);
        int16_t s16 = static_cast<int16_t>(clamped);
        out[i] = s16;
        float sample = static_cast<float>(s16) / 32768.0f;
        RMS += sample * sample;
    }

    float rmsFinal = std::sqrt(RMS / frames);
    ESP_LOGI(TAG, "chunk RMS %.4f", rmsFinal);

    return true;
}



bool post_chunk(const char *sid, uint32_t seq, const uint8_t *data, size_t len, bool last)
{
    char url[512];
    int last_flag = last ? 1 : 0;
    int url_len = std::snprintf(url, sizeof(url),
                                "http://%s:%" PRIu16 "%s?sid=%s&seq=%" PRIu32 "&last=%d&sr=%d&bits=%d&ch=%d",
                                SERVER_IP, SERVER_PORT, SERVER_PATH_BASE, sid, seq, last_flag,
                                SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS);
    if (url_len < 0 || url_len >= static_cast<int>(sizeof(url))) {
        ESP_LOGE(TAG, "chunk URL truncated");
        return false;
    }

    if (len > INT_MAX) {
        ESP_LOGE(TAG, "chunk length %zu exceeds HTTP client limit", len);
        return false;
    }
    esp_http_client_config_t config = {};
    config.url = url;
    config.method = HTTP_METHOD_POST;
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "failed to initialize HTTP client");
        return false;
    }

    bool success = false;
    int content_length = static_cast<int>(len);
    do {
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
        esp_http_client_set_header(client, "Connection", "close");

        esp_err_t err = esp_http_client_open(client, content_length);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "http open failed (%s)", esp_err_to_name(err));
            break;
        }

        int written = esp_http_client_write(client, reinterpret_cast<const char *>(data), content_length);
        if (written < 0 || written != content_length) {
            ESP_LOGE(TAG, "http write mismatch (%d != %d)", written, content_length);
            break;
        }

        err = esp_http_client_fetch_headers(client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "fetch headers failed (%s)", esp_err_to_name(err));
            break;
        }

        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "server responded with status %d", status);
            break;
        }

        success = true;
    } while (false);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return success;
}

std::string make_session_id()
{
    constexpr char hex[] = "0123456789abcdef";
    std::string sid;
    sid.resize(16);
    uint32_t value = esp_random();
    for (int i = 0; i < 8; ++i) {
        sid[i] = hex[(value >> (28 - (i * 4))) & 0xF];
    }
    value = esp_random();
    for (int i = 0; i < 8; ++i) {
        sid[8 + i] = hex[(value >> (28 - (i * 4))) & 0xF];
    }
    return sid;
}

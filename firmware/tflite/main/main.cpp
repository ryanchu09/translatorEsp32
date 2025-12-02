#include <array>
#include <vector>
#include <stdio.h>

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include "streamer.h"
// #include "test_kissfft.h"
#include "inference.h"

static const char* TAG = "main";
#define TEST_MODEL

#ifdef TEST_MODEL
static void run_model_test()
{
    if (!inference_init()) {
        ESP_LOGE(TAG, "inference_init failed");
        return;
    }

    static std::array<int16_t, kInferenceFftSize> samples = {};
    static std::array<float, kInferenceLogMelBins> logmel_out = {};

    if (!inference_extract_logmel(samples.data(), logmel_out.data(), logmel_out.size())) {
        ESP_LOGE(TAG, "inference_extract_logmel failed");
        return;
    }

    const size_t output_len = inference_output_size();
    if (output_len == 0) {
        ESP_LOGW(TAG, "model output size is zero");
        return;
    }

    std::vector<float> output(output_len);
    if (!inference_invoke_model(logmel_out.data(), logmel_out.size(), output.data(), output_len)) {
        ESP_LOGE(TAG, "inference_invoke_model failed");
        return;
    }

    for (size_t i = 0; i < output.size(); ++i) {
        ESP_LOGI(TAG, "model output[%zu]=%.6f", i, output[i]);
    }
}
#endif

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();
    i2s_init();

#ifdef TEST_MODEL
    run_model_test();
#endif
}

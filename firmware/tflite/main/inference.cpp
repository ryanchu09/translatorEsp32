#include "inference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <esp_log.h>

#include "constant.h"
#include "kiss_fft.h"
#include "../managed_components/espressif__esp-tflite-micro/third_party/kissfft/kiss_fft.c"
#include "tools/kiss_fftr.h"
#include "../managed_components/espressif__esp-tflite-micro/third_party/kissfft/tools/kiss_fftr.c"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "model_data.h"

extern const unsigned char g_quantized_model[];

namespace {

constexpr int kLogMelEdges = static_cast<int>(kInferenceLogMelBins + 2);
constexpr int kFftBins = static_cast<int>(kInferenceFftSize / 2 + 1);
constexpr size_t kArenaSize = 16 * 1024;
constexpr float kLogMelFloor = 1e-8f;
constexpr float kPi = 3.14159265358979323846f;
static const char* TAG = "inference";

alignas(16) static uint8_t arena[kArenaSize];
static float window_buffer[kInferenceFftSize];
static kiss_fft_cpx spectrum_buffer[kFftBins];
static float magnitude_buffer[kFftBins];
static std::array<float, kLogMelEdges> mel_edges = {};
constexpr size_t kFftCfgBufferSize = 24 * 1024;
alignas(16) static uint8_t fft_cfg_buffer[kFftCfgBufferSize];

using OpResolver = tflite::MicroMutableOpResolver<12>;
static OpResolver op_resolver;
static tflite::MicroInterpreter* interpreter = nullptr;
static const tflite::Model* model = nullptr;
static kiss_fftr_cfg fft_cfg = nullptr;
static bool fft_cfg_from_heap = false;
static bool initialized = false;

float hz_to_mel(float hz)
{
    return 2595.0f * std::log10f(1.0f + hz / 700.0f);
}

float mel_to_hz(float mel)
{
    return 700.0f * (std::powf(10.0f, mel / 2595.0f) - 1.0f);
}

bool add_op(TfLiteStatus status, const char* name)
{
    if (status != kTfLiteOk) {
        ESP_LOGE(TAG, "failed to add %s op", name);
        return false;
    }
    return true;
}

void build_mel_edges()
{
    const float mel_low = hz_to_mel(0.0f);
    const float mel_high = hz_to_mel(static_cast<float>(SAMPLE_RATE) / 2.0f);
    const float mel_step = (mel_high - mel_low) / (kInferenceLogMelBins + 1);
    for (int i = 0; i < kLogMelEdges; ++i) {
        mel_edges[i] = mel_to_hz(mel_low + mel_step * i);
    }
}

bool configure_ops()
{
    return add_op(op_resolver.AddConv2D(), "Conv2D") &&
           add_op(op_resolver.AddDepthwiseConv2D(), "DepthwiseConv2D") &&
           add_op(op_resolver.AddFullyConnected(), "FullyConnected") &&
           add_op(op_resolver.AddSoftmax(), "Softmax") &&
           add_op(op_resolver.AddAdd(), "Add") &&
           add_op(op_resolver.AddMul(), "Mul") &&
           add_op(op_resolver.AddRelu(), "Relu") &&
           add_op(op_resolver.AddLogistic(), "Logistic") &&
           add_op(op_resolver.AddReshape(), "Reshape") &&
           add_op(op_resolver.AddQuantize(), "Quantize") &&
           add_op(op_resolver.AddDequantize(), "Dequantize") &&
           add_op(op_resolver.AddAveragePool2D(), "AveragePool2D");
}

bool ensure_model_ok()
{
    model = tflite::GetModel(g_quantized_model);
    if (model == nullptr || model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "model missing or schema mismatch");
        return false;
    }
    return true;
}

}  // namespace

bool inference_init()
{
    if (initialized) {
        return true;
    }

    ESP_LOGI(TAG, "initializing inference helpers");

    if (!ensure_model_ok()) {
        ESP_LOGE(TAG, "model validation failed");
        return false;
    }

    if (!configure_ops()) {
        ESP_LOGE(TAG, "op configuration failed");
        return false;
    }

    // Reserve the interpreter arena before we do anything heap-heavy.
    static tflite::MicroInterpreter static_interpreter(model, op_resolver, arena, kArenaSize);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(TAG, "tensor allocation failed");
        return false;
    }

    ESP_LOGI(TAG, "allocated tensors, arena at %zu bytes", kArenaSize);

    // Try to build the kiss FFT state in a pre-sized static buffer to keep heap usage predictable.
    size_t fft_cfg_size = kFftCfgBufferSize;
    fft_cfg = kiss_fftr_alloc(static_cast<int>(kInferenceFftSize), 0,
                              fft_cfg_buffer, &fft_cfg_size);
    fft_cfg_from_heap = false;
    if (fft_cfg == nullptr) {
        ESP_LOGW(TAG, "static FFT buffer too small (%zu bytes needed), falling back to heap",
                 fft_cfg_size);
        fft_cfg = kiss_fftr_alloc(static_cast<int>(kInferenceFftSize), 0, nullptr, nullptr);
        fft_cfg_from_heap = true;
    }
    if (fft_cfg == nullptr) {
        ESP_LOGE(TAG, "kissfftr alloc failed");
        return false;
    }

    ESP_LOGI(TAG, "fft cfg initialized%s", fft_cfg_from_heap ? " from heap" : " from static buffer");

    build_mel_edges();
    initialized = true;
    return true;
}

void inference_shutdown()
{
    if (fft_cfg != nullptr) {
        if (fft_cfg_from_heap) {
            // Free only if we allocated the FFT state on the heap; static buffer lives for program lifetime.
            kiss_fftr_free(fft_cfg);
            fft_cfg_from_heap = false;
        }
        fft_cfg = nullptr;
    }
    interpreter = nullptr;
    model = nullptr;
    initialized = false;
}

bool inference_extract_logmel(const int16_t *samples, float *logmel_out, size_t logmel_len)
{
    if (!initialized || samples == nullptr || logmel_out == nullptr || logmel_len < kInferenceLogMelBins) {
        ESP_LOGE(TAG, "invalid arguments or module uninitialized in extract_logmel");
        return false;
    }

    const float denominator = static_cast<float>(kInferenceFftSize - 1);
    for (size_t i = 0; i < kInferenceFftSize; ++i) {
        float sample = static_cast<float>(samples[i]) / 32768.0f;
        float window = 0.5f * (1.0f - std::cosf(2.0f * kPi * static_cast<float>(i) / denominator));
        window_buffer[i] = sample * window;
    }

    kiss_fftr(fft_cfg, window_buffer, spectrum_buffer);

    for (int i = 0; i < kFftBins; ++i) {
        const float re = spectrum_buffer[i].r;
        const float im = spectrum_buffer[i].i;
        magnitude_buffer[i] = std::sqrt(re * re + im * im);
    }

    const float bin_step = static_cast<float>(SAMPLE_RATE) / static_cast<float>(kInferenceFftSize);
    for (int mel = 0; mel < static_cast<int>(kInferenceLogMelBins); ++mel) {
        const float lower = mel_edges[mel];
        const float center = mel_edges[mel + 1];
        const float upper = mel_edges[mel + 2];
        const float left_range = center - lower;
        const float right_range = upper - center;

        float sum = 0.0f;
        for (int bin = 0; bin < kFftBins; ++bin) {
            const float freq = static_cast<float>(bin) * bin_step;
            float weight = 0.0f;
            if (freq >= lower && freq <= center && left_range > 0.0f) {
                weight = (freq - lower) / left_range;
            } else if (freq >= center && freq <= upper && right_range > 0.0f) {
                weight = (upper - freq) / right_range;
            }
            if (weight <= 0.0f) {
                continue;
            }
            sum += weight * magnitude_buffer[bin];
        }
        logmel_out[mel] = std::logf(std::max(sum, kLogMelFloor));
    }

    return true;
}

bool inference_invoke_model(const float *logmel_input, size_t logmel_len,
                            float *output_buffer, size_t output_len)
{
    if (!initialized || logmel_input == nullptr || output_buffer == nullptr) {
        ESP_LOGE(TAG, "invoke_model called with invalid state/args");
        return false;
    }

    TfLiteTensor *input = interpreter->input(0);
    if (input == nullptr) {
        return false;
    }

    const size_t expected_len = static_cast<size_t>(input->bytes) / sizeof(float);
    if (logmel_len < expected_len) {
        ESP_LOGE(TAG, "feature length (%zu) shorter than expected (%zu)", logmel_len, expected_len);
        return false;
    }

    std::copy_n(logmel_input, expected_len, tflite::GetTensorData<float>(input));

    if (interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(TAG, "interpreter invoke failed");
        return false;
    }

    TfLiteTensor *output = interpreter->output(0);
    if (output == nullptr) {
        ESP_LOGE(TAG, "model output tensor is NULL");
        return false;
    }

    const size_t output_elements = static_cast<size_t>(output->bytes) / sizeof(float);
    const size_t copy_count = std::min(output_len, output_elements);
    if (copy_count > 0) {
        std::copy_n(tflite::GetTensorData<float>(output), copy_count, output_buffer);
    }

    return true;
}

size_t inference_output_size()
{
    if (!initialized || interpreter == nullptr) {
        return 0;
    }

    TfLiteTensor *output = interpreter->output(0);
    if (output == nullptr) {
        return 0;
    }

    return static_cast<size_t>(output->bytes) / sizeof(float);
}

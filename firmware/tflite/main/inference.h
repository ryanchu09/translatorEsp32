#pragma once

#include <cstddef>
#include <cstdint>

/// Number of log-Mel bins produced by this helper.
constexpr size_t kInferenceLogMelBins = 24;

/// Size of the FFT window (must match the chunk size we record).
constexpr size_t kInferenceFftSize = 1024;

#ifdef __cplusplus
extern "C" {
#endif

/// Sets up the TensorFlow Lite Micro interpreter and KissFFT helpers.
bool inference_init();

/// Releases any resources held by the inference helpers.
void inference_shutdown();

/// Converts a single audio chunk (kInferenceFftSize samples) into log-Mel features.
/// Returns false if the helper is not initialized or the provided buffer is too small.
bool inference_extract_logmel(const int16_t *samples, float *logmel_out, size_t logmel_len);

/// Copies the provided log-Mel features into the model input, invokes inference,
/// and copies up to `output_len` floats from the model output tensor.
bool inference_invoke_model(const float *logmel_input, size_t logmel_len,
                            float *output_buffer, size_t output_len);

/// Returns the number of floats produced by the model's output tensor.
size_t inference_output_size();

#ifdef __cplusplus
}
#endif

idf_component_register(
    SRCS
        # # MicroFrontend
        # "src/tensorflow/lite/experimental/microfrontend/lib/frontend.c"
        # "src/tensorflow/lite/experimental/microfrontend/lib/frontend_util.c"
        # "src/tensorflow/lite/experimental/microfrontend/lib/filterbank.c"
        # "src/tensorflow/lite/experimental/microfrontend/lib/pcan_gain_control.c"
        # "src/tensorflow/lite/experimental/microfrontend/lib/noise_reduction.c"
        # "src/tensorflow/lite/experimental/microfrontend/lib/log_lut.c"
        # "src/tensorflow/lite/experimental/microfrontend/lib/window.c"

        # Kiss FFT
        "kissfft/kiss_fft.c"
        "kissfft/kiss_fftr.c"

        # TensorFlow Lite Micro core files...
        # (your existing list here)
    INCLUDE_DIRS
        "."
)
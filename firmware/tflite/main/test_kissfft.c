#include "kiss_fft.h"
#include "tools/kiss_fftr.h"
#include <stdio.h>


void test_kissfft()
{
    printf("Running KissFFT test...\n");

    int nfft = 16;
    kiss_fft_cfg cfg = kiss_fft_alloc(nfft, 0, NULL, NULL);

    kiss_fft_cpx in[16] = {0};
    kiss_fft_cpx out[16];

    // Test input: impulse → FFT should be all 1+0i
    in[0].r = 1.0f;
    in[0].i = 0.0f;

    kiss_fft(cfg, in, out);

    // Print first few FFT results
    for (int i = 0; i < 4; i++) {
        printf("Out[%d] = (%f, %f)\n", i, out[i].r, out[i].i);
    }

    printf("KissFFT test done.\n");
}

#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

void wifi_init_sta();
void i2s_init();
bool read_block_int16(int16_t *out, size_t frames);
bool post_chunk(const char *sid, uint32_t seq, const uint8_t *data, size_t len, bool last);

#ifdef __cplusplus
}
#endif

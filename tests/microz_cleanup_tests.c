#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Inspect live allocations immediately before free; never read freed memory.
static void audited_free(void *pointer);
#define free audited_free
#include "../lib/microz/microz.c"
#undef free

static int phase, releases, failures;

static void audited_free(void *pointer)
{
    if (pointer != NULL && phase != 0) {
        size_t size = phase == 1 ? sizeof(tdefl_compressor) : sizeof(inflate_state);
        const unsigned char *bytes = (const unsigned char *)pointer;
        ++releases;
        for (size_t i = 0; i < size; ++i) {
            if (bytes[i] != 0) {
                fprintf(stderr, "Sensitive compression state was not erased (phase %d).\n", phase);
                ++failures;
                break;
            }
        }
    }
    free(pointer);
}

int main(void)
{
    unsigned char input[6000], output[6000];
    const char marker[] = "synthetic secret for cleanup regression";
    uint32_t compressed_len = microz_compress_bound(sizeof(input));
    unsigned char *compressed = (unsigned char *)malloc(compressed_len);
    uint32_t output_len = sizeof(output);
    if (compressed == NULL) return 1;
    for (size_t i = 0; i < sizeof(input); ++i) input[i] = marker[i % (sizeof(marker) - 1)];
    phase = 1;
    if (microz_compress_block(input, sizeof(input), compressed, &compressed_len) != MICROZ_OK) return 1;
    phase = 2;
    if (microz_decompress_block(compressed, compressed_len, output, &output_len) != MICROZ_OK ||
        output_len != sizeof(input) || memcmp(input, output, sizeof(input)) != 0) return 1;
    // Error cleanup after the dictionary has received some plaintext.
    output_len = 1;
    (void)microz_decompress_block(compressed, compressed_len, output, &output_len);
    // Invalid block type, exercising failure before any output is produced.
    compressed[0] = 7;
    output_len = sizeof(output);
    if (microz_decompress_block(compressed, compressed_len, output, &output_len) != MICROZ_ERR_DECOMPRESS) return 1;
    phase = 0;
    free(compressed);
    if (releases != 4) return 1;
    return failures ? 1 : 0;
}

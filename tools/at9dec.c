/* Decodes a raw ATRAC9 stream (an FSB5 sample's data) to 16-bit PCM.
 *   at9dec CONFIG_HEX FRAMES < stream > pcm
 * CONFIG_HEX: the 4 config bytes (fe....), FRAMES: samples per channel to
 * write; the output is interleaved s16le, cut or padded with silence to FRAMES.
 * Built by tools/build-at9dec.sh against LibAtrac9 (MIT). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libatrac9.h"

int main(int argc, char** argv) {
    if (argc != 3 || strlen(argv[1]) != 8) {
        fprintf(stderr, "usage: at9dec CONFIG_HEX FRAMES < stream > pcm\n");
        return 2;
    }
    unsigned char config[4];
    for (int i = 0; i < 4; i++) {
        unsigned v;
        sscanf(argv[1] + 2 * i, "%2x", &v);
        config[i] = (unsigned char)v;
    }
    long want = atol(argv[2]);

    size_t cap = 1 << 20, len = 0;
    unsigned char* in = malloc(cap);
    for (size_t n; (n = fread(in + len, 1, cap - len, stdin)) > 0;) {
        len += n;
        if (len == cap) in = realloc(in, cap *= 2);
    }

    void* h = Atrac9GetHandle();
    if (Atrac9InitDecoder(h, config) != 0) {
        fprintf(stderr, "at9dec: bad config %s\n", argv[1]);
        return 1;
    }
    Atrac9CodecInfo info;
    Atrac9GetCodecInfo(h, &info);
    short* pcm = malloc(sizeof(short) * info.frameSamples * info.channels);
    long written = 0;
    size_t pos = 0;
    /* A sample's data ends in zeros up to 32-byte alignment: the first frame
     * that does not decode there is the end of the stream. */
    int end = 0;
    while (!end && written < want && pos + info.superframeSize <= len) {
        const size_t superframe = pos;
        for (int f = 0; f < info.framesInSuperframe && written < want; f++) {
            int used = 0;
            if (Atrac9Decode(h, in + pos, pcm, &used) != 0) {
                if (len - pos > 32) fprintf(stderr, "at9dec: decode error at byte %zu of %zu\n", pos, len);
                end = 1;
                break;
            }
            pos += used;
            long n = info.frameSamples;
            if (n > want - written) n = want - written;
            fwrite(pcm, sizeof(short) * info.channels, n, stdout);
            written += n;
        }
        pos = superframe + info.superframeSize;  /* past the superframe's own padding */
    }
    memset(pcm, 0, sizeof(short) * info.frameSamples * info.channels);
    while (written < want) {
        long n = info.frameSamples;
        if (n > want - written) n = want - written;
        fwrite(pcm, sizeof(short) * info.channels, n, stdout);
        written += n;
    }
    Atrac9ReleaseHandle(h);
    return 0;
}

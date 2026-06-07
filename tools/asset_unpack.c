#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "codec_lz4.h"

static int read_file(const char *path, unsigned char **out, size_t *out_sz)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "asset_unpack: cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "asset_unpack: cannot seek '%s'\n", path);
        fclose(f);
        return 1;
    }

    const long sz = ftell(f);
    if (sz < 0) {
        fprintf(stderr, "asset_unpack: cannot size '%s'\n", path);
        fclose(f);
        return 1;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "asset_unpack: cannot rewind '%s'\n", path);
        fclose(f);
        return 1;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) {
        fprintf(stderr, "asset_unpack: out of memory\n");
        fclose(f);
        return 1;
    }

    const size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (nread != (size_t)sz) {
        fprintf(stderr, "asset_unpack: short read on '%s'\n", path);
        free(buf);
        return 1;
    }

    *out = buf;
    *out_sz = (size_t)sz;
    return 0;
}

static int write_file(const char *path, const unsigned char *data, size_t data_sz)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "asset_unpack: cannot write '%s': %s\n", path, strerror(errno));
        return 1;
    }

    const size_t nwritten = fwrite(data, 1, data_sz, f);
    fclose(f);
    if (nwritten != data_sz) {
        fprintf(stderr, "asset_unpack: short write on '%s'\n", path);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <input.lz4> <uncompressed-size> <output.tar>\n", argv[0]);
        return 1;
    }

    char *end = NULL;
    const unsigned long uncomp_ul = strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || uncomp_ul == 0) {
        fprintf(stderr, "asset_unpack: invalid uncompressed size '%s'\n", argv[2]);
        return 1;
    }

    unsigned char *compressed = NULL;
    size_t compressed_sz = 0;
    if (read_file(argv[1], &compressed, &compressed_sz) != 0) {
        return 1;
    }

    unsigned char *uncompressed = (unsigned char *)malloc((size_t)uncomp_ul);
    if (!uncompressed) {
        fprintf(stderr, "asset_unpack: out of memory\n");
        free(compressed);
        return 1;
    }

    const int decoded = lz4_decompress((const char *)compressed,
                                       (char *)uncompressed,
                                       (int)compressed_sz,
                                       (int)uncomp_ul);
    free(compressed);

    if (decoded != (int)uncomp_ul) {
        fprintf(stderr, "asset_unpack: decompression failed (got %d, expected %lu)\n",
                decoded, uncomp_ul);
        free(uncompressed);
        return 1;
    }

    if (write_file(argv[3], uncompressed, (size_t)uncomp_ul) != 0) {
        free(uncompressed);
        return 1;
    }

    free(uncompressed);
    return 0;
}

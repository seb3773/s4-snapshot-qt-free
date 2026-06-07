#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lz4_compress_wrapper.h"

static int read_file(const char *path, unsigned char **out, size_t *out_sz)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "asset_pack: cannot open '%s': %s\n", path, strerror(errno));
        return 1;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "asset_pack: cannot seek '%s'\n", path);
        fclose(f);
        return 1;
    }

    const long sz = ftell(f);
    if (sz < 0) {
        fprintf(stderr, "asset_pack: cannot size '%s'\n", path);
        fclose(f);
        return 1;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "asset_pack: cannot rewind '%s'\n", path);
        fclose(f);
        return 1;
    }

    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    if (!buf) {
        fprintf(stderr, "asset_pack: out of memory\n");
        fclose(f);
        return 1;
    }

    const size_t nread = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (nread != (size_t)sz) {
        fprintf(stderr, "asset_pack: short read on '%s'\n", path);
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
        fprintf(stderr, "asset_pack: cannot write '%s': %s\n", path, strerror(errno));
        return 1;
    }

    const size_t nwritten = fwrite(data, 1, data_sz, f);
    fclose(f);
    if (nwritten != data_sz) {
        fprintf(stderr, "asset_pack: short write on '%s'\n", path);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <input> <output.lz4> <sizes-file>\n", argv[0]);
        return 1;
    }

    unsigned char *input = NULL;
    size_t input_sz = 0;
    if (read_file(argv[1], &input, &input_sz) != 0) {
        return 1;
    }

    unsigned char *compressed = NULL;
    size_t compressed_sz = 0;
    if (lz4_compress_c(input, input_sz, &compressed, &compressed_sz) != 0) {
        fprintf(stderr, "asset_pack: compression failed\n");
        free(input);
        return 1;
    }

    if (lz4_verify_c(compressed, compressed_sz, input, input_sz) != 0) {
        fprintf(stderr, "asset_pack: compression self-verify failed\n");
        free(input);
        free(compressed);
        return 1;
    }

    if (write_file(argv[2], compressed, compressed_sz) != 0) {
        free(input);
        free(compressed);
        return 1;
    }

    FILE *sizes = fopen(argv[3], "w");
    if (!sizes) {
        fprintf(stderr, "asset_pack: cannot write sizes '%s': %s\n", argv[3], strerror(errno));
        free(input);
        free(compressed);
        return 1;
    }

    fprintf(sizes, "%zu %zu\n", input_sz, compressed_sz);
    fclose(sizes);

    free(input);
    free(compressed);
    return 0;
}

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mtar_memory.h"

typedef struct {
    const unsigned char *data;
    unsigned size;
    unsigned pos;
} mtar_mem_stream_t;

static int mem_read(mtar_t *tar, void *data, unsigned size)
{
    mtar_mem_stream_t *stream = (mtar_mem_stream_t *)tar->stream;
    if (stream->pos + size > stream->size) {
        return MTAR_EREADFAIL;
    }
    memcpy(data, stream->data + stream->pos, size);
    stream->pos += size;
    return MTAR_ESUCCESS;
}

static int mem_write(mtar_t *tar, const void *data, unsigned size)
{
    (void)tar;
    (void)data;
    (void)size;
    return MTAR_EWRITEFAIL;
}

static int mem_seek(mtar_t *tar, unsigned pos)
{
    mtar_mem_stream_t *stream = (mtar_mem_stream_t *)tar->stream;
    if (pos > stream->size) {
        return MTAR_ESEEKFAIL;
    }
    stream->pos = pos;
    return MTAR_ESUCCESS;
}

static int mem_close(mtar_t *tar)
{
    free(tar->stream);
    tar->stream = NULL;
    return MTAR_ESUCCESS;
}

int mtar_open_memory(mtar_t *tar, const void *data, unsigned size)
{
    mtar_mem_stream_t *stream = (mtar_mem_stream_t *)calloc(1, sizeof(*stream));
    if (!stream) {
        return MTAR_EFAILURE;
    }

    memset(tar, 0, sizeof(*tar));
    tar->read = mem_read;
    tar->write = mem_write;
    tar->seek = mem_seek;
    tar->close = mem_close;
    tar->stream = stream;
    stream->data = (const unsigned char *)data;
    stream->size = size;
    stream->pos = 0;
    return MTAR_ESUCCESS;
}

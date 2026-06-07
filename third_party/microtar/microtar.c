/*
 * Copyright (c) 2017 rxi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "microtar.h"

typedef struct {
    char name[100];
    char mode[8];
    char owner[8];
    char group[8];
    char size[12];
    char mtime[12];
    char checksum[8];
    char type;
    char linkname[100];
    char _padding[255];
} mtar_raw_header_t;

static unsigned round_up(unsigned n, unsigned incr)
{
    return n + (incr - n % incr) % incr;
}

static unsigned checksum(const mtar_raw_header_t *rh)
{
    unsigned i;
    unsigned char *p = (unsigned char *)rh;
    unsigned res = 256;
    for (i = 0; i < offsetof(mtar_raw_header_t, checksum); i++) {
        res += p[i];
    }
    for (i = offsetof(mtar_raw_header_t, type); i < sizeof(*rh); i++) {
        res += p[i];
    }
    return res;
}

static int tread(mtar_t *tar, void *data, unsigned size)
{
    int err = tar->read(tar, data, size);
    tar->pos += size;
    return err;
}

static int twrite(mtar_t *tar, const void *data, unsigned size)
{
    int err = tar->write(tar, data, size);
    tar->pos += size;
    return err;
}

static int write_null_bytes(mtar_t *tar, int n)
{
    int i, err;
    char nul = '\0';
    for (i = 0; i < n; i++) {
        err = twrite(tar, &nul, 1);
        if (err) {
            return err;
        }
    }
    return MTAR_ESUCCESS;
}

static int raw_to_header(mtar_header_t *h, const mtar_raw_header_t *rh)
{
    unsigned chksum1, chksum2;

    if (*rh->checksum == '\0') {
        return MTAR_ENULLRECORD;
    }

    chksum1 = checksum(rh);
    sscanf(rh->checksum, "%o", &chksum2);
    if (chksum1 != chksum2) {
        return MTAR_EBADCHKSUM;
    }

    sscanf(rh->mode, "%o", &h->mode);
    sscanf(rh->owner, "%o", &h->owner);
    sscanf(rh->size, "%o", &h->size);
    sscanf(rh->mtime, "%o", &h->mtime);
    h->type = rh->type;
    strcpy(h->name, rh->name);
    strcpy(h->linkname, rh->linkname);

    return MTAR_ESUCCESS;
}

static int header_to_raw(mtar_raw_header_t *rh, const mtar_header_t *h)
{
    unsigned chksum;

    memset(rh, 0, sizeof(*rh));
    sprintf(rh->mode, "%o", h->mode);
    sprintf(rh->owner, "%o", h->owner);
    sprintf(rh->size, "%o", h->size);
    sprintf(rh->mtime, "%o", h->mtime);
    rh->type = h->type ? h->type : MTAR_TREG;
    strcpy(rh->name, h->name);
    strcpy(rh->linkname, h->linkname);

    chksum = checksum(rh);
    sprintf(rh->checksum, "%06o", chksum);
    rh->checksum[7] = ' ';

    return MTAR_ESUCCESS;
}

const char *mtar_strerror(int err)
{
    switch (err) {
    case MTAR_ESUCCESS:
        return "success";
    case MTAR_EFAILURE:
        return "failure";
    case MTAR_EOPENFAIL:
        return "could not open";
    case MTAR_EREADFAIL:
        return "could not read";
    case MTAR_EWRITEFAIL:
        return "could not write";
    case MTAR_ESEEKFAIL:
        return "could not seek";
    case MTAR_EBADCHKSUM:
        return "bad checksum";
    case MTAR_ENULLRECORD:
        return "null record";
    case MTAR_ENOTFOUND:
        return "file not found";
    }
    return "unknown error";
}

static int file_write(mtar_t *tar, const void *data, unsigned size)
{
    unsigned res = fwrite(data, 1, size, tar->stream);
    return (res == size) ? MTAR_ESUCCESS : MTAR_EWRITEFAIL;
}

static int file_read(mtar_t *tar, void *data, unsigned size)
{
    unsigned res = fread(data, 1, size, tar->stream);
    return (res == size) ? MTAR_ESUCCESS : MTAR_EREADFAIL;
}

static int file_seek(mtar_t *tar, unsigned offset)
{
    int res = fseek(tar->stream, offset, SEEK_SET);
    return (res == 0) ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

static int file_close(mtar_t *tar)
{
    fclose(tar->stream);
    return MTAR_ESUCCESS;
}

int mtar_open(mtar_t *tar, const char *filename, const char *mode)
{
    int err;
    mtar_header_t h;

    memset(tar, 0, sizeof(*tar));
    tar->write = file_write;
    tar->read = file_read;
    tar->seek = file_seek;
    tar->close = file_close;

    if (strchr(mode, 'r')) {
        mode = "rb";
    }
    if (strchr(mode, 'w')) {
        mode = "wb";
    }
    if (strchr(mode, 'a')) {
        mode = "ab";
    }

    tar->stream = fopen(filename, mode);
    if (!tar->stream) {
        return MTAR_EOPENFAIL;
    }

    if (*mode == 'r') {
        err = mtar_read_header(tar, &h);
        if (err != MTAR_ESUCCESS) {
            mtar_close(tar);
            return err;
        }
    }

    return MTAR_ESUCCESS;
}

int mtar_close(mtar_t *tar)
{
    return tar->close(tar);
}

int mtar_seek(mtar_t *tar, unsigned pos)
{
    int err = tar->seek(tar, pos);
    tar->pos = pos;
    return err;
}

int mtar_rewind(mtar_t *tar)
{
    tar->remaining_data = 0;
    tar->last_header = 0;
    return mtar_seek(tar, 0);
}

int mtar_next(mtar_t *tar)
{
    int err, n;
    mtar_header_t h;
    err = mtar_read_header(tar, &h);
    if (err) {
        return err;
    }
    n = round_up(h.size, 512) + sizeof(mtar_raw_header_t);
    return mtar_seek(tar, tar->pos + n);
}

int mtar_find(mtar_t *tar, const char *name, mtar_header_t *h)
{
    int err;
    mtar_header_t header;
    err = mtar_rewind(tar);
    if (err) {
        return err;
    }
    while ((err = mtar_read_header(tar, &header)) == MTAR_ESUCCESS) {
        if (!strcmp(header.name, name)) {
            if (h) {
                *h = header;
            }
            return MTAR_ESUCCESS;
        }
        mtar_next(tar);
    }
    if (err == MTAR_ENULLRECORD) {
        err = MTAR_ENOTFOUND;
    }
    return err;
}

int mtar_read_header(mtar_t *tar, mtar_header_t *h)
{
    int err;
    mtar_raw_header_t rh;
    tar->last_header = tar->pos;
    err = tread(tar, &rh, sizeof(rh));
    if (err) {
        return err;
    }
    err = mtar_seek(tar, tar->last_header);
    if (err) {
        return err;
    }
    return raw_to_header(h, &rh);
}

int mtar_read_data(mtar_t *tar, void *ptr, unsigned size)
{
    int err;
    if (tar->remaining_data == 0) {
        mtar_header_t h;
        err = mtar_read_header(tar, &h);
        if (err) {
            return err;
        }
        err = mtar_seek(tar, tar->pos + sizeof(mtar_raw_header_t));
        if (err) {
            return err;
        }
        tar->remaining_data = h.size;
    }
    err = tread(tar, ptr, size);
    if (err) {
        return err;
    }
    tar->remaining_data -= size;
    if (tar->remaining_data == 0) {
        return mtar_seek(tar, tar->last_header);
    }
    return MTAR_ESUCCESS;
}

int mtar_write_header(mtar_t *tar, const mtar_header_t *h)
{
    mtar_raw_header_t rh;
    header_to_raw(&rh, h);
    tar->remaining_data = h->size;
    return twrite(tar, &rh, sizeof(rh));
}

int mtar_write_file_header(mtar_t *tar, const char *name, unsigned size)
{
    mtar_header_t h;
    memset(&h, 0, sizeof(h));
    strcpy(h.name, name);
    h.size = size;
    h.type = MTAR_TREG;
    h.mode = 0664;
    return mtar_write_header(tar, &h);
}

int mtar_write_dir_header(mtar_t *tar, const char *name)
{
    mtar_header_t h;
    memset(&h, 0, sizeof(h));
    strcpy(h.name, name);
    h.type = MTAR_TDIR;
    h.mode = 0775;
    return mtar_write_header(tar, &h);
}

int mtar_write_data(mtar_t *tar, const void *data, unsigned size)
{
    int err;
    err = twrite(tar, data, size);
    if (err) {
        return err;
    }
    tar->remaining_data -= size;
    if (tar->remaining_data == 0) {
        return write_null_bytes(tar, round_up(tar->pos, 512) - tar->pos);
    }
    return MTAR_ESUCCESS;
}

int mtar_finalize(mtar_t *tar)
{
    return write_null_bytes(tar, sizeof(mtar_raw_header_t) * 2);
}

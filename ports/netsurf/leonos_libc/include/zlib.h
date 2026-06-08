#ifndef LEONOS_LIBC_ZLIB_H
#define LEONOS_LIBC_ZLIB_H

#include <stddef.h>
#include <stdint.h>

#define Z_OK 0
#define Z_STREAM_END 1
#define Z_NO_FLUSH 0
#define Z_DATA_ERROR (-3)
#define Z_NULL ((void *) 0)
#define MAX_WBITS 15

typedef unsigned char Bytef;
typedef unsigned int uInt;
typedef unsigned long uLong;
typedef void *voidpf;
typedef struct leonos_gz_file *gzFile;

typedef struct z_stream_s {
    Bytef *next_in;
    uInt avail_in;
    Bytef *next_out;
    uInt avail_out;
    voidpf zalloc;
    voidpf zfree;
    voidpf opaque;
} z_stream;

int inflateInit2(z_stream *strm, int windowBits);
int inflate(z_stream *strm, int flush);
int inflateEnd(z_stream *strm);
uLong crc32(uLong crc, const Bytef *buf, uInt len);
gzFile gzopen(const char *path, const char *mode);
char *gzgets(gzFile file, char *buf, int len);
int gzclose(gzFile file);

#endif

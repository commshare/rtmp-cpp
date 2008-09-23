/*
 * Buffered I/O for ffmpeg system
 * Copyright (c) 2000,2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/crc.h"
#include "avformat.h"
#include "avio.h"
#include <stdarg.h>

//Fernando: 20080908
//#undef printf
//#define LogStr(str)  printf ( "************************** %s: %s - %s-%d **************************\n", __func__, str, __FILE__, __LINE__)
#define LogStr(str) av_log(NULL, AV_LOG_ERROR, "************************** %s: %s - %s-%d **************************\n", __func__, str, __FILE__, __LINE__);

#define IO_BUFFER_SIZE 32768

static void fill_buffer( ByteIOContext *s );

int init_put_byte( ByteIOContext *s, unsigned char *buffer, int buffer_size, int write_flag, void *opaque, int(*read_packet)( void *opaque, uint8_t *buf, int buf_size ), int(*write_packet)( void *opaque, uint8_t *buf, int buf_size ), offset_t(*seek)( void *opaque, offset_t offset, int whence ) )
{
    //LogStr ("Init");

    s->buffer = buffer;
    s->buffer_size = buffer_size;
    s->buf_ptr = buffer;
    url_resetbuf(s, write_flag ? URL_WRONLY : URL_RDONLY);
    s->opaque = opaque;
    s->write_packet = write_packet;
    s->read_packet = read_packet;
    s->seek = seek;
    s->pos = 0;
    s->must_flush = 0;
    s->eof_reached = 0;
    s->error = 0;
    s->is_streamed = 0;
    s->max_packet_size = 0;
    s->update_checksum = NULL;

    if (!read_packet && !write_flag)
    {
        s->pos = buffer_size;
        s->buf_end = s->buffer + buffer_size;
    }

    s->read_pause = NULL;
    s->read_seek = NULL;

    //LogStr ("Exit");

    return 0;
}

ByteIOContext *av_alloc_put_byte( unsigned char *buffer, int buffer_size, int write_flag, void *opaque, int(*read_packet)( void *opaque, uint8_t *buf, int buf_size ), int(*write_packet)( void *opaque, uint8_t *buf, int buf_size ), offset_t(*seek)( void *opaque, offset_t offset, int whence ) )
{
    //LogStr ("Init");

    ByteIOContext *s = av_mallocz(sizeof(ByteIOContext));
    init_put_byte(s, buffer, buffer_size, write_flag, opaque, read_packet, write_packet, seek);

    //LogStr ("Exit");


    return s;
}

static void flush_buffer( ByteIOContext *s )
{
    //LogStr ("Init");

    if (s->buf_ptr > s->buffer)
    {
        if (s->write_packet && !s->error)
        {
            int ret = s->write_packet(s->opaque, s->buffer, s->buf_ptr - s->buffer);
            if (ret < 0)
            {
                s->error = ret;
            }
        }
        if (s->update_checksum)
        {
            s->checksum = s->update_checksum(s->checksum, s->checksum_ptr, s->buf_ptr - s->checksum_ptr);
            s->checksum_ptr = s->buffer;
        }
        s->pos += s->buf_ptr - s->buffer;
    }
    s->buf_ptr = s->buffer;

    //LogStr ("Exit");


}

void put_byte( ByteIOContext *s, int b )
{
    ////LogStr ("Init");

    *(s->buf_ptr)++ = b;

    if (s->buf_ptr >= s->buf_end)
    {
        flush_buffer(s);
    }

    ////LogStr ("Exit");


}

void put_buffer( ByteIOContext *s, const unsigned char *buf, int size )
{
    //LogStr ("Init");

    int len;

    while (size > 0)
    {
        len = (s->buf_end - s->buf_ptr);

        if (len > size)
        {
            len = size;
        }
        memcpy(s->buf_ptr, buf, len);
        s->buf_ptr += len;

        if (s->buf_ptr >= s->buf_end)
        {
            flush_buffer(s);
        }

        buf += len;
        size -= len;
    }

    //LogStr ("Exit");


}

void put_flush_packet( ByteIOContext *s )
{
    //LogStr ("Init");

    flush_buffer(s);
    s->must_flush = 0;

    //LogStr ("Exit");


}

offset_t url_fseek( ByteIOContext *s, offset_t offset, int whence )
{
    //LogStr ("Init");

    offset_t offset1;
    offset_t pos;

    if (!s)
    {
        //LogStr ("Exit");


        return AVERROR(EINVAL);
    }

    pos = s->pos - (s->write_flag ? 0 : (s->buf_end - s->buffer));

    if (whence != SEEK_CUR && whence != SEEK_SET)
    {
        //LogStr ("Exit");


        return AVERROR(EINVAL);
    }

    if (whence == SEEK_CUR)
    {
        offset1 = pos + (s->buf_ptr - s->buffer);
        if (offset == 0)
        {
            //LogStr ("Exit");


            return offset1;
        }
        offset += offset1;
    }

    offset1 = offset - pos;
    if (!s->must_flush && offset1 >= 0 && offset1 < (s->buf_end - s->buffer))
    {
        /* can do the seek inside the buffer */
        s->buf_ptr = s->buffer + offset1;
    }
    else if (s->is_streamed && !s->write_flag && offset1 >= 0 && offset1 < (s->buf_end - s->buffer) + (1 << 16))
    {
        while (s->pos < offset && !s->eof_reached)
        {
            fill_buffer(s);
        }

        if (s->eof_reached)
        {
            //LogStr ("Exit");


            return AVERROR(EPIPE);
        }
        s->buf_ptr = s->buf_end + offset - s->pos;
    }
    else
    {
        offset_t res = AVERROR(EPIPE);

#if defined(CONFIG_MUXERS) || defined(CONFIG_NETWORK)
        if (s->write_flag)
        {
            flush_buffer(s);
            s->must_flush = 1;
        }
#endif /* defined(CONFIG_MUXERS) || defined(CONFIG_NETWORK) */
        if (!s->seek || (res = s->seek(s->opaque, offset, SEEK_SET)) < 0)
        {
            //LogStr ("Exit");


            return res;
        }

        if (!s->write_flag)
        {
            s->buf_end = s->buffer;
        }
        s->buf_ptr = s->buffer;
        s->pos = offset;
    }

    s->eof_reached = 0;
    //LogStr ("Exit");


    return offset;
}

void url_fskip( ByteIOContext *s, offset_t offset )
{
    //LogStr ("Init");

    url_fseek(s, offset, SEEK_CUR);

    //LogStr ("Exit");


}

offset_t url_ftell( ByteIOContext *s )
{
    //LogStr ("Init");

    //LogStr ("Exit");


    return url_fseek(s, 0, SEEK_CUR);
}

offset_t url_fsize( ByteIOContext *s )
{
    //LogStr ("Init");


    offset_t size;

    if (!s)
    {
        //LogStr ("Exit");


        return AVERROR(EINVAL);
    }

    if (!s->seek)
    {
        //LogStr ("Exit");


        return AVERROR(EPIPE);
    }

    size = s->seek(s->opaque, 0, AVSEEK_SIZE);
    if (size < 0)
    {
        if ((size = s->seek(s->opaque, -1, SEEK_END)) < 0)
        {
            //LogStr ("Exit");


            return size;
        }
        size++;
        s->seek(s->opaque, s->pos, SEEK_SET);
    }

    //LogStr ("Exit");


    return size;
}

int url_feof( ByteIOContext *s )
{
    //LogStr ("Init");


    if (!s)
    {
        //LogStr ("Exit");


        return 0;
    }

    //LogStr ("Exit");
    return s->eof_reached;
}

int url_ferror( ByteIOContext *s )
{
    //LogStr ("Init");


    if (!s)
    {
        //LogStr ("Exit");


        return 0;
    }
    //LogStr ("Exit");


    return s->error;
}

void put_le32( ByteIOContext *s, unsigned int val )
{
    //LogStr ("Init");


    put_byte(s, val);
    put_byte(s, val >> 8);
    put_byte(s, val >> 16);
    put_byte(s, val >> 24);
    //LogStr ("Exit");


}

void put_be32( ByteIOContext *s, unsigned int val )
{
    //LogStr ("Init");


    put_byte(s, val >> 24);
    put_byte(s, val >> 16);
    put_byte(s, val >> 8);
    put_byte(s, val);
    //LogStr ("Exit");


}

void put_strz( ByteIOContext *s, const char *str )
{
    //LogStr ("Init");


    if (str)
    {
        put_buffer(s, (const unsigned char *) str, strlen(str) + 1);
    }
    else
    {
        put_byte(s, 0);
    }
    //LogStr ("Exit");


}

void put_le64( ByteIOContext *s, uint64_t val )
{
    //LogStr ("Init");


    put_le32(s, (uint32_t) (val & 0xffffffff));
    put_le32(s, (uint32_t) (val >> 32));

    //LogStr ("Exit");


}

void put_be64( ByteIOContext *s, uint64_t val )
{
    //LogStr ("Init");


    put_be32(s, (uint32_t) (val >> 32));
    put_be32(s, (uint32_t) (val & 0xffffffff));

    //LogStr ("Exit");


}

void put_le16( ByteIOContext *s, unsigned int val )
{
    //LogStr ("Init");


    put_byte(s, val);
    put_byte(s, val >> 8);

    //LogStr ("Exit");


}

void put_be16( ByteIOContext *s, unsigned int val )
{
    //LogStr ("Init");


    put_byte(s, val >> 8);
    put_byte(s, val);

    //LogStr ("Exit");


}

void put_le24( ByteIOContext *s, unsigned int val )
{
    //LogStr ("Init");

    put_le16(s, val & 0xffff);
    put_byte(s, val >> 16);

    //LogStr ("Exit");


}

void put_be24( ByteIOContext *s, unsigned int val )
{
    //LogStr ("Init");

    put_be16(s, val >> 8);
    put_byte(s, val);

    //LogStr ("Exit");


}

void put_tag( ByteIOContext *s, const char *tag )
{
    //LogStr ("Init");


    while (*tag)
    {
        put_byte(s, *tag++);
    }

    //LogStr ("Exit");


}

/* Input stream */

static void fill_buffer( ByteIOContext *s )
{
    //LogStr ("Init");

    int len = 0;

    /* no need to do anything if EOF already reached */
    if (s->eof_reached)
    {
        //LogStr ("Exit");
        return;
    }

    if (s->update_checksum)
    {
        //LogStr ("-*-*-*-*-* 1 -*-*-*-*-*");
        if (s->buf_end > s->checksum_ptr)
        {
            //LogStr ("-*-*-*-*-* 2 -*-*-*-*-*");
            s->checksum = s->update_checksum(s->checksum, s->checksum_ptr, s->buf_end - s->checksum_ptr);
            //LogStr ("-*-*-*-*-* 2 -*-*-*-*-*");
        }
        //LogStr ("-*-*-*-*-* 3 -*-*-*-*-*");

        s->checksum_ptr = s->buffer;
    }

    //LogStr ("-*-*-*-*-* 4 -*-*-*-*-*");

    if (s->read_packet)
    {
        //LogStr ("-*-*-*-*-* 5 -*-*-*-*-*");

        len = s->read_packet(s->opaque, s->buffer, s->buffer_size);
    }

    //LogStr ("-*-*-*-*-* 6 -*-*-*-*-*");

    if (len <= 0)
    {
        //LogStr ("-*-*-*-*-* 7 -*-*-*-*-*");

        /* do not modify buffer if EOF reached so that a seek back can
         be done without rereading data */
        s->eof_reached = 1;
        if (len < 0)
        {

            //LogStr ("-*-*-*-*-* 8 -*-*-*-*-*");

            s->error = len;
        }

        //LogStr ("-*-*-*-*-* 9 -*-*-*-*-*");

    }
    else
    {
        //LogStr ("-*-*-*-*-* 10 -*-*-*-*-*");

        s->pos += len;
        s->buf_ptr = s->buffer;
        s->buf_end = s->buffer + len;
    }

    //LogStr ("-*-*-*-*-* 11 -*-*-*-*-*");


    //LogStr ("Exit");


}

unsigned long ff_crc04C11DB7_update( unsigned long checksum, const uint8_t *buf, unsigned int len )
{
    //LogStr ("Init");

    //LogStr ("Exit");


    return av_crc(av_crc_get_table(AV_CRC_32_IEEE), checksum, buf, len);
}

unsigned long get_checksum( ByteIOContext *s )
{
    //LogStr ("Init");


    s->checksum = s->update_checksum(s->checksum, s->checksum_ptr, s->buf_ptr - s->checksum_ptr);
    s->update_checksum = NULL;

    //LogStr ("Exit");


    return s->checksum;
}

void init_checksum( ByteIOContext *s, unsigned long(*update_checksum)( unsigned long c, const uint8_t *p, unsigned int len ), unsigned long checksum )
{
    //LogStr ("Init");


    s->update_checksum = update_checksum;
    if (s->update_checksum)
    {
        s->checksum = checksum;
        s->checksum_ptr = s->buf_ptr;
    }
}

/* XXX: put an inline version */
int get_byte( ByteIOContext *s )
{
    //LogStr ("Init");


    if (s->buf_ptr < s->buf_end)
    {
        //LogStr ("Exit");


        return *s->buf_ptr++;
    }
    else
    {
        fill_buffer(s);
        if (s->buf_ptr < s->buf_end)
        {
            //LogStr ("Exit");


            return *s->buf_ptr++;
        }
        else
        {
            //LogStr ("Exit");


            return 0;
        }
    }

    //LogStr ("Exit");


}

int url_fgetc( ByteIOContext *s )
{
    //LogStr ("Init");


    if (s->buf_ptr < s->buf_end)
    {
        //LogStr ("Exit");


        return *s->buf_ptr++;
    }
    else
    {
        fill_buffer(s);
        if (s->buf_ptr < s->buf_end)
        {
            //LogStr ("Exit");


            return *s->buf_ptr++;
        }
        else
        {
            //LogStr ("Exit");


            return URL_EOF;
        }
    }

    //LogStr ("Exit");


}

int get_buffer( ByteIOContext *s, unsigned char *buf, int size )
{
    //LogStr ("Init");


    int len, size1;

    size1 = size;
    while (size > 0)
    {
        len = s->buf_end - s->buf_ptr;
        if (len > size)
        {
            len = size;
        }

        if (len == 0)
        {
            if (size > s->buffer_size && !s->update_checksum)
            {
                if (s->read_packet)
                {
                    len = s->read_packet(s->opaque, buf, size);
                }

                if (len <= 0)
                {
                    /* do not modify buffer if EOF reached so that a seek back can
                     be done without rereading data */
                    s->eof_reached = 1;
                    if (len < 0)
                    {
                        s->error = len;
                    }

                    break;
                }
                else
                {
                    s->pos += len;
                    size -= len;
                    buf += len;
                    s->buf_ptr = s->buffer;
                    s->buf_end = s->buffer/* + len*/;
                }
            }
            else
            {
                fill_buffer(s);
                len = s->buf_end - s->buf_ptr;
                if (len == 0)
                {
                    break;
                }
            }
        }
        else
        {
            memcpy(buf, s->buf_ptr, len);
            buf += len;
            s->buf_ptr += len;
            size -= len;
        }
    }

    //LogStr ("Exit");


    return size1 - size;
}

int get_partial_buffer( ByteIOContext *s, unsigned char *buf, int size )
{
    //LogStr ("Init");


    int len;

    if (size < 0)
    {
        //LogStr ("Exit");


        return -1;
    }

    len = s->buf_end - s->buf_ptr;
    if (len == 0)
    {
        fill_buffer(s);
        len = s->buf_end - s->buf_ptr;
    }

    if (len > size)
    {
        len = size;
    }

    memcpy(buf, s->buf_ptr, len);
    s->buf_ptr += len;

    //LogStr ("Exit");


    return len;
}

unsigned int get_le16( ByteIOContext *s )
{
    //LogStr ("Init");


    unsigned int val;
    val = get_byte(s);
    val |= get_byte(s) << 8;

    //LogStr ("Exit");


    return val;
}

unsigned int get_le24( ByteIOContext *s )
{
    //LogStr ("Init");


    unsigned int val;
    val = get_le16(s);
    val |= get_byte(s) << 16;

    //LogStr ("Exit");


    return val;
}

unsigned int get_le32( ByteIOContext *s )
{
    //LogStr ("Init");


    unsigned int val;
    val = get_le16(s);
    val |= get_le16(s) << 16;

    //LogStr ("Exit");


    return val;
}

uint64_t get_le64( ByteIOContext *s )
{
    //LogStr ("Init");


    uint64_t val;
    val = (uint64_t) get_le32(s);
    val |= (uint64_t) get_le32(s) << 32;

    //LogStr ("Exit");


    return val;
}

unsigned int get_be16( ByteIOContext *s )
{
    //LogStr ("Init");


    unsigned int val;
    val = get_byte(s) << 8;
    val |= get_byte(s);

    //LogStr ("Exit");


    return val;
}

unsigned int get_be24( ByteIOContext *s )
{
    //LogStr ("Init");


    unsigned int val;
    val = get_be16(s) << 8;
    val |= get_byte(s);

    //LogStr ("Exit");


    return val;
}
unsigned int get_be32( ByteIOContext *s )
{
    //LogStr ("Init");


    unsigned int val;
    val = get_be16(s) << 16;
    val |= get_be16(s);

    //LogStr ("Exit");


    return val;
}

char *get_strz( ByteIOContext *s, char *buf, int maxlen )
{
    //LogStr ("Init");


    int i = 0;
    char c;

    while ((c = get_byte(s)))
    {
        if (i < maxlen - 1)
        {
            buf[i++] = c;
        }
    }

    buf[i] = 0; /* Ensure null terminated, but may be truncated */

    //LogStr ("Exit");


    return buf;
}

uint64_t get_be64( ByteIOContext *s )
{
    //LogStr ("Init");


    uint64_t val;
    val = (uint64_t) get_be32(s) << 32;
    val |= (uint64_t) get_be32(s);

    //LogStr ("Exit");


    return val;
}

uint64_t ff_get_v( ByteIOContext *bc )
{
    //LogStr ("Init");


    uint64_t val = 0;
    int tmp;

    do
    {
        tmp = get_byte(bc);
        val = (val << 7) + (tmp & 127);
    } while (tmp & 128);

    //LogStr ("Exit");


    return val;
}

int url_fdopen( ByteIOContext **s, URLContext *h )
{
    //LogStr ("Init");


    uint8_t *buffer;
    int buffer_size, max_packet_size;

    max_packet_size = url_get_max_packet_size(h);
    if (max_packet_size)
    {
        buffer_size = max_packet_size; /* no need to bufferize more than one packet */
    }
    else
    {
        buffer_size = IO_BUFFER_SIZE;
    }

    buffer = av_malloc(buffer_size);

    if (!buffer)
    {

        //LogStr ("Exit");


        return AVERROR(ENOMEM);
    }

    *s = av_mallocz(sizeof(ByteIOContext));
    if (!*s)
    {
        av_free(buffer);
        //LogStr ("Exit");


        return AVERROR(ENOMEM);
    }

    if (init_put_byte(*s, buffer, buffer_size, (h->flags & URL_WRONLY || h->flags & URL_RDWR), h, url_read, url_write, url_seek) < 0)
    {
        av_free(buffer);
        av_freep(s);
        //LogStr ("Exit");


        return AVERROR(EIO);
    }

    (*s)->is_streamed = h->is_streamed;
    (*s)->max_packet_size = max_packet_size;

    if (h->prot)
    {
        (*s)->read_pause = (int(*)( void *, int )) h->prot->url_read_pause;
        (*s)->read_seek = (offset_t(*)( void *, int, int64_t, int )) h->prot->url_read_seek;
    }

    //LogStr ("Exit");


    return 0;
}

int url_setbufsize( ByteIOContext *s, int buf_size )
{
    //LogStr ("Init");


    uint8_t *buffer;
    buffer = av_malloc(buf_size);

    if (!buffer)
    {

        //LogStr ("Exit");


        return AVERROR(ENOMEM);
    }

    av_free(s->buffer);
    s->buffer = buffer;
    s->buffer_size = buf_size;
    s->buf_ptr = buffer;
    url_resetbuf(s, s->write_flag ? URL_WRONLY : URL_RDONLY);

    //LogStr ("Exit");


    return 0;
}

int url_resetbuf( ByteIOContext *s, int flags )
{
    //LogStr ("Init");


    URLContext *h = s->opaque;

    if ((flags & URL_RDWR) || (h && h->flags != flags && !h->flags & URL_RDWR))
    {

        //LogStr ("Exit");


        return AVERROR(EINVAL);
    }

    if (flags & URL_WRONLY)
    {
        s->buf_end = s->buffer + s->buffer_size;
        s->write_flag = 1;
    }
    else
    {
        s->buf_end = s->buffer;
        s->write_flag = 0;
    }

    //LogStr ("Exit");


    return 0;
}

int url_fopen( ByteIOContext **s, const char *filename, int flags )
{
    //LogStr ("Init");


    URLContext *h;
    int err;

    err = url_open(&h, filename, flags);
    if (err < 0)
    {
        //LogStr ("Exit");


        return err;
    }

    err = url_fdopen(s, h);
    if (err < 0)
    {
        url_close(h);
        //LogStr ("Exit");


        return err;
    }

    //LogStr ("Exit");


    return 0;
}

int url_fclose( ByteIOContext *s )
{
    //LogStr ("Init");


    URLContext *h = s->opaque;

    av_free(s->buffer);
    av_free(s);

    //LogStr ("Exit");


    return url_close(h);
}

URLContext *url_fileno( ByteIOContext *s )
{
    //LogStr ("Init");

    //LogStr ("Exit");


    return s->opaque;
}

#ifdef CONFIG_MUXERS
int url_fprintf(ByteIOContext *s, const char *fmt, ...)
{
    //LogStr ("Init");


    va_list ap;
    char buf[4096];
    int ret;

    va_start(ap, fmt);
    ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    put_buffer(s, buf, strlen(buf));

    //LogStr ("Exit");


    return ret;
}
#endif //CONFIG_MUXERS

char *url_fgets( ByteIOContext *s, char *buf, int buf_size )
{
    //LogStr ("Init");


    int c;
    char *q;

    c = url_fgetc(s);

    if (c == EOF)
    {
        //LogStr ("Exit");


        return NULL;
    }

    q = buf;
    for (;;)
    {
        if (c == EOF || c == '\n')
        {
            break;
        }

        if ((q - buf) < buf_size - 1)
        {
            *q++ = c;
        }

        c = url_fgetc(s);
    }

    if (buf_size > 0)
    {
        *q = '\0';
    }

    //LogStr ("Exit");


    return buf;
}

int url_fget_max_packet_size( ByteIOContext *s )
{
    //LogStr ("Init");

    //LogStr ("Exit");


    return s->max_packet_size;
}

int av_url_read_fpause( ByteIOContext *s, int pause )
{
    //LogStr ("Init");


    if (!s->read_pause)
    {
        //LogStr ("Exit");


        return AVERROR(ENOSYS);
    }
    //LogStr ("Exit");


    return s->read_pause(s->opaque, pause);
}

offset_t av_url_read_fseek( ByteIOContext *s, int stream_index, int64_t timestamp, int flags )
{
    //LogStr ("Init");


    URLContext *h = s->opaque;
    offset_t ret;

    if (!s->read_seek)
    {
        //LogStr ("Exit");


        return AVERROR(ENOSYS);
    }

    ret = s->read_seek(h, stream_index, timestamp, flags);
    if (ret >= 0)
    {
        s->buf_ptr = s->buf_end; // Flush buffer
        s->pos = s->seek(h, 0, SEEK_CUR);
    }

    //LogStr ("Exit");


    return ret;
}

/* url_open_dyn_buf and url_close_dyn_buf are used in rtp.c to send a response
 * back to the server even if CONFIG_MUXERS is not set. */
#if defined(CONFIG_MUXERS) || defined(CONFIG_NETWORK)
/* buffer handling */
int url_open_buf(ByteIOContext **s, uint8_t *buf, int buf_size, int flags)
{
    //LogStr ("Init");


    int ret;
    *s = av_mallocz(sizeof(ByteIOContext));
    if(!*s)
    {
        //LogStr ("Exit");


        return AVERROR(ENOMEM);
    }
    ret = init_put_byte(*s, buf, buf_size,
        (flags & URL_WRONLY || flags & URL_RDWR),
        NULL, NULL, NULL, NULL);

    if(ret != 0)
    {
        av_freep(s);
    }

    //LogStr ("Exit");


    return ret;
}

int url_close_buf(ByteIOContext *s)
{
    //LogStr ("Init");


    put_flush_packet(s);

    //LogStr ("Exit");


    return s->buf_ptr - s->buffer;
}

/* output in a dynamic buffer */

typedef struct DynBuffer
{
    int pos, size, allocated_size;
    uint8_t *buffer;
    int io_buffer_size;
    uint8_t io_buffer[1];
}DynBuffer;

static int dyn_buf_write(void *opaque, uint8_t *buf, int buf_size)
{
    //LogStr ("Init");


    DynBuffer *d = opaque;
    int new_size, new_allocated_size;

    /* reallocate buffer if needed */
    new_size = d->pos + buf_size;
    new_allocated_size = d->allocated_size;
    if(new_size < d->pos || new_size> INT_MAX/2)
    {
        //LogStr ("Exit");


        return -1;
    }
    while (new_size> new_allocated_size)
    {
        if (!new_allocated_size)
        new_allocated_size = new_size;
        else
        new_allocated_size += new_allocated_size / 2 + 1;
    }

    if (new_allocated_size> d->allocated_size)
    {
        d->buffer = av_realloc(d->buffer, new_allocated_size);
        if(d->buffer == NULL)
        {
            //LogStr ("Exit");


            return -1234;
        }
        d->allocated_size = new_allocated_size;
    }
    memcpy(d->buffer + d->pos, buf, buf_size);
    d->pos = new_size;
    if (d->pos> d->size)
    d->size = d->pos;


    //LogStr ("Exit");


    return buf_size;
}

static int dyn_packet_buf_write(void *opaque, uint8_t *buf, int buf_size)
{
    //LogStr ("Init");


    unsigned char buf1[4];
    int ret;

    /* packetized write: output the header */
    buf1[0] = (buf_size >> 24);
    buf1[1] = (buf_size >> 16);
    buf1[2] = (buf_size >> 8);
    buf1[3] = (buf_size);
    ret= dyn_buf_write(opaque, buf1, 4);
    if(ret < 0)
    {
        //LogStr ("Exit");


        return ret;
    }

    //LogStr ("Exit");


    /* then the data */
    return dyn_buf_write(opaque, buf, buf_size);
}

static offset_t dyn_buf_seek(void *opaque, offset_t offset, int whence)
{
    //LogStr ("Init");


    DynBuffer *d = opaque;

    if (whence == SEEK_CUR)
        offset += d->pos;
    else if (whence == SEEK_END)
        offset += d->size;

    if (offset < 0 || offset> 0x7fffffffLL)
    {
        //LogStr ("Exit");


        return -1;
    }

    d->pos = offset;
    //LogStr ("Exit");


    return 0;
}

static int url_open_dyn_buf_internal(ByteIOContext **s, int max_packet_size)
{
    //LogStr ("Init");


    DynBuffer *d;
    int io_buffer_size, ret;

    if (max_packet_size)
    io_buffer_size = max_packet_size;
    else
    io_buffer_size = 1024;

    if(sizeof(DynBuffer) + io_buffer_size < io_buffer_size)
    {
        //LogStr ("Exit");
        return -1;
    }

    d = av_malloc(sizeof(DynBuffer) + io_buffer_size);
    if (!d)
    {
        //LogStr ("Exit");
        return -1;
    }

    *s = av_mallocz(sizeof(ByteIOContext));
    if(!*s)
    {
        av_free(d);
        //LogStr ("Exit");
        return AVERROR(ENOMEM);
    }
    d->io_buffer_size = io_buffer_size;
    d->buffer = NULL;
    d->pos = 0;
    d->size = 0;
    d->allocated_size = 0;
    ret = init_put_byte(*s, d->io_buffer, io_buffer_size,
        1, d, NULL,
        max_packet_size ? dyn_packet_buf_write : dyn_buf_write,
        max_packet_size ? NULL : dyn_buf_seek);
    if (ret == 0)
    {
        (*s)->max_packet_size = max_packet_size;
    }
    else
    {
        av_free(d);
        av_freep(s);
    }

    //LogStr ("Exit");


    return ret;
}

int url_open_dyn_buf(ByteIOContext **s)
{
    //LogStr ("Init");

    //LogStr ("Exit");


    return url_open_dyn_buf_internal(s, 0);
}

int url_open_dyn_packet_buf(ByteIOContext **s, int max_packet_size)
{
    //LogStr ("Init");


    if (max_packet_size <= 0)
    {
        //LogStr ("Exit");
        return -1;
    }

    //LogStr ("Exit");

    return url_open_dyn_buf_internal(s, max_packet_size);
}

int url_close_dyn_buf(ByteIOContext *s, uint8_t **pbuffer)
{
    //LogStr ("Init");


    DynBuffer *d = s->opaque;
    int size;

    put_flush_packet(s);

    *pbuffer = d->buffer;
    size = d->size;
    av_free(d);
    av_free(s);

    //LogStr ("Exit");


    return size;
}
#endif /* CONFIG_MUXERS || CONFIG_NETWORK */
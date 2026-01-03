/*
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

#include "internal.h"
#include "avdevice.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "libavformat/avformat.h"
#include "libavformat/demux.h"

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#endif

int ff_alloc_input_device_context(AVFormatContext **avctx, const AVInputFormat *iformat, const char *format)
{
    AVFormatContext *s;
    int ret = 0;

    *avctx = NULL;
    if (!iformat && !format)
        return AVERROR(EINVAL);
    if (!(s = avformat_alloc_context()))
        return AVERROR(ENOMEM);

    if (!iformat)
        iformat = av_find_input_format(format);
    if (!iformat || !iformat->priv_class || !AV_IS_INPUT_DEVICE(iformat->priv_class->category)) {
        ret = AVERROR(EINVAL);
        goto error;
    }
    s->iformat = iformat;
    if (ffifmt(s->iformat)->priv_data_size > 0) {
        s->priv_data = av_mallocz(ffifmt(s->iformat)->priv_data_size);
        if (!s->priv_data) {
            ret = AVERROR(ENOMEM);
            goto error;
        }
        if (s->iformat->priv_class) {
            *(const AVClass**)s->priv_data= s->iformat->priv_class;
            av_opt_set_defaults(s->priv_data);
        }
    } else
        s->priv_data = NULL;

    *avctx = s;
    return 0;
  error:
    avformat_free_context(s);
    return ret;
}

int avdevice_input_info_query_json(char *dst, int dst_size)
{
    int64_t t_us;
    int len;

    if (!dst || dst_size <= 0)
        return AVERROR(EINVAL);

    t_us = av_gettime();

#if defined(__APPLE__)
    {
        CGRect bounds = CGDisplayBounds(CGMainDisplayID());
        CGEventRef ev = CGEventCreate(NULL);
        CGPoint p = { 0 };

        if (ev) {
            p = CGEventGetLocation(ev);
            CFRelease(ev);
        }

        len = snprintf(dst, dst_size,
                       "{\"t_us\":%" PRId64 ",\"mx\":%.0f,\"my\":%.0f,\"w\":%.0f,\"h\":%.0f}",
                       t_us, p.x, p.y, bounds.size.width, bounds.size.height);
    }
#else
    len = snprintf(dst, dst_size, "{\"t_us\":%" PRId64 "}", t_us);
#endif

    if (len < 0)
        return AVERROR(EINVAL);
    if (len >= dst_size)
        return AVERROR(ENOSPC);

    return len;
}

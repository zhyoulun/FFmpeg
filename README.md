FFmpeg README
=============

## 本仓库的改动说明（本地定制）

本仓库在上游 FFmpeg 的基础上，对 `ffplay` 的 SEI 叠加层显示做了若干改进，主要用于更清晰地展示 `AV_FRAME_DATA_SEI_UNREGISTERED` 中携带的文本信息。

### ffplay：SEI 叠加层显示优化（macOS）

* 字号与布局：根据当前 viewport 自适应字号，并调整边距、行间距与背景框尺寸，使叠加信息在高分辨率画面下也可读。
* 文本裁切修复：修正 CoreText 渲染时的基线/高度计算，避免每行只显示上半部分的问题。
* 方向修复：修正 macOS/CoreText 渲染路径下偶发的上下颠倒显示问题。
* 时间字段格式化：将 SEI 文本中的 `t_us`（微秒 Unix 时间戳）转换为 `YYYY-MM-DD HH:MM:SS` 形式显示（按本机时区）。

相关实现主要位于：`fftools/ffplay.c`。

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides means to alter decoded audio and video through a directed graph of connected filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process and will be ignored.

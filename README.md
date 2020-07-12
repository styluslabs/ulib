# ulib #

Support library for [usvg](https://github.com/styluslabs/usvg) and [ugui](https://github.com/styluslabs/ugui)

Includes some potentially useful single-header libraries:
* [unet.h](unet.h) - minimal cross-platform sockets wrapper (Windows + Unix, incl. iOS, Android, and macOS)
* [miniz_gzip.h](miniz_gzip.h) - gzip/gunzip using [miniz](https://github.com/richgel999/miniz) zlib library; also implements a "block gzip" format providing block-level random read access and the ability to replace or append blocks w/o rewriting blocks before the first modified block

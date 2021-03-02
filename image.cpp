// Image decoding/encoding:
// * uses https://github.com/nothings/stb - PNG/JPG enc (stb_image_write.h), PNG/JPG dec (stb_image.h)
// * optionally uses miniz to provide deflate for PNG (impl in stb_image_write uses fixed dictionary)
// * code using libjpeg(-turbo) and libpng hasn't been tested recently
// Refs:
// - https://blog.gibson.sh/2015/07/18/comparing-png-compression-ratios-of-stb_image_write-lodepng-miniz-and-libpng/
// - https://blog.gibson.sh/2015/03/23/comparing-performance-stb_image-vs-libjpeg-turbo-libpng-and-lodepng/
// Other options:
// - https://github.com/serge-rgb/TinyJPEG/ for JPEG compression
// - https://github.com/lvandeve/lodepng - PNG enc/dec (2 files)
// Image processing libraries:
// - CImg - can only read/write to files; could load with separate code
// - CxImage - seems to do everything; last update 2011

// JPEG images are degraded slightly every time they're encoded, with severe degredation after a few hundred
//  cycles, so we cache JPEG data when decoding to avoid reencoding unless modified; we don't bother for PNG
//  since reencoding is lossless and encoded data is closer in size to decoded data (so more memory usage)

// I/O options: char*, size_t vs. istream, ostream vs. vector/string
// libpng: uses callbacks passed source/dest and length, so buffer or streams work equally well
// libjpeg: uses buffers natively ... but since we need to convert RGB888 <-> RGBA8888, we have to go through
//   temp buffer both directions, so we can use streams IF we can combine the conversion with stream I/O
// STB_image: works with mem buffers natively; supports callbacks

#include <stdio.h>
#include <string.h>
#include "image.h"
#include "painter.h"

Image::Image(int w, int h, Encoding imgfmt) : Image(w, h, NULL, imgfmt)
{
  if(w > 0 && h > 0)
    data = (unsigned char*)calloc(w*h, 4);
}

Image::Image(Image&& other) : width(std::exchange(other.width, 0)), height(std::exchange(other.height, 0)),
    data(std::exchange(other.data, nullptr)), jpegData(std::move(other.jpegData)),
    encoding(other.encoding), painterHandle(std::exchange(other.painterHandle, -1)) {}

Image& Image::operator=(Image&& other)
{
  std::swap(width, other.width);
  std::swap(height, other.height);
  std::swap(data, other.data);
  std::swap(jpegData, other.jpegData);
  std::swap(encoding, other.encoding);
  std::swap(painterHandle, other.painterHandle);
  return *this;
}

// we've switched from vector to plain pointer for data since that's what stb_image's load fns return
// we can't copy painterHandle ... TODO: could use something like clone_ptr here instead
Image::Image(const Image& other) : width(other.width), height(other.height), data(NULL),
   jpegData(other.jpegData), encoding(other.encoding), painterHandle(-1)
{
  int n = width*height*4;
  data = (unsigned char*)malloc(n);
  memcpy(data, other.data, n);
}

Image Image::fromPixels(int w, int h, unsigned char* d, Encoding imgfmt)
{
  size_t n = w*h*4;
  unsigned char* ourpx = (unsigned char*)malloc(n);
  memcpy(ourpx, d, n);
  return Image(w, h, ourpx, imgfmt);
}

Image Image::fromPixelsNoCopy(int w, int h, unsigned char* d, Encoding imgfmt)
{
  return Image(w, h, d, imgfmt);
}

Image::~Image()
{
  Painter::invalidateImage(this);
  if(data)
    free(data);
}

// use Painter for image transformations
Image Image::transformed(const Transform2D& tf) const
{
  // apply transform to bounding rect to determine size of output image
  Rect b = tf.mapRect(Rect::wh(width, height));
  int wout = std::ceil(b.width());
  int hout = std::ceil(b.height());
  Image out(wout, hout, encoding);
  Painter painter(&out);
  painter.setBackgroundColor(Color::TRANSPARENT_COLOR);
  painter.beginFrame();
  painter.transform(Transform2D().translate(-b.left, -b.top) * tf);
  // all scaling done by transform, so pass dest = src so drawImage doesn't scale
  painter.drawImage(Rect::wh(width, height), *this);
  painter.endFrame();
  return out;
}

Image Image::scaled(int w, int h) const
{
  return transformed(Transform2D().scale(w/(float)width, h/(float)height));
}

Image Image::cropped(const Rect& src) const
{
  int outw = std::min(int(src.right), width) - int(src.left);
  int outh = std::min(int(src.bottom), height) - int(src.top);
  if(outw <= 0 || outh <= 0)
    return Image(0, 0);
  Image out(outw, outh, encoding);
  const unsigned int* srcpix = constPixels() + int(src.top)*width + int(src.left);
  unsigned int* dstpix = out.pixels();
  for(int y = 0; y < out.height; ++y) {
    for(int x = 0; x < out.width; ++x)
      dstpix[y*out.width + x] = srcpix[y*width + x];
  }
  return out;
}

void Image::fill(unsigned int color)
{
  Painter::invalidateImage(this);
  unsigned int* pixels = (unsigned int*)data;
  for(int ii = 0; ii < width*height; ++ii)
    pixels[ii] = color;
}

// only used for comparing test images, so we don't care about performance
Image& Image::subtract(const Image& other, int scale, int offset)
{
  unsigned char* a = bytes();
  const unsigned char* b = other.constBytes();
  for(int ii = 0; ii < std::min(height, other.height); ++ii) {
    for(int jj = 0; jj < std::min(width, other.width)*4; ++jj) {
      if(jj % 4 != 3) // skip alpha
        a[ii*width*4 + jj] = scale*((int)a[ii*width*4 + jj] - b[ii*other.width*4 + jj]) + offset;
        //a[ii*width*4 + jj] = std::min(std::max(scale*((int)a[ii*width*4 + jj] - b[ii*other.width*4 + jj]) + offset, 0), 255);
    }
  }
  return *this;
}

bool Image::operator==(const Image& other) const
{
  if(width != other.width || height != other.height)
    return false;
  return memcmp(data, other.data, dataLen()) == 0;
}

bool Image::hasTransparency() const
{
  unsigned int* pixels = (unsigned int*)data;
  for(int ii = 0; ii < width*height; ++ii) {
    if((pixels[ii] & 0xFF000000) != 0xFF000000)
      return true;
  }
  return false;
}

// decoding

// JPEG and PNG dominate code, so excluding other types gains little
//#define STBI_ONLY_JPEG
//#define STBI_ONLY_PNG
//#define STBI_ONLY_BMP
// don't need file I/O
//#define STBI_NO_STDIO
//#define STB_IMAGE_IMPLEMENTATION -- we expect this to be done somewhere else
#include "stb_image.h"

Image Image::decodeBuffer(const unsigned char* buff, size_t len, Encoding formatHint)
{
  if(!buff || len < 16)
    return Image(0, 0);

  if(buff[0] == 0xFF && buff[1] == 0xD8)
    formatHint = JPEG;
  else if(memcmp(buff, "\x89PNG", 4) == 0)
    formatHint = PNG;

#ifdef USE_STB_IMAGE
  int w = 0, h = 0;
  unsigned char* data = stbi_load_from_memory(buff, len, &w, &h, NULL, 4);  // request 4 channels (RGBA)
  return Image(w, h, data, formatHint, formatHint == JPEG ? EncodeBuff(buff, buff+len) : EncodeBuff());
#else
  if(formatHint == PNG)
    return decodePNG(buff, len);
  else //if(formatHint == JPEG)
    return decodeJPEG(buff, len);
#endif
}

// encoding

Image::EncodeBuff Image::encode(Encoding fmt) const
{
  return fmt == JPEG ? encodeJPEG() : encodePNG();
}

#ifndef NO_MINIZ
#include "miniz/miniz.h"

// use miniz instead of zlib impl built into stb_image_write for better compression
static unsigned char* mz_stbiw_zlib_compress(unsigned char *data, int data_len, int *out_len, int quality)
{
  mz_ulong buflen = mz_compressBound(data_len);
  // Note that the returned buffer will be free'd by stbi_write_png*()
  // with STBIW_FREE(), so if you have overridden that (+ STBIW_MALLOC()),
  // adjust the next malloc() call accordingly:
  unsigned char* buf = (unsigned char*)malloc(buflen);
  if(buf == NULL || mz_compress2(buf, &buflen, data, data_len, quality) != 0) {
    free(buf); // .. yes, this would have to be adjusted as well.
    return NULL;
  }
  *out_len = buflen;
  return buf;
}

#define STBIW_ZLIB_COMPRESS  mz_stbiw_zlib_compress
#endif // NO_MINIZ

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static void stbi_write_vec(void* context, void* data, int size)
{
  auto v = static_cast<Image::EncodeBuff*>(context);
  auto d = static_cast<unsigned char*>(data);
  v->insert(v->end(), d, d + size);
}

Image::EncodeBuff Image::encodePNG() const
{
  //stbi_write_png_compression_level = quality;
  EncodeBuff v;
  v.reserve(dataLen()/4);  // guess at compressed size
  // returns 0 on failure ...
  if(!stbi_write_png_to_func(&stbi_write_vec, &v, width, height, 4, data, width*4))
    v.clear();
  return v;
}

Image::EncodeBuff Image::encodeJPEG(int quality) const
{
  if(jpegData.empty()) {
    jpegData.reserve(dataLen()/4);
    // returns 0 on failure ...
    if(!stbi_write_jpg_to_func(&stbi_write_vec, &jpegData, width, height, 4, data, quality))
      jpegData.clear();
  }
  return jpegData;  // makes a copy unavoidably
}

// libjpeg, libpng, and base64 code removed 19 Feb 2021

// test
#ifdef IMAGE_TEST
#include <fstream>

int main(int argc, char* argv[])
{
  if(argc < 2)
    return -1;
  std::vector<char> buff;
  if(!readFile(&buff, argv[1]))
    return -2;
  Image* img = Image::decodeBuffer(&buff[0], buff.size());
  if(!img)
    return -3;
  // reencode
  char* outbuff;
  size_t outlen;
  // write to jpeg
  img->encodeJPEG(&outbuff, &outlen);
  std::ofstream jpegout("out.jpg", std::ofstream::binary);
  jpegout.write(outbuff, outlen);
  jpegout.close();
  free(outbuff);
  // write to png
  img->encodePNG(&outbuff, &outlen);
  std::ofstream pngout("out.png", std::ofstream::binary);
  pngout.write(outbuff, outlen);
  pngout.close();
  free(outbuff);
  // all done
  delete img;
  return 0;
}
#endif

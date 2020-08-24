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

// I/O options: char*, size_t vs. istream, ostream vs. vector/string
// libpng: uses callbacks passed source/dest and length, so buffer or streams work equally well
// libjpeg: uses buffers natively ... but since we need to convert RGB888 <-> RGBA8888, we have to go through
//   temp buffer both directions, so we can use streams IF we can combine the conversion with stream I/O
// STB_image: works with mem buffers natively; supports callbacks

#include <stdio.h>
#include <string.h>
#include "image.h"
#include "painter.h"

Image::Image(int w, int h, ImageFormat imgfmt) : Image(w, h, NULL, imgfmt)
{
  if(w > 0 && h > 0)
    data = (unsigned char*)calloc(w*h, 4);
}

Image::Image(Image&& other) : width(std::exchange(other.width, 0)), height(std::exchange(other.height, 0)),
    data(std::exchange(other.data, nullptr)), imageFormat(other.imageFormat),
    painterHandle(std::exchange(other.painterHandle, -1)) {}

Image& Image::operator=(Image&& other)
{
  std::swap(width, other.width);
  std::swap(height, other.height);
  std::swap(data, other.data);
  std::swap(imageFormat, other.imageFormat);
  std::swap(painterHandle, other.painterHandle);
  return *this;
}

// we've switched from vector to plain pointer for data since that's what stb_image's load fns return
// we can't copy painterHandle ... TODO: could use something like clone_ptr here instead
Image::Image(const Image& other) : width(other.width), height(other.height), data(NULL),
    imageFormat(other.imageFormat), painterHandle(-1)
{
  int n = width*height*4;
  data = (unsigned char*)malloc(n);
  memcpy(data, other.data, n);
}

Image Image::fromPixels(int w, int h, unsigned char* d, ImageFormat imgfmt)
{
  size_t n = w*h*4;
  unsigned char* ourpx = (unsigned char*)malloc(n);
  memcpy(ourpx, d, n);
  return Image(w, h, ourpx, imgfmt);
}

Image Image::fromPixelsNoCopy(int w, int h, unsigned char* d, ImageFormat imgfmt)
{
  return Image(w, h, d, imgfmt);
}

Image::~Image()
{
  Painter::invalidateImage(painterHandle);
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
  Image out(wout, hout, imageFormat);
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
  Image out(outw, outh, imageFormat);
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
  Painter::invalidateImage(painterHandle);
  painterHandle = -1;
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

#ifdef USE_STB_IMAGE
// JPEG and PNG dominate code, so excluding other types gains little
//#define STBI_ONLY_JPEG
//#define STBI_ONLY_PNG
//#define STBI_ONLY_BMP
// don't need file I/O
//#define STBI_NO_STDIO
//#define STB_IMAGE_IMPLEMENTATION -- we expect this to be done somewhere else
#include "stb_image.h"
#endif

Image Image::decodeBuffer(const unsigned char* buff, size_t len, ImageFormat formatHint)
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
  return Image(w, h, data, formatHint);
#else
  if(formatHint == PNG)
    return decodePNG(buff, len);
  else //if(formatHint == JPEG)
    return decodeJPEG(buff, len);
#endif
}

// encoding

Image::EncodeBuff Image::encode(ImageFormat defaultFormat) const
{
  ImageFormat format = imageFormat == UNKNOWN ? defaultFormat : imageFormat;
  if(format == JPEG && !hasTransparency())
    return encodeJPEG();
  else
    return encodePNG();
}

#ifdef USE_STB_IMAGE
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
  EncodeBuff v;
  v.reserve(dataLen()/4);
  // returns 0 on failure ...
  if(!stbi_write_jpg_to_func(&stbi_write_vec, &v, width, height, 4, data, quality))
    v.clear();
  return v;
}

Image::EncodeBuff Image::toBase64(const Image::EncodeBuff& src)
{
  EncodeBuff v;
  size_t base64len;
  char* base64 = base64_encode(NULL, src.size(), NULL, &base64len);
  v.resize(++base64len);  // base64len does not include trailing '\0'
  base64_encode((const unsigned char*)src.data(), src.size(), (char*)v.data(), &base64len);
  return v;
}

#else
// use libjpeg(-turbo) and libpng
#include <jpeglib.h>
#include <png.h>

struct mem_block {
  mem_block(char* d, size_t s) : data(d), size(s), index(0) {}
  char* data;
  size_t size;
  int index;
};

static void readPNGCallback(png_structp pngptr, png_bytep buffer, png_size_t length)
{
  mem_block* mb = (mem_block*)png_get_io_ptr(pngptr);
  memcpy(buffer, mb->data + mb->index, length);
  mb->index += length;
}

static void writePNGCallback(png_structp pngptr, png_bytep buffer, png_size_t length)
{
  mem_block* mb = (mem_block*)png_get_io_ptr(pngptr);
  memcpy(mb->data + mb->index, buffer, length);
  mb->index += length;
}

Image* Image::decodePNG(const char* buff, size_t len)
{
  mem_block block((char*)buff, len);
  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if(!png_ptr) return NULL;
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if(!info_ptr) return NULL;
  if(setjmp(png_jmpbuf(png_ptr)))
    return NULL;
  png_set_read_fn(png_ptr, &block, readPNGCallback);
  png_read_info(png_ptr, info_ptr);
  int width = png_get_image_width(png_ptr, info_ptr);
  int height = png_get_image_height(png_ptr, info_ptr);
  if(png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_PALETTE)
    png_set_palette_to_rgb(png_ptr);
  if(png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_GRAY || png_get_color_type(png_ptr, info_ptr) == PNG_COLOR_TYPE_GRAY_ALPHA)
    png_set_gray_to_rgb(png_ptr);
  if(!(png_get_color_type(png_ptr, info_ptr) & PNG_COLOR_MASK_ALPHA))
    png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
  if(png_get_bit_depth(png_ptr, info_ptr) < 8)
    png_set_packing(png_ptr);
  if(png_get_bit_depth(png_ptr, info_ptr) == 16)
    png_set_strip_16(png_ptr);
  // now we should get 32 bit RGBA
  png_read_update_info(png_ptr, info_ptr);

  Image* imgout = new Image(width, height, PNG);
  char* outbuff = imgout->data.data();
  //char* outbuff = new char[width*height*4];

  png_bytep* row_pointers = new png_bytep[height];
  for(int yp = 0; yp < height; yp++)
    row_pointers[yp] = (png_byte*)(outbuff + 4*width*yp);
  png_read_image(png_ptr, row_pointers);
  png_read_end(png_ptr, info_ptr);
  delete[] row_pointers;
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
  return imgout; //new Image(width, height, outbuff, PNG);
}

// options: accept dest pointer and max len; accept vector<>, return vector<>

// ideally: if we have a simple buffer, we can write to it without an unnecessary copy - this means we must be able to work with simple buffer, not just vector
// options: 1. wrap buffer with an ostream, 2. accept iterator instead of stream, use ostream_iterator

bool Image::encodePNG(char** buffout, size_t* lenout) const
{
  size_t maxsize = 4*width*height + 4096;
  char* compressed = (char*)malloc(maxsize);
  mem_block block(compressed, maxsize);
  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  const char* bytes = data.data();
  if(!png_ptr) return false;
  png_infop info_ptr = png_create_info_struct(png_ptr);
  if(!info_ptr) return false;
  if(setjmp(png_jmpbuf(png_ptr)))
    return false;
  png_set_write_fn(png_ptr, &block, writePNGCallback, 0);
  png_set_IHDR(png_ptr, info_ptr, width, height, 8,
     PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png_ptr, info_ptr);
  png_bytep* row_pointers = new png_bytep[height];
  for(int yp = 0; yp < height; yp++)
    row_pointers[yp] = (png_byte*)(bytes + 4*width*yp);
  png_write_image(png_ptr, row_pointers);
  delete[] row_pointers;
  png_write_end(png_ptr, NULL);
  png_destroy_write_struct(&png_ptr, &info_ptr);
  *buffout = compressed;
  *lenout = block.index;
  return true;
}

Image* Image::decodeJPEG(const char* buff, size_t len)
{
  struct jpeg_decompress_struct jpeg;
  struct jpeg_error_mgr err;

  jpeg.err = jpeg_std_error(&err);
  jpeg_create_decompress(&jpeg);
  jpeg_mem_src(&jpeg, (uint8_t*)buff, len);
  jpeg_read_header(&jpeg, 1);
  jpeg_start_decompress(&jpeg);
  int width = jpeg.output_width;
  int height = jpeg.output_height;
  if(jpeg.output_components != 3) return NULL;

  Image* imgout = new Image(width, height, JPEG);
  uint8_t* pData = (uint8_t*)imgout->data.data();
  //uint8_t* pData = new uint8_t[width*height*4];

  uint8_t* pRow = new uint8_t[width*3];
  //if(!pRow) { jpeg_abort_decompress(&jpeg); jpeg_destroy_decompress(&jpeg); }
  uint32_t* pPixel = (uint32_t*)pData;
  for(int y = 0; y < height; ++y) {
    jpeg_read_scanlines(&jpeg, &pRow, 1);
    for(int x = 0; x < width; ++x, ++pPixel)
      *pPixel = 0xFF << 24 | pRow[x*3 + 2] << 16 | pRow[x*3 + 1] << 8 | pRow[x*3];
  }
  jpeg_finish_decompress(&jpeg);
  jpeg_destroy_decompress(&jpeg);
  delete[] pRow;
  return imgout; //new Image(width, height, (char*)pData, JPEG);
}

bool Image::encodeJPEG(char** buffout, size_t* lenout, int quality) const
{
  jpeg_compress_struct cinfo;
  jpeg_error_mgr jerr;
  uint8_t* mem = NULL;
  unsigned long memSize = 0;
  const char* bytes = data.data();
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 3;
  cinfo.in_color_space = JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_mem_dest(&cinfo, &mem, &memSize);
  jpeg_set_quality(&cinfo, quality, true);
  jpeg_start_compress(&cinfo, true);
  JSAMPROW rowptr = new uint8_t[width*3];
  while(cinfo.next_scanline < cinfo.image_height) {
    const char* src = &bytes[cinfo.next_scanline*width*4];
    char* dest = (char*)rowptr;
    for(int x = 0; x < width; x++, dest += 3, src += 4)    {
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
    }
    jpeg_write_scanlines(&cinfo, &rowptr, 1);
  }
  jpeg_finish_compress(&cinfo);
  *buffout = (char*)mem;
  *lenout = memSize;
  jpeg_destroy_compress(&cinfo);
  delete rowptr;
  return true;
}
#endif  // else not USE_STB_IMAGE

// base64 encode/decode - TODO: replace this w/ impl in stringutil.h
// - mostly from http://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c
static const char base64enc[] = "ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZabcdef" "ghijklmn" "opqrstuv" "wxyz0123" "456789+/";
static char _base64dec[256];
static char* base64dec = NULL;
static const int mod_table[] = {0, 2, 1};

char* base64_encode(const unsigned char *data, size_t input_length, char* encoded_data, size_t *output_length)
{
  size_t enclen = 4 * ((input_length + 2) / 3);
  if(encoded_data && *output_length < enclen + 1)  // caller passed a buffer, but it's too small
    return NULL;
  *output_length = enclen;
  if(!data) return NULL;  // no data - caller may just want output_length
  if(!encoded_data)
    encoded_data = (char*)malloc(enclen + 1);
  if(!encoded_data) return NULL;  // allocation failure

  for(unsigned int i = 0, j = 0; i < input_length;) {
    uint32_t octet_a = i < input_length ? data[i++] : 0;
    uint32_t octet_b = i < input_length ? data[i++] : 0;
    uint32_t octet_c = i < input_length ? data[i++] : 0;

    uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

    encoded_data[j++] = base64enc[(triple >> 3 * 6) & 0x3F];
    encoded_data[j++] = base64enc[(triple >> 2 * 6) & 0x3F];
    encoded_data[j++] = base64enc[(triple >> 1 * 6) & 0x3F];
    encoded_data[j++] = base64enc[(triple >> 0 * 6) & 0x3F];
  }

  for(int i = 0; i < mod_table[input_length % 3]; i++)
    encoded_data[enclen - 1 - i] = '=';

  encoded_data[enclen] = '\0';  // zero-terminate so it can be passed as string
  return encoded_data;
}

unsigned char* base64_decode(const char *data, size_t input_length, unsigned char* decoded_data, size_t *output_length)
{
  if(!base64dec) {
    // init decoding table on first run
    base64dec = &(_base64dec[0]);
    for(int ii = 0; ii < 64; ii++)
      base64dec[(unsigned char) base64enc[ii]] = ii;
  }

  if(input_length % 4 != 0) return NULL;
  size_t declen = input_length / 4 * 3;
  if(data[input_length - 1] == '=') declen--;
  if(data[input_length - 2] == '=') declen--;

  if(decoded_data && *output_length < declen)  // caller passed a buffer, but it's too small
    return NULL;
  *output_length = declen;
  if(!data) return NULL;  // no data - caller may just want output_length
  if(!decoded_data)
    decoded_data = (unsigned char*)malloc(declen);
  if(!decoded_data) return NULL;  // allocation failure

  for(unsigned int i = 0, j = 0; i < input_length;) {
    uint32_t sextet_a = data[i] == '=' ? 0 & i++ : base64dec[int(data[i++])];
    uint32_t sextet_b = data[i] == '=' ? 0 & i++ : base64dec[int(data[i++])];
    uint32_t sextet_c = data[i] == '=' ? 0 & i++ : base64dec[int(data[i++])];
    uint32_t sextet_d = data[i] == '=' ? 0 & i++ : base64dec[int(data[i++])];

    uint32_t triple = (sextet_a << 3 * 6)
        + (sextet_b << 2 * 6)
        + (sextet_c << 1 * 6)
        + (sextet_d << 0 * 6);

    if(j < declen) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
    if(j < declen) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
    if(j < declen) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
  }
  return decoded_data;
}

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

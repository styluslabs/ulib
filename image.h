#pragma once

#include <stddef.h>
#include <vector>
#include "geom.h"

#define USE_STB_IMAGE

class Image {
public:
  typedef std::vector<unsigned char> EncodeBuff;

  int width;
  int height;
  unsigned char* data;  //std::vector<char> data;
  mutable EncodeBuff jpegData;
  enum Encoding {UNKNOWN=0, PNG=1, JPEG=2} encoding;  // prefered encoding
  mutable int painterHandle;

  Image(int w, int h, Encoding imgfmt = UNKNOWN);
  Image(Image&& other);
  Image& operator=(Image&& obj);
  ~Image();
  Image copy() const { return Image(*this); }
  void invalidate();

  unsigned char* bytes() { return data; }
  const unsigned char* constBytes() const { return data; }
  unsigned int* pixels() { return (unsigned int*)data; }
  const unsigned int* constPixels() const { return (const unsigned int*)data; }
  int dataLen() const { return width*height*4; }
  int getWidth() const { return width; }
  int getHeight() const { return height; }
  bool hasTransparency() const;
  Image& subtract(const Image& other, int scale=1, int offset=0);

  EncodeBuff encode(Encoding dflt) const;  // dflt=PNG
  EncodeBuff encodePNG() const;
  EncodeBuff encodeJPEG(int quality = 75) const;

  void fill(unsigned int color);
  Image scaled(int w, int h) const;  // return a scaled version of the image
  Image transformed(const Transform2D& tf) const;
  Image cropped(const Rect& src) const;
  bool isNull() const { return !data; }
  bool operator==(const Image& other) const;
  bool operator!=(const Image& other) const { return !operator==(other); }

  //enum PixelFormat {ARGB32, ARGB32_Premul, RGB32};
  //PixelFormat format() const { return ARGB32; }
  //Image convertToFormat(PixelFormat format) const { return Image(*this); }
  //unsigned int pixel(int x, int y) const { return constPixels()[y * width + x]; }
  //unsigned int* scanLine(int y) { return pixels() + y * width; }

#ifndef USE_STB_IMAGE
  static Image* decodePNG(const char* buff, size_t len);
  static Image* decodeJPEG(const char* buff, size_t len);
#endif
  static Image decodeBuffer(const unsigned char* buff, size_t len, Encoding formatHint = UNKNOWN);
  static Image fromPixels(int w, int h, unsigned char* d, Encoding imgfmt = UNKNOWN);
  static Image fromPixelsNoCopy(int w, int h, unsigned char* d, Encoding imgfmt = UNKNOWN);
protected:
  Image(int w, int h, unsigned char* d, Encoding imgfmt, EncodeBuff jpegdata = EncodeBuff())
      : width(w), height(h), data(d), jpegData(jpegdata), encoding(imgfmt), painterHandle(-1) {}
  Image(const Image& other);
};

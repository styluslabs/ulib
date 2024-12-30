#pragma once

#include <utility>
#include "geom.h"

typedef unsigned int color_t;

// what about the convention that class and struct types get capital names, while simple types don't
//  so real instead of Dim, and we could have typedef unsigned int color_t
class Color {
public:
  color_t color;

  Color(color_t c = NONE) : color(c) {}
  Color(int r, int g, int b, int a = 255)
  {
    color = ((a & 255) << SHIFT_A) | ((r & 255) << SHIFT_R) | ((g & 255) << SHIFT_G) | ((b & 255) << SHIFT_B);
  }
  static Color fromRgb(unsigned int argb) { return Color(swapRB(argb) | A); }
  static Color fromArgb(unsigned int argb) { return Color(swapRB(argb)); }
  static Color fromFloat(float r, float g, float b, float a = 1.0)
    { return Color(int(r*255 + 0.5), int(g*255 + 0.5), int(b*255 + 0.5), int(a*255 + 0.5)); }

  void setColor(color_t c) { color = c; }
  // convert to/from ARGB order ... consider instead storing as ARGB and providing to/fromGLColor() methods
  void setArgb(unsigned int argb) { color = swapRB(argb); }
  void setRgb(unsigned int rgb) { color = swapRB(rgb) | A; }
  unsigned int argb() const { return swapRB(color); }
  //unsigned int rgba() const { return argb(); }  // ???
  unsigned int rgb() const { return swapRB(color & ~A); }

  static unsigned int swapRB(unsigned int c) { return (c & A) | ((c & B) >> 16) | (c & G) | ((c & R) << 16); }

  Color& setAlpha(int a) { color = (color & ~A) | (a << SHIFT_A); return *this; }
  Color& setAlphaF(float a) { return setAlpha(int(a * 255.0f + 0.5f)); }
  Color& mulAlphaF(float a) { return setAlphaF(alphaF()*a); }
  real alphaF() const { return alpha() / 255.0f; }
  int alpha() const { return (color >> SHIFT_A) & 255; }
  int red() const { return (color >> SHIFT_R) & 255; }
  int green() const { return (color >> SHIFT_G) & 255; }
  int blue() const { return (color >> SHIFT_B) & 255; }
  int luma() const { return int(0.2126*red() + 0.7152*green() + 0.0722*blue() + 0.5); }

  bool isValid() const { return color != Color::INVALID_COLOR; }
  Color opaque() const { return Color(color | A); }

  // src over blend
  static Color mix(Color src, Color dest)
  {
    real a = src.alphaF();
    return Color(int(src.red()*a + dest.red()*(1-a) + 0.5), int(src.green()*a + dest.green()*(1-a) + 0.5),
        int(src.blue()*a + dest.blue()*(1-a) + 0.5), int(src.alpha()*a + dest.alpha()*(1-a) + 0.5));
  }

  friend bool operator==(const Color& a, const Color& b) { return a.color == b.color; }
  friend bool operator!=(const Color& a, const Color& b) { return a.color != b.color; }

  // shifts
  // OpenGL uses byte-order RGBA, corresponding to word-order ABGR32 on little endian (x86) and RGBA32 on big endian (arm)
  static constexpr int SHIFT_A = 24; //24;
  static constexpr int SHIFT_R = 0; //16;
  static constexpr int SHIFT_G = 8; //8;
  static constexpr int SHIFT_B = 16; //0;
  // masks
  static constexpr color_t A = 0xFFu << SHIFT_A;
  static constexpr color_t R = 0xFFu << SHIFT_R;
  static constexpr color_t G = 0xFFu << SHIFT_G;
  static constexpr color_t B = 0xFFu << SHIFT_B;

  // consider namespace Colors { ... } instead?
  static constexpr color_t INVALID_COLOR = 0x00000000;
  // if you need a totally transparent color, use this
  static constexpr color_t TRANSPARENT_COLOR = R | G | B;
  static constexpr color_t NONE = R | G | B;
  static constexpr color_t WHITE = A | R | G | B;
  static constexpr color_t BLACK = A;
  static constexpr color_t RED = A | R;
  static constexpr color_t GREEN = A | G;
  static constexpr color_t DARKGREEN = A | (0x7Fu << SHIFT_G);  // #0f0 is too bright
  static constexpr color_t BLUE = A | B;
  static constexpr color_t YELLOW = A | R | G;
  static constexpr color_t MAGENTA = A | R | B;
  static constexpr color_t CYAN = A | G | B;
};

struct ColorF {
  float r,g,b,a;
  //union { float rgba[4]; struct { float r,g,b,a; }; };
  ColorF(float _r = 0.f, float _g = 0.f, float _b = 0.f, float _a = 1.0f) : r(_r), g(_g), b(_b), a(_a) {}
  ColorF(const Color& c) : r(c.red()/255.0f), g(c.green()/255.0f), b(c.blue()/255.0f), a(c.alpha()/255.0f) {}
  float hueHSV() const;  // 0 - 360
  float satHSV() const;  // 0 - 1
  float valueHSV() const;  // 0 - 1
  Color toColor() const { return Color::fromFloat(r, g, b, a); }

  static ColorF fromHSV(float h, float s, float v, float a = 1.0f);
};

struct GradientStop
{
  GradientStop(real p, Color c) : first(p), second(c) {}
  real first; //pos;
  Color second; //color;
};

typedef std::vector<GradientStop> GradientStops;

class UniqueHandle
{
public:
  int handle = -1;
  UniqueHandle(int v) : handle(v) {}
  UniqueHandle(const UniqueHandle& other);  // = delete;
  UniqueHandle(UniqueHandle&& other) : handle(std::exchange(other.handle, -1)) {}
  UniqueHandle& operator=(UniqueHandle&& other) { std::swap(handle, other.handle); return *this; }
};

class Gradient
{
public:
  enum Type { Linear, Radial, Box } type;
  struct LinearGradCoords { real x1, y1, x2, y2; };
  struct RadialGradCoords { real cx, cy, radius, fx, fy; };
  struct BoxGradCoords { real x, y, w, h, r, feather; };
  union { LinearGradCoords linear; RadialGradCoords radial; BoxGradCoords box; } coords;
  GradientStops gradStops;
  Rect objectBBox;
  mutable UniqueHandle painterHandle = -1;

  enum CoordinateMode { userSpaceOnUseMode, ObjectBoundingMode } coordMode = ObjectBoundingMode;
  enum Spread { PadSpread, RepeatSpread, ReflectSpread };
  enum ColorInterpolation { sRGBColorInterp, LinearColorInterp } colorInterp = sRGBColorInterp;

  static Gradient linear(real x1, real y1, real x2, real y2)
      { return Gradient(LinearGradCoords{x1, y1, x2, y2}); }
  static Gradient radial(real cx, real cy, real radius, real fx, real fy)
      { return Gradient(RadialGradCoords{ cx, cy, radius, fx, fy }); }
  static Gradient box(real x, real y, real w, real h, real r = 0, real feather = 0)
      { return Gradient(BoxGradCoords{ x, y, w, h, r, feather }); }

  Gradient(LinearGradCoords c) : type(Linear) { coords.linear = c; }
  Gradient(RadialGradCoords c) : type(Radial) { coords.radial = c; }
  Gradient(BoxGradCoords c) : type(Box) { coords.box = c; }
  Gradient(const Gradient& other) = default;
  Gradient(Gradient&& other) = default;
  void invalidate() const;
  ~Gradient() { invalidate(); }

  void setSpread(Spread spread) {}
  void setCoordinateMode(CoordinateMode mode) { coordMode = mode; }
  CoordinateMode coordinateMode() const { return coordMode; }
  void setColorInterp(ColorInterpolation mode) { colorInterp = mode; }
  const GradientStops& stops() const { return gradStops; }
  void setStops(GradientStops stops) { gradStops = stops; invalidate(); }
  void clearStops() { gradStops.clear(); invalidate(); }
  // stops should be added in order
  void addStop(real pos, Color color) { gradStops.emplace_back(pos, color); invalidate(); }
  void setObjectBBox(const Rect& r) { objectBBox = r; }
};

// ideally Brush and Pen will get merged into the SVG styles
class Brush
{
public:
  Color brushColor;
  Gradient* brushGradient;
  enum Style { NoBrush, Solid, LinearGradient, RadialGradient };

  Brush(const Color& color = Color::BLACK) : brushColor(color), brushGradient(NULL) {}
  Brush(color_t color) : brushColor(color), brushGradient(NULL) {}
  // we might want to copy the gradient, but first have to determine type and cast!
  Brush(Gradient* grad) : brushGradient(grad) {}
  Style style() const
  {
    if(brushGradient)
      return brushGradient->type == Gradient::Linear ? LinearGradient : RadialGradient;
    return brushColor == Color::NONE ? NoBrush : Solid;
  }

  void setColor(Color c) { brushColor = c; }
  Color color() const { return brushColor; }
  const Gradient* gradient() const { return brushGradient; }
  void setMatrix(const Transform2D& tf) {}  // not sure what this is supposed to do
  bool isNone() const { return !brushGradient && brushColor == Color::NONE; }

  static constexpr color_t NONE = Color::NONE;
};

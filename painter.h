#pragma once

#include <vector>
#include <string>
#include <utility>
#include "geom.h"
#include "nanovg.h"

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

class Image;
class Path2D;
struct NVGLUframebuffer;
struct NVGSWUblitter;

class Painter
{
public:
  static Painter* cachingPainter;  // painter instance for caching images (i.e. as textures)
  static FONScontext* fontStash;
  static std::string defaultFontFamily;
  static int maxCachedBytes;

  static constexpr int NOT_SUPPORTED = 2000;
  static constexpr int COMP_OP_BASE = 1000;
  enum CompOp {
    CompOp_Clear = COMP_OP_BASE,
    CompOp_Src = NVG_COPY,
    CompOp_SrcOver = NVG_SOURCE_OVER,
    CompOp_DestOver = NVG_DESTINATION_OVER,
    CompOp_SrcIn = NVG_SOURCE_IN,
    CompOp_DestIn = NVG_DESTINATION_IN,
    CompOp_SrcOut = NVG_SOURCE_OUT,
    CompOp_DestOut = NVG_DESTINATION_OUT,
    CompOp_SrcAtop = NVG_ATOP,
    CompOp_DestAtop = NVG_DESTINATION_ATOP,
    CompOp_Xor = NVG_XOR,
    CompOp_Lighten = NVG_LIGHTER,
    CompOp_Dest = NOT_SUPPORTED,
    CompOp_Plus,
    CompOp_Multiply,
    CompOp_Screen,
    CompOp_Overlay,
    CompOp_Darken,
    CompOp_ColorDodge,
    CompOp_ColorBurn,
    CompOp_HardLight,
    CompOp_SoftLight,
    CompOp_Difference,
    CompOp_Exclusion
  };

  enum TextAlignBits {
    AlignLeft = NVG_ALIGN_LEFT,
    AlignHCenter = NVG_ALIGN_CENTER,
    AlignRight = NVG_ALIGN_RIGHT,
    AlignTop = NVG_ALIGN_TOP,
    AlignVCenter = NVG_ALIGN_MIDDLE,
    AlignBottom = NVG_ALIGN_BOTTOM,
    AlignBaseline = NVG_ALIGN_BASELINE
  };
  typedef unsigned int TextAlign;
  static const unsigned int HorzAlignMask = AlignLeft | AlignHCenter | AlignRight;
  static const unsigned int VertAlignMask = AlignTop | AlignVCenter | AlignBottom | AlignBaseline;

  enum CapStyle {InheritCap = -1, FlatCap = NVG_BUTT, RoundCap = NVG_ROUND, SquareCap = NVG_SQUARE};
  enum JoinStyle {InheritJoin = -1, MiterJoin = NVG_MITER, RoundJoin = NVG_ROUND, BevelJoin = NVG_BEVEL};
  enum VectorEffect { NoVectorEffect = 0, NonScalingStroke = 1 };
  enum StrokeAlign { StrokeCenter = 0, StrokeInner = 1, StrokeOuter = 2};
  enum ImageFlags { ImagePremult = NVG_IMAGE_PREMULTIPLIED, ImageNoCopy = NVG_IMAGE_NOCOPY };

  //enum FontWeight { WeightLight, WeightNormal, WeightDemiBold, WeightBold, WeightBlack };
  enum FontStyle { StyleNormal, StyleItalic, StyleOblique };
  enum FontCapitalization { MixedCase, SmallCaps, AllUppercase, AllLowercase, Capitalize };

  // need to track some state - for example, color and opacity since we accept them separately (since SVG treats them
  //  separately) but nvg only accepts them together
  struct PainterState {
    // nanovg doesn't provide a way to read back stroke and fill properties
    // ... ideally, we would also not provide a way to read back state (client should track if needed)
    Brush fillBrush;
    // stroke
    Brush strokeBrush;
    float strokeWidth = 1;
    float strokeDashOffset = 0;
    float* strokeDashes;
    float strokeMiterLimit = 0;
    CapStyle strokeCap = FlatCap;
    JoinStyle strokeJoin = BevelJoin;
    VectorEffect strokeEffect = NoVectorEffect;
    StrokeAlign strokeAlign = StrokeCenter;
    // font
    //std::string fontFamily;
    short fontId = -1, boldFontId = -1, italicFontId = -1, boldItalicFontId = -1;
    bool fauxBold = false, fauxItalic = false;
    float fontPixelSize = 16;
    int fontWeight = 400;
    float letterSpacing = 0;
    FontStyle fontStyle = StyleNormal;
    FontCapitalization fontCaps = MixedCase;
    // other
    Rect clipBounds = Rect::ltrb(REAL_MIN, REAL_MIN, REAL_MAX, REAL_MAX);
    float globalAlpha = 1.0;
    color_t colorXorMask = 0;
    CompOp compOp = CompOp_SrcOver;
    bool antiAlias = true;
    bool sRGBAdjAlpha = false;
  };
  std::vector<PainterState> painterStates;

  Rect deviceRect;
  Color bgColor = Color::WHITE;
  float atlasTextThresh = 0;
  Image* targetImage = NULL;
  NVGLUframebuffer* nvgFB = NULL;
  NVGSWUblitter* swBlitter = NULL;
  NVGcontext* vg = NULL;
  int createFlags = 0;
  // members for keeping track of and clearing GL textures for images
  int cachedBytes = 0;
  std::vector<int> imgHandles;

  enum CreateFlags { PAINT_NULL = 0, PAINT_SW = 1, PAINT_GL = 2, /*PAINT_VTEX = 3,*/ PAINT_MASK = 3,
      PRIVATE_FONTS = 1<<2, NO_TEXT = 1<<3, MULTITHREAD = 1<<4, SRGB_AWARE = 1<<5, SW_NO_XC = 1<<6,
      SW_BLIT_GL = 1<<7, CACHE_IMAGES = 1<<8, PAINT_DEBUG_GL = 1<<9, ALIGN_SCISSOR = 1<<10 };

  Painter(int flags, Image* image = NULL);
  Painter(NVGcontext* _vg, Image* image = NULL);
  //Painter(Image* image, NVGcontext* _vg);
  ~Painter();

  PainterState& currState() { return painterStates.back(); }
  const PainterState& currState() const { return painterStates.back(); }
  bool usesGPU() const;
  bool sRGB() const;
  void save();
  void restore();
  void reset();

  void beginFrame(real pxRatio = 1.0);
  void endFrame();

  void setTarget(Image* image);
  void blitImageToScreen(Rect dirty, bool blend = false);

  // transformations
  void translate(real x, real y) { nvgTranslate(vg, x, y);  }
  void translate(Point p) { translate(p.x, p.y); }
  void scale(real sx, real sy) { nvgScale(vg, sx, sy); }
  void scale(real s) { nvgScale(vg, s, s); }
  void rotate(real rad) { nvgRotate(vg, rad); }
  void transform(const Transform2D& tf);
  void setTransform(const Transform2D& tf);
  Transform2D getTransform() const;

  // nanovg only supports rectangular scissor/clip path
  void setClipRect(const Rect& r);
  void clipRect(Rect r);
  Rect getClipBounds() const;

  // drawing
  void beginPath();
  void endPath();
  void drawPath(const Path2D& path);
  void drawImage(const Rect& dest, const Image& image, Rect src = Rect(), int flags = 0);
  real drawText(real x, real y, const char* start, const char* end = NULL);
  void setTextAlign(TextAlign align);

  void drawLine(const Point& a, const Point& b);
  void drawRect(Rect rect);
  void fillRect(Rect rect, Color c);

  bool setAntiAlias(bool antialias);
  void setCompOp(CompOp op);
  CompOp compOp() const { return currState().compOp; }
  void setOpacity(real opacity);
  real opacity() const { return currState().globalAlpha; }

  // Pen, brush, font - principle is that all values should be independent and atomic (not composite, like
  //  Pen or Font class)
  NVGpaint getGradientPaint(const Gradient* grad);
  // fill
  void setFillBrush(const Brush& b);
  const Brush& fillBrush() const { return currState().fillBrush; }
  // stroke
  void setStrokeBrush(const Brush& b);
  const Brush& strokeBrush() const { return currState().strokeBrush; }
  void setVectorEffect(VectorEffect veffect) { currState().strokeEffect = veffect; }
  VectorEffect vectorEffect() const { return currState().strokeEffect; }
  void setStrokeCap(CapStyle cap);
  CapStyle strokeCap() const { return currState().strokeCap; }
  void setStrokeJoin(JoinStyle join);
  JoinStyle strokeJoin() const { return currState().strokeJoin; }
  void setMiterLimit(real lim);
  real miterLimit() const { return currState().strokeMiterLimit; }
  void setStrokeWidth(real w);
  real strokeWidth() const { return currState().strokeWidth; }
  void setStroke(const Brush& b, real w = 1, CapStyle cap = FlatCap, JoinStyle join = BevelJoin);
  void setDashArray(float* dashes);  // list should be terminated by negative value
  const float* dashArray() const { return currState().strokeDashes; }
  void setDashOffset(real offset);
  real dashOffset() const { return currState().strokeDashOffset; }
  void setStrokeAlign(StrokeAlign align) { currState().strokeAlign = align; }
  StrokeAlign strokeAlign() const { return currState().strokeAlign; }
  // font
  void setFontSize(real px);
  real fontSize() const { return currState().fontPixelSize; }
  void setFontWeight(int weight);
  int fontWeight() const { return currState().fontWeight; }
  void setLetterSpacing(real px);
  real letterSpacing() const { return currState().letterSpacing; }
  bool setFontFamily(const char* family);
  //const char* fontFamily() const { return currState().fontFamily.c_str(); }
  void setFontStyle(FontStyle style);
  FontStyle fontStyle() const { return currState().fontStyle; }
  void setCapitalization(FontCapitalization c) { currState().fontCaps = c; }
  FontCapitalization capitalization() const { return currState().fontCaps; }
  void setAtlasTextThreshold(float thresh);

  void setsRGBAdjAlpha(bool adj) { currState().sRGBAdjAlpha = adj; }
  void setColorXorMask(color_t mask) { currState().colorXorMask = mask; }

  // background color
  void setBackgroundColor(const Color& c) { bgColor = c; }
  Color backgroundColor() const { return bgColor; }

  // text measurement
  real textBounds(real x, real y, const char* start, const char* end = NULL, Rect* boundsout = NULL);
  int textGlyphPositions(real x, real y, const char* start, const char* end, std::vector<Rect>* pos_out);
  std::string textBreakLines(const char* start, const char* end, float width, int maxLines);
  real textLineHeight();

  // non-static because it needs sRGB
  NVGcolor colorToNVGColor(Color c, float alpha = -1);

  // static methods
  static void invalidateImage(int handle, int len);
  static void initFontStash(int flags, int pad = 4, float pixdist = 32.0f);
  static bool loadFont(const char* name, const char* filename, Painter* painter = NULL);
  static bool loadFontMem(const char* name, unsigned char* data, int len, Painter* painter = NULL);
  static bool addFallbackFont(const char* name, const char* fallback, Painter* painter = NULL);

private:
  void resolveFont();
};

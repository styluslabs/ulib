#pragma once

#include <vector>
#include <string>
#include <utility>
#include "geom.h"
#include "color.h"
#include "nanovg.h"

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
  enum FontStyle { StyleNormal = 0, StyleItalic, StyleOblique };
  enum FontCapitalization { MixedCase = 0, SmallCaps, AllUppercase, AllLowercase, Capitalize };

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
  int textGlyphPositions(real x, real y, const char* start, const char* end, std::vector<GlyphPosition>* pos_out);
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

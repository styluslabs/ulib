#include <string.h>
#include <mutex>
#include <memory>

#include "platformutil.h"
#include "painter.h"
#include "path2d.h"
#include "image.h"

#include "fontstash.h"
#include "nanovg_sw.h"
#ifndef NO_PAINTER_GL
typedef uint32_t GLuint;
#include "nanovg_vtex.h"
#include "nanovg_gl_utils.h"
#include "nanovg_sw_utils.h"
#endif


Painter* Painter::cachingPainter = NULL;
FONScontext* Painter::fontStash = NULL;
std::string Painter::defaultFontFamily;
int Painter::maxCachedBytes = 512*1024*1024;

// NVGcontext for bounds calculation only
static int nullvg__renderCreate(void* uptr) { return 1; }

static NVGcontext* nvgNullCreate(int flags)
{
  NVGparams params;
  memset(&params, 0, sizeof(params));
  params.renderCreate = nullvg__renderCreate;
  params.flags = flags;
  return nvgCreateInternal(&params);
}

static void nvgNullDelete(NVGcontext* ctx)
{
  nvgDeleteInternal(ctx);
}

std::unique_lock<std::mutex> getFonsLock(Painter* p)
{
  static std::mutex fonsMutex;
  if(p->createFlags & Painter::PRIVATE_FONTS || p->createFlags & Painter::NO_TEXT || !(p->createFlags & Painter::MULTITHREAD))
    return std::unique_lock<std::mutex>();
  return std::unique_lock<std::mutex>(fonsMutex);
}

Painter::Painter(int flags, Image* image)
{
  int nvgFlags = NVG_AUTOW_DEFAULT;
  nvgFlags |= (flags & NO_TEXT) ? NVG_NO_FONTSTASH : 0;
  nvgFlags |= (flags & PRIVATE_FONTS) ? 0 : NVG_NO_FONTSTASH;
  nvgFlags |= (flags & SRGB_AWARE) ? NVG_SRGB : 0;
  bool sharefons = !(flags & PRIVATE_FONTS || flags & NO_TEXT);
  if(sharefons && fontStash && fonsInternalParams(fontStash)->flags & FONS_SDF)
    nvgFlags |= NVG_SDF_TEXT;

  if((flags & PAINT_MASK) == PAINT_NULL)
    vg = nvgNullCreate(nvgFlags);
#ifndef NO_PAINTER_GL
  else if((flags & PAINT_MASK) == PAINT_GL) {
    // caching painter is assumed to have same lifetime as GL context
    //nvgFlags |= (flags & CACHE_IMAGES) ? NVGL_DELETE_NO_GL : 0;
    nvgFlags |= (flags & PAINT_DEBUG_GL) ? NVGL_DEBUG : 0;
    vg = nvglCreate(nvgFlags);
  }
#endif
  else { //if(flags & PAINT_MASK == PAINT_SW)
    nvgFlags |= (flags & SW_NO_XC) ? 0 : NVGSW_PATHS_XC;
    vg = nvgswCreate(nvgFlags);
  }

  if(sharefons)
    nvgSetFontStash(vg, fontStash);

  if(flags & SW_BLIT_GL)
    swBlitter = nvgswuCreateBlitter();

  if(flags & CACHE_IMAGES && !cachingPainter)
    cachingPainter = this;

  createFlags = flags;
  setTarget(image);
  painterStates.reserve(32);
  painterStates.emplace_back();
  reset();
}

Painter::Painter(NVGcontext* _vg, Image* image) : vg(_vg)
{
  setTarget(image);
  painterStates.reserve(32);
  painterStates.emplace_back();
  reset();
}

void Painter::setTarget(Image* image)
{
  targetImage = image;
  deviceRect = image ? Rect::wh(image->width, image->height) : Rect();
#ifndef NO_PAINTER_GL
  if(image && usesGPU())
    nvgFB = nvgluCreateFramebuffer(vg, image->width, image->height, NVGLU_NO_NVG_IMAGE | (sRGB() ? NVG_IMAGE_SRGB : 0));
  else if(nvgFB) {
    nvgluDeleteFramebuffer(nvgFB);
    nvgFB = NULL;
  }
#endif
}

Painter::~Painter()
{
  if(swBlitter) {
    nvgswuDeleteBlitter(swBlitter);
    delete targetImage;
  }
  if(!vg) {}
  else if(!nvgInternalParams(vg)->userPtr)
    nvgNullDelete(vg);
  else if(usesGPU())
    nvglDelete(vg);
  else
    nvgswDelete(vg);
#ifndef NO_PAINTER_GL
  if(nvgFB)
    nvgluDeleteFramebuffer(nvgFB);
#endif
  if(this == cachingPainter)
    cachingPainter = NULL;
}

void Painter::reset()
{
  setTransform(Transform2D());
  setOpacity(1);
  setFillBrush(Brush(Color::BLACK));
  // stroke
  setStrokeBrush(Brush(Color::NONE));
  setStrokeWidth(1);
  setMiterLimit(0);
  setStrokeCap(FlatCap);
  setStrokeJoin(MiterJoin);
  setVectorEffect(NoVectorEffect);
  //Dim strokeDashOffset = 0;
  //std::vector<Dim> strokeDashes;
  // font
  setFontSize(12);
  setFontFamily(defaultFontFamily.c_str());
  //int fontWeight = 400;
  //FontStyle fontStyle = StyleNormal;
  //FontCapitalization fontCaps = MixedCase;
  // other
  setCompOp(CompOp_SrcOver);
  setAntiAlias(true);
  setTextAlign(AlignLeft | AlignBaseline);
  setClipRect(Rect());  // clear scissor
}

void Painter::beginFrame(real pxRatio)
{
  ASSERT(deviceRect.isValid());
  int fbWidth = deviceRect.width(), fbHeight = deviceRect.height();

  // clearing textures added to support Write documents w/ large number of images (e.g. from PDF conversion)
  if(cachedBytes > maxCachedBytes) {
    for(int h : imgHandles)
      nvgDeleteImage(vg, h);
    imgHandles.clear();
    cachedBytes = 0;
  }

#ifndef NO_PAINTER_SW
  if(swBlitter) {
    bool sizeChanged = (fbWidth != swBlitter->width || fbHeight != swBlitter->height);
    if(!targetImage || sizeChanged) {
      delete targetImage;
      targetImage = new Image(fbWidth, fbHeight);
    }
  }
  // have to set at beginning of frame since each tile has separate command lists for multithreaded case
  if(targetImage && !usesGPU())
    nvgswSetFramebuffer(vg, targetImage->bytes(), targetImage->width, targetImage->height, 0, 8, 16, 24);
#endif
  nvgBeginFrame(vg, fbWidth, fbHeight, pxRatio);
  // nvgBeginFrame resets nanovg state stack, so reset ours too
  ASSERT(painterStates.size() == 1);
  painterStates.resize(1);
  reset();
  if(nvgFB) {
    translate(0, fbHeight);
    scale(1, -1);
  }
}

void Painter::endFrame()
{
  bool glRender = usesGPU();
  // moved from Painter::beginFrame - nanovg does not make any GL calls until endFrame, so neither should we
#ifdef NO_PAINTER_GL
  nvgEndFrame(vg);
#else
  int prevFBO = -1;
  if(nvgFB)
    prevFBO = nvgluBindFramebuffer(nvgFB);

  // setup GL state
  if(glRender)
    nvgluSetViewport(0, 0, int(deviceRect.width()), int(deviceRect.height()));

  // don't clear when drawing to screen to support partial redraw
  if(targetImage && bgColor.isValid()) {
    if(glRender)
      nvgluClear(colorToNVGColor(bgColor));
    else
      targetImage->fill(bgColor.argb());
  }

  nvgEndFrame(vg);

  if(nvgFB) {
    nvgluReadPixels(nvgFB, targetImage->bytes());
    nvgluBindFBO(prevFBO);
  }
#endif
//#ifndef NO_PAINTER_SW
//  if(targetImage && !glRender)
//    nvgswSetFramebuffer(vg, NULL, targetImage->width, targetImage->height, 0, 8, 16, 24);
//#endif
}

void Painter::blitImageToScreen(Rect dirty, bool blend)
{
  if(!swBlitter || !targetImage) return;
  int fbWidth = targetImage->width, fbHeight = targetImage->height;
  nvgswuSetBlend(blend);
  nvgswuBlit(swBlitter, targetImage->bytes(), fbWidth, fbHeight,
      int(dirty.left), int(dirty.top), int(dirty.width()), int(dirty.height()));
}

bool Painter::usesGPU() const
{
  return nvgInternalParams(vg)->flags & NVG_IS_GPU;
}

bool Painter::sRGB() const
{
  return nvgInternalParams(vg)->flags & NVG_SRGB;
}

#ifdef FONS_WPATH
#include "fileutil.h"
#define FONS_PATH(x) ((const char*)PLATFORM_STR(x))
#else
#define FONS_PATH(x) (x)
#endif

bool Painter::loadFont(const char* name, const char* filename, Painter* painter)
{
  if(painter) return nvgCreateFont(painter->vg, name, FONS_PATH(filename)) != -1;
  if(!fontStash) return false;
  if(fonsAddFont(fontStash, name, FONS_PATH(filename)) == -1) return false;
  if(defaultFontFamily.empty()) defaultFontFamily = name;
  return true;
}

// fallback is name of already loaded font, not filename!
bool Painter::loadFontMem(const char* name, unsigned char* data, int len, Painter* painter)
{
  if(painter) return nvgCreateFontMem(painter->vg, name, data, len, 0) != -1;
  if(!fontStash) return false;
  if(fonsAddFontMem(fontStash, name, data, len, 0) == -1) return false;
  if(defaultFontFamily.empty()) defaultFontFamily = name;
  return true;
}

bool Painter::addFallbackFont(const char* name, const char* fallback, Painter* painter)
{
  if(painter)
    return nvgAddFallbackFont(painter->vg, name, fallback);
  if(fontStash)
    return fonsAddFallbackFont(fontStash,
        fonsGetFontByName(fontStash, name), fonsGetFontByName(fontStash, fallback));
  return false;
}

static void fonsDeleter(FONScontext* fons) { fonsDeleteInternal(fons); };

void Painter::initFontStash(int flags, int pad, float pixdist)
{
  static std::unique_ptr<FONScontext, decltype(&fonsDeleter)> dfltFons(NULL, fonsDeleter);

  FONSparams fonsParams = {0};
  fonsParams.sdfPadding = pad;
  fonsParams.sdfPixelDist = pixdist;
  fonsParams.flags = flags | FONS_ZERO_TOPLEFT;  //FONS_ZERO_TOPLEFT | FONS_DELAY_LOAD | FONS_SUMMED;
  fonsParams.atlasBlockHeight = 0;
  dfltFons.reset(fonsCreateInternal(&fonsParams));
  Painter::fontStash = dfltFons.get();
}

void Painter::save()
{
  nvgSave(vg);
  painterStates.emplace_back(currState());
}

void Painter::restore()
{
  painterStates.pop_back();
  nvgRestore(vg);
}

// transforms

void Painter::setTransform(const Transform2D& tf)
{
  nvgResetTransform(vg);
  transform(tf);
}

void Painter::transform(const Transform2D& tf)
{
  nvgTransform(vg, tf.m[0], tf.m[1], tf.m[2], tf.m[3], tf.m[4], tf.m[5]);
}

Transform2D Painter::getTransform() const
{
  float m[6];
  nvgCurrentTransform(vg, &m[0]);
  return Transform2D(m[0], m[1], m[2], m[3], m[4], m[5]);
}

// PainterState.clipBounds may not be as tight as nanovg's actual scissor (if transform has any rotation) and
//  is always in global coordinate system, whereas rect passed to (set)ClipRect is in local coordinates; clip
//  can only be saved and restored with full save()/restore()
void Painter::setClipRect(const Rect& r)
{
  currState().clipBounds = Rect();
  nvgResetScissor(vg);
  clipRect(r);
}

void Painter::clipRect(Rect r)
{
  if(r.isValid()) {
    Transform2D tf = getTransform();
    // clip rects need to be aligned to pixel boundaries - I think we should do this in nanovg-2 instead
    if(!tf.isRotating())
      r = tf.inverse().mapRect(tf.mapRect(r).round());
    Rect curr = currState().clipBounds;
    currState().clipBounds = curr.isValid() ? curr.rectIntersect(tf.mapRect(r)) : tf.mapRect(r);
    if(createFlags & ALIGN_SCISSOR) {
      r = currState().clipBounds;
      nvgResetTransform(vg);
      nvgScissor(vg, r.left, r.top, r.width(), r.height());
      transform(tf);
    }
    else
      nvgIntersectScissor(vg, r.left, r.top, r.width(), r.height());
  }
}

// consider adding nvgCurrentScissor() to nanovg (won't include rotation however)
Rect Painter::getClipBounds() const
{
  // clipBounds is in global coords, so transform back to local coords
  return currState().clipBounds.isValid() ? getTransform().inverse().mapRect(currState().clipBounds) : Rect();
}

// drawing

void Painter::beginPath()
{
  nvgBeginPath(vg);
}

void Painter::endPath()
{
  if(!currState().fillBrush.isNone())
    nvgFill(vg);
  if(!currState().strokeBrush.isNone()) {
    // compensate for multiplication by scale done in nvgStroke() ... w/ nanovg-2 we can now just pass width = 0
    if(currState().strokeEffect == NonScalingStroke)
      nvgStrokeWidth(vg, currState().strokeWidth/getTransform().avgScale());
    nvgStroke(vg);
    if(currState().strokeEffect == NonScalingStroke)
      nvgStrokeWidth(vg, currState().strokeWidth);  // restore
  }
}

// TODO: can we decouple Painter and Path better?
void Painter::drawPath(const Path2D& path)
{
  nvgFillRule(vg, path.fillRule == Path2D::EvenOddFill ? NVG_EVENODD : NVG_NONZERO);
  beginPath();
  for(int ii = 0; ii < path.size(); ++ii) {
    if(path.command(ii) == Path2D::MoveTo)
      nvgMoveTo(vg, path.point(ii).x, path.point(ii).y);
    else if(path.command(ii) == Path2D::LineTo)
      nvgLineTo(vg, path.point(ii).x, path.point(ii).y);
    else if(path.command(ii) == Path2D::CubicTo) {
      nvgBezierTo(vg, path.point(ii).x, path.point(ii).y,
          path.point(ii+1).x, path.point(ii+1).y, path.point(ii+2).x, path.point(ii+2).y);
      ii += 2;  // skip control points
    }
    else if(path.command(ii) == Path2D::QuadTo) {
      nvgQuadTo(vg, path.point(ii).x, path.point(ii).y, path.point(ii+1).x, path.point(ii+1).y);
      ++ii;  // skip control point
    }
    else if(path.command(ii) == Path2D::ArcTo) {
      real cx = path.point(ii).x;
      real cy = path.point(ii).y;
      real rx = path.point(ii+1).x;
      //Dim ry = path.point(ii+1).y;
      real start = path.point(ii+2).x;
      real sweep = path.point(ii+2).y;
      //if(rx != ry)
      //  logError("nanovg only supports circular arcs!");
      // nvgArc does not actually close the current subpath, so should be ok
      nvgArc(vg, cx, cy, rx, start, start + sweep, sweep < 0 ? NVG_CCW : NVG_CW);
      ii += 2;  // skip control points
    }
  }
  endPath();
  nvgFillRule(vg, NVG_NONZERO);
}

void Painter::drawLine(const Point& a, const Point& b)
{
  beginPath();
  nvgMoveTo(vg, a.x, a.y);
  nvgLineTo(vg, b.x, b.y);
  endPath();
}

void Painter::drawRect(Rect rect)
{
  beginPath();
  nvgRect(vg, rect.left, rect.top, rect.width(), rect.height());
  endPath();
}

void Painter::fillRect(Rect rect, Color c)
{
  nvgFillColor(vg, colorToNVGColor(c));
  beginPath();
  nvgRect(vg, rect.left, rect.top, rect.width(), rect.height());
  nvgFill(vg);
  setFillBrush(currState().fillBrush); // restore brush
}

void Painter::drawImage(const Rect& dest, const Image& image, Rect src, int flags)
{
  // we are using the fact that a nanovg instance assigns monotonically increasing image (texture) handles
  int handle;
  if(this == cachingPainter && !imgHandles.empty() && image.painterHandle >= imgHandles.front())
    handle = image.painterHandle;
  else {
    //flags |= NVG_IMAGE_GENERATE_MIPMAPS; ... just seemed to make things more blurry
    flags |= sRGB() ? NVG_IMAGE_SRGB : 0;
    flags |= (this != cachingPainter) ? NVG_IMAGE_DISCARD : 0;
    auto bytes = image.bytesOnce();
    handle = nvgCreateImageRGBA(vg, image.width, image.height, flags, bytes);
    if(bytes != image.data) free(bytes);
    if(this == cachingPainter) {
      image.painterHandle = handle;
      imgHandles.push_back(handle);
      cachedBytes += image.dataLen();
    }
  }

  if(!src.isValid())
    src = Rect::ltwh(0, 0, image.width, image.height);
  real sx = dest.width()/src.width(), sy = dest.height()/src.height();
  real ex = image.width*sx, ey = image.height*sy;
  real ox = dest.left - src.left*sx, oy = dest.top - src.top*sy;
  NVGpaint imgpaint = nvgImagePattern(vg, ox, oy, ex, ey, 0, handle, 1.0f);
  nvgBeginPath(vg);
  nvgRect(vg, dest.left, dest.top, dest.width(), dest.height());
  nvgFillPaint(vg, imgpaint);
  nvgFill(vg);
  // restore fill settings
  setFillBrush(currState().fillBrush);
}

void Painter::invalidateImage(int handle, int len)
{
  if(cachingPainter && !cachingPainter->imgHandles.empty() && handle >= cachingPainter->imgHandles.front()) {
    cachingPainter->cachedBytes -= len;
    nvgDeleteImage(cachingPainter->vg, handle);
  }
}

void Painter::setAtlasTextThreshold(float thresh)
{
  atlasTextThresh = thresh;
  nvgAtlasTextThreshold(vg, thresh);
}

// returns new x position
real Painter::drawText(real x, real y, const char* start, const char* end)
{
  auto lock = getFonsLock(this);
  bool faux = currState().fauxItalic || currState().fauxBold;
  if(currState().strokeBrush.isNone() && !faux)
    return nvgText(vg, x, y, start, end);
  float weight = (currState().fontWeight - 400)/300.0;
#ifdef FONS_SDF
  // support for drawing faux bold and text w/ halo using SDF
  if(currState().fontPixelSize*getTransform().avgScale() < atlasTextThresh && !currState().fauxItalic) {
    real adv = 0;
    float strokeadj = 0;  // default 0 for StrokeOuter
    if(!currState().strokeBrush.isNone()) {
      if(currState().strokeAlign == StrokeCenter) strokeadj = currState().strokeWidth/2;
      else if(currState().strokeAlign == StrokeInner) strokeadj = currState().strokeWidth;
      nvgFontBlur(vg, currState().strokeWidth + weight - strokeadj);
      // nvgText uses fill
      nvgFillColor(vg, colorToNVGColor(currState().strokeBrush.color()));
      adv = nvgText(vg, x, y, start, end);
      setFillBrush(currState().fillBrush);  // restore
    }
    if(!currState().fillBrush.isNone()) {
      nvgFontBlur(vg, weight - strokeadj);
      adv = nvgText(vg, x, y, start, end);
    }
    nvgFontBlur(vg, 0);
    return adv;
  }
#endif
  // have to render as paths
  if(faux) save();  //nvgSave(vg);
  if(currState().fauxBold && currState().strokeBrush.isNone())
    setStroke(currState().fillBrush, currState().fontPixelSize*0.05*weight, FlatCap, MiterJoin);
  if(currState().fauxItalic) {
    // y translation must be applied before skew
    nvgTranslate(vg, -0.1*currState().fontPixelSize, y);
    nvgSkewX(vg, -13*M_PI/180);
    y = 0;
  }
  real nextx = nvgTextAsPaths(vg, x, y, start, end);
  if(!currState().strokeBrush.isNone() && currState().strokeAlign == StrokeOuter) {
    nvgStroke(vg);
    if(!currState().fillBrush.isNone())
      nvgFill(vg);
  }
  else
    endPath();
  if(faux) restore();  //nvgRestore(vg);
  return nextx;
}

// Note that text measurement depends on current painter state and thus cannot be static methods
// return X advance; if passed, boundsout is united with the text bounding rect (if boundsout is an
//  invalid rect, this will just set it equal to the text rect)
// TODO: adjust text bounds for faux italic/bold!
real Painter::textBounds(real x, real y, const char* s, const char* end, Rect* boundsout)
{
  auto lock = getFonsLock(this);
  float bounds[4];
  real advX = nvgTextBounds(vg, x, y, s, end, &bounds[0]);
  if(boundsout)
    boundsout->rectUnion(getTransform().mapRect(Rect::ltrb(bounds[0], bounds[1], bounds[2], bounds[3])));
  return advX;
}

int Painter::textGlyphPositions(real x, real y, const char* start, const char* end, std::vector<Rect>* pos_out)
{
  auto lock = getFonsLock(this);
  size_t len = end ? end - start : strlen(start);
  NVGglyphPosition* positions = new NVGglyphPosition[len];
  int npos = nvgTextGlyphPositions(vg, x, y, start, end, positions, len);
  for(int ii = 0; ii < npos; ++ii)
    pos_out->push_back(Rect::ltrb(positions[ii].minx, y, positions[ii].maxx, y));
  delete[] positions;
  return npos;
}

std::string Painter::textBreakLines(const char* start, const char* end, float width, int maxLines)
{
  std::vector<FONStextRow> rows(maxLines);
  int nlines = nvgTextBreakLines(vg, start, end, width, rows.data(), maxLines);
  if(nlines <= 0) return "";
  std::string res(rows[0].start, rows[0].end - rows[0].start);
  for(int ii = 1; ii < nlines; ++ii) {
    res.append("\n").append(rows[ii].start, rows[ii].end - rows[ii].start);
  }
  return res;
}

real Painter::textLineHeight()
{
  auto lock = getFonsLock(this);
  float lineh = 0;
  nvgTextMetrics(vg, NULL, NULL, &lineh);
  return lineh;
}

void Painter::setTextAlign(TextAlign align)
{
  nvgTextAlign(vg, align);
}

bool Painter::setAntiAlias(bool antialias)
{
  // nanovg state includes antialias, but it can't be read back, so just keep track of it in painterStates
  bool prev = currState().antiAlias;
  currState().antiAlias = antialias;
  nvgShapeAntiAlias(vg, antialias);
  return prev;
}

void Painter::setCompOp(CompOp op)
{
  if(op == CompOp_Clear)
    nvgGlobalCompositeBlendFunc(vg, NVG_ZERO, NVG_ZERO);
  else if(op < NOT_SUPPORTED)
    nvgGlobalCompositeOperation(vg, op);
}

void Painter::setOpacity(real opacity)
{
  float a = opacity;
  currState().globalAlpha = a;
  // this is an ugly hack, and assumes transparent color is dark
  if(a < 1.0f && a > 0.0f && sRGB() && currState().sRGBAdjAlpha)
    a = 1.0f - pow(1.0f - a, 2.2f);
  nvgGlobalAlpha(vg, a);
}

// fill/stroke paint, font

static ColorF sRGBtoLinear(Color c)
{
  ColorF res;
  res.r = nvgSRGBtoLinear(c.red());
  res.g = nvgSRGBtoLinear(c.green());
  res.b = nvgSRGBtoLinear(c.blue());
  res.a = c.alphaF();
  return res;
}

static Color linearTosRGB(ColorF c)
{
  static float ig = 1/2.31f;
  return Color::fromFloat(powf(c.r, ig), powf(c.g, ig), powf(c.b, ig), c.a);
}

static ColorF colorInterpF(ColorF a, ColorF b, float u)
{
  ColorF res;
  res.r = (1-u)*a.r + u*b.r;
  res.g = (1-u)*a.g + u*b.g;
  res.b = (1-u)*a.b + u*b.b;
  res.a = (1-u)*a.a + u*b.a;
  return res;
}

NVGpaint Painter::getGradientPaint(const Gradient* grad)
{
  if(grad->stops().empty())
    return nvgLinearGradient(vg, 0, 0, 1, 0, {0}, {0});

  NVGpaint paint;
  // we could add a flag for SW renderer to support sRGB interp directly, but match GL renderer for now
  bool xinterp = (sRGB() ? Gradient::LinearColorInterp : Gradient::sRGBColorInterp) != grad->colorInterp;
  bool alphaonly = grad->stops().front().second.opaque() == grad->stops().back().second.opaque();
  bool multi = grad->stops().size() > 2 || (grad->stops().size() > 1 && xinterp && !alphaonly);
  // nanovg gradients basically assume param scale roughly matches actual size of object, in particular
  //  clamping feather to be >= 1.0 and using 1e5 offset for linear grad
  real scale = std::max(1000.0, std::max(deviceRect.width(), deviceRect.height()));
  GradientStop stop1 = multi ? GradientStop(0, Color::BLACK) : grad->stops().front();
  GradientStop stop2 = multi ? GradientStop(1, Color::WHITE) : grad->stops().back();
  NVGcolor cin = colorToNVGColor(stop1.second);
  NVGcolor cout = colorToNVGColor(stop2.second);
  if(grad->type == Gradient::Linear) {
    const Gradient::LinearGradCoords& g = grad->coords.linear;
    // we need s1 != s2 in order to specify direction of gradient
    real s1 = stop1.first;
    real s2 = stop2.first == s1 ? s1 + 0.1/scale : stop2.first;
    real x1 = scale * ( g.x1 + s1*(g.x2 - g.x1) );
    real y1 = scale * ( g.y1 + s1*(g.y2 - g.y1) );
    real x2 = scale * ( g.x1 + s2*(g.x2 - g.x1) );
    real y2 = scale * ( g.y1 + s2*(g.y2 - g.y1) );
    paint = nvgLinearGradient(vg, x1, y1, x2, y2, cin, cout);
  }
  else if(grad->type == Gradient::Radial) {
    const Gradient::RadialGradCoords& g = grad->coords.radial;
    // nanovg doesn't support offset radial gradients (fx != cx or fy != cy)
    real rin = scale * stop1.first*g.radius;  //Point(g.fx - g.cx, g.fy - g.cy).dist();
    real rout = scale * stop2.first*g.radius;
    paint = nvgRadialGradient(vg, scale * g.cx, scale * g.cy, rin, rout, cin, cout);
  }
  else if(grad->type == Gradient::Box) {
    const Gradient::BoxGradCoords& g = grad->coords.box;
    paint = nvgBoxGradient(vg, g.x, g.y, g.w, g.h, g.r, g.feather, cin, cout);
  }

  if(multi) {
    int handle = this == cachingPainter ? grad->painterHandle.handle : -1;
    if(handle <= 0) {  //imgHandleBase
      auto& stops = grad->stops();
      // in general, we can't premultiply in sRGB space, so we don't use premultiplied texture
      int flags = (this != cachingPainter ? NVG_IMAGE_DISCARD : 0) | (sRGB() ? NVG_IMAGE_SRGB : 0);
      if(grad->colorInterp == Gradient::LinearColorInterp) {
        size_t w = 256;
        std::vector<color_t> img(w);
        real f = 0, fstep = 1.0/(w - 1);
        ColorF c0 = sRGBtoLinear(stops[0].second);
        ColorF c1 = sRGBtoLinear(stops[1].second);
        for (size_t pidx = 0, sidx = 0; pidx < w; ++pidx) {
          while (f > stops[sidx+1].first && sidx < stops.size() - 2) {
            ++sidx;
            c0 = sRGBtoLinear(stops[sidx].second);
            c1 = sRGBtoLinear(stops[sidx+1].second);
          }
          ColorF c = colorInterpF(c0, c1, (f - stops[sidx].first)/(stops[sidx+1].first - stops[sidx].first));
          img[pidx] = linearTosRGB(c).color;  // this is why we can't use nvgMultiGradient for this case
          f += fstep;
        }
        handle = nvgCreateImageRGBA(vg, int(w), 1, flags, (unsigned char*)img.data());
      }
      else {
        std::vector<float> fstops;
        std::vector<NVGcolor> colors;
        fstops.reserve(stops.size());
        colors.reserve(stops.size());
        for(auto& stop : stops) {
          fstops.push_back(float(stop.first));
          colors.push_back(colorToNVGColor(stop.second));
        }
        handle = nvgMultiGradient(vg, flags, fstops.data(), colors.data(), fstops.size());
      }
      if(this == cachingPainter)
        grad->painterHandle.handle = handle;
    }
    paint.image = handle;
  }

  float xform[6];
  if(grad->type != Gradient::Box) {
    nvgTransformScale(xform, 1/scale, 1/scale);
    nvgTransformMultiply(paint.xform, xform);
  }
  if(grad->coordinateMode() == Gradient::ObjectBoundingMode && grad->objectBBox.isValid()) {
    nvgTransformScale(xform, grad->objectBBox.width(), grad->objectBBox.height());
    nvgTransformMultiply(paint.xform, xform);
    nvgTransformTranslate(xform, grad->objectBBox.left, grad->objectBBox.top);
    nvgTransformMultiply(paint.xform, xform);
  }
  return paint;
}

void Painter::setFillBrush(const Brush& b)
{
  currState().fillBrush = b;
  if(b.gradient())
    nvgFillPaint(vg, getGradientPaint(b.gradient()));
  else
    nvgFillColor(vg, colorToNVGColor(b.color()));
}

void Painter::setStrokeBrush(const Brush& b)
{
  currState().strokeBrush = b;
  if(b.gradient())
    nvgStrokePaint(vg, getGradientPaint(b.gradient()));
  else
    nvgStrokeColor(vg, colorToNVGColor(b.color()));
}

void Painter::setStrokeCap(CapStyle cap) { currState().strokeCap = cap;  nvgLineCap(vg, cap); }
void Painter::setStrokeJoin(JoinStyle join) { currState().strokeJoin = join;  nvgLineJoin(vg, join); }
void Painter::setMiterLimit(real lim) { currState().strokeMiterLimit = lim;  nvgMiterLimit(vg, lim); }
void Painter::setStrokeWidth(real w) { currState().strokeWidth = w;  nvgStrokeWidth(vg, w); }
void Painter::setDashArray(float* dashes) { currState().strokeDashes = dashes;  nvgDashArray(vg, dashes); }
void Painter::setDashOffset(real offset) { currState().strokeDashOffset = offset;  nvgDashOffset(vg, offset); }

void Painter::setStroke(const Brush& b, real w, CapStyle cap, JoinStyle join)
{
  setStrokeBrush(b);
  setStrokeWidth(w);
  setStrokeCap(cap);
  setStrokeJoin(join);
}

void Painter::resolveFont()
{
  auto lock = getFonsLock(this);
  bool italic = currState().fontStyle != StyleNormal;
  bool bold  = currState().fontWeight > 550;
  int res = -1;
  if(bold && italic) {
    res = nvgFontFaceId(vg, currState().boldItalicFontId);
    bold = italic = res < 0;  // still need bold and italic?
  }
  if(res < 0 && bold) {
    res = nvgFontFaceId(vg, currState().boldFontId);
    bold = res < 0;  // still need (faux) bold?
  }
  if(res < 0 && italic) {
    res = nvgFontFaceId(vg, currState().italicFontId);
    italic = res < 0;  // still need (faux) italic?
  }
  if(res < 0)
    nvgFontFaceId(vg, currState().fontId);
  currState().fauxBold = bold;
  currState().fauxItalic = italic;
}

// nanovg won't render or calc bounds if unknown font is set; don't set and return false if font not found
bool Painter::setFontFamily(const char* family)
{
  int fontid = nvgFindFont(vg, family);
  if(fontid == -1)
    return false;
  if(fontid == currState().fontId)
    return true;
  std::string fam(family);
  currState().fontId = fontid;
  currState().boldFontId = nvgFindFont(vg, (fam + "-bold").c_str());
  currState().italicFontId = nvgFindFont(vg, (fam + "-italic").c_str());
  currState().boldItalicFontId = nvgFindFont(vg, (fam + "-bold-italic").c_str());
  resolveFont();
  return true;
}

void Painter::setFontWeight(int weight)
  { if(currState().fontWeight != weight) { currState().fontWeight = weight; resolveFont(); } }

void Painter::setFontStyle(FontStyle style)
  { if(currState().fontStyle != style) { currState().fontStyle = style; resolveFont(); } }

void Painter::setFontSize(real px) { currState().fontPixelSize = px;  nvgFontSize(vg, px);}  //pt * 2.08

void Painter::setLetterSpacing(real px) { currState().letterSpacing = px;  nvgTextLetterSpacing(vg, px);}

// sRGB: see nanovg-2 readme for more, but in short, images and colors (i.e. #XXXXXX) are always in sRGB
//  space, but blending should happen in linear space, i.e. shaders should work in linear space, so inputs to
//  shaders should be converted to linear (automatic w/ sRGB texture for images, manual for colors as below)
//  and outputs converted back to sRGB (by writing to sRGB framebuffer and enabling GL_FRAMEBUFFER_SRGB)

// nanovg now handles sRGB conversion based on NVG_SRGB flag passed to nvgCreate
NVGcolor Painter::colorToNVGColor(Color color, float alpha)
{
  float a = alpha >= 0 ? alpha : color.alpha() / 255.0f;
  if(a < 1.0f && a > 0.0f && sRGB() && currState().sRGBAdjAlpha)
    a = 1.0f - std::pow(1.0f - a, 2.2f);
  color.color ^= currState().colorXorMask;
  return nvgRGBA(color.red(), color.green(), color.blue(), (unsigned char)(a*255.0f + 0.5f));
}

// Gradient

UniqueHandle::UniqueHandle(const UniqueHandle& other) : handle(-1)
{
#if IS_DEBUG
  if(other.handle >= 0)
    PLATFORM_LOG("UniqueHandle copied!");
#endif
}

void Gradient::invalidate() const
{
  //Painter::invalidateImage(painterHandle.handle, 0);  // gradients not counted toward cachedBytes
  if(painterHandle.handle > 0 && Painter::cachingPainter) {
    nvgDeleteImage(Painter::cachingPainter->vg, painterHandle.handle);
    painterHandle.handle = -1;
  }
}

// Color
// HSV ref: Foley, van Dam, et. al. (1st ed. in C) 13.3

ColorF ColorF::fromHSV(float h, float s, float v, float a)
{
  h = h >= 360 ? 0 : h;
  int h6floor = std::floor(h/60);
  float h6frac = h/60 - h6floor;
  float p = v * (1 - s);
  float q = v * (1 - s*h6frac);
  float t = v * (1 - (s * (1 - h6frac)));
  float r = 0, g = 0, b = 0;
  switch(h6floor) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    case 5: r = v; g = p; b = q; break;
  }
  return {r, g, b, a};
}

float ColorF::hueHSV() const
{
  //int r = red(), g = green(), b = blue();
  float max = valueHSV();
  float min = std::min(std::min(r, g), b);
  float delta = max - min;
  float hue = 0;
  if(max == 0 || delta == 0)  // i.e., if sat == 0
    hue = 0;
  else if(max == r)
    hue = (60*(g - b))/delta;
  else if(max == g)
    hue = 120 + (60*(b - r))/delta;
  else if(max == b)
    hue = 240 + (60*(r - g))/delta;
  if(hue < 0)
    hue += 360;
  return hue;
}

float ColorF::satHSV() const
{
  float max = valueHSV();
  float min = std::min(std::min(r, g), b);
  return max > 0 ? (max - min)/max : 0;
}

float ColorF::valueHSV() const
{
  return std::max(std::max(r, g), b);
}

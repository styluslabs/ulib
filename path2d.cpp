#include "path2d.h"


void Path2D::fillCommands()
{
  if(!commands.empty() || points.empty())
    return;
  commands.reserve(points.size() + 12);
  commands.push_back(MoveTo);
  for(size_t ii = 1; ii < points.size(); ++ii)
    commands.push_back(LineTo);
}

void Path2D::addPoint(const Point& p, PathCommand cmd)
{
  if(!commands.empty() || (cmd != LineTo && (!points.empty() || cmd != MoveTo))) {
    fillCommands();
    commands.push_back(cmd);
  }
  points.push_back(p);
}

void Path2D::moveTo(const Point& p)
{
  if(!empty()) {
    fillCommands();
    commands.push_back(MoveTo);
  }
  pushPoint(p);
}

void Path2D::lineTo(const Point& p)
{
  if(!isSimple())
    commands.push_back(LineTo);
  pushPoint(p);
}

void Path2D::quadTo(const Point& c, const Point& p)
{
  fillCommands();
  commands.push_back(QuadTo);
  commands.push_back(QuadTo);
  pushPoint(c);
  pushPoint(p);
}

void Path2D::cubicTo(const Point& c1, const Point& c2, const Point& p)
{
  fillCommands();
  commands.push_back(CubicTo);
  commands.push_back(CubicTo);
  commands.push_back(CubicTo);
  pushPoint(c1);
  pushPoint(c2);
  pushPoint(p);
}

void Path2D::closeSubpath()
{
  if(points.empty())
    return;
  if(commands.empty())
    lineTo(points[0]);
  else {
    for(int ii = commands.size() - 1; ii >= 0; --ii) {
      if(commands[ii] == MoveTo) {
        lineTo(points[ii]);
        break;
      }
    }
  }
}

static void pathArcSegment(Path2D* path, real xc, real yc, real th0, real th1, real rx, real ry, real xRot)
{
  real sinTh = std::sin(xRot);
  real cosTh = std::cos(xRot);
  real a00 =  cosTh * rx;
  real a01 = -sinTh * ry;
  real a10 =  sinTh * rx;
  real a11 =  cosTh * ry;

  real thHalf = 0.5 * (th1 - th0);
  real t = (8.0 / 3.0) * std::sin(thHalf * 0.5) * std::sin(thHalf * 0.5) / std::sin(thHalf);
  real x1 = xc + std::cos(th0) - t * std::sin(th0);
  real y1 = yc + std::sin(th0) + t * std::cos(th0);
  real x3 = xc + std::cos(th1);
  real y3 = yc + std::sin(th1);
  real x2 = x3 + t * std::sin(th1);
  real y2 = y3 - t * std::cos(th1);

  path->cubicTo(a00 * x1 + a01 * y1, a10 * x1 + a11 * y1,
                a00 * x2 + a01 * y2, a10 * x2 + a11 * y2,
                a00 * x3 + a01 * y3, a10 * x3 + a11 * y3);
}

// Arcs greatly complicate many calculations; one option would be to store arc data in separate
//  vector but use cubics for calculations
// Maybe move arc handling to a derived class (e.g. ArcPreservingPath)
// Some things that still have to be modified to support arcs: iterating over points
bool Path2D::PRESERVE_ARCS = false;

void Path2D::addArc(real cx, real cy, real rx, real ry, real startRad, real sweepRad, real xAxisRotRad)
{
  fillCommands();
  if(PRESERVE_ARCS && rx == ry) {
    commands.push_back(ArcTo);
    commands.push_back(ArcTo);
    commands.push_back(ArcTo);
    pushPoint(Point(cx, cy));
    pushPoint(Point(rx, ry));
    pushPoint(Point(startRad, sweepRad));
  }
  else {
    int n_segs = std::ceil(std::abs(sweepRad / (M_PI * 0.5 + 0.001)));
    for(int i = 0; i < n_segs; i++) {
      pathArcSegment(this, cx/rx, cy/ry, startRad + i * sweepRad / n_segs,
          startRad + (i + 1) * sweepRad / n_segs, rx, ry, xAxisRotRad);
    }
  }
}

Path2D& Path2D::addEllipse(real cx, real cy, real rx, real ry)
{
  moveTo(cx + rx, cy);
  addArc(cx, cy, rx, ry, 0, 2*M_PI);
  return *this;
}

Path2D& Path2D::addRect(const Rect& r)
{
  moveTo(r.left, r.top);
  lineTo(r.right, r.top);
  lineTo(r.right, r.bottom);
  lineTo(r.left, r.bottom);
  closeSubpath();
  return *this;
}

void Path2D::connectPath(const Path2D& other)
{
  // no need to call reserve() - vector::insert() will do it
  points.insert(points.end(), other.points.cbegin(), other.points.cend());
  if(other.empty() || (isSimple() && other.isSimple())) {}
  else if(other.isSimple())
    commands.insert(commands.end(), other.size(), LineTo);
  else {
    fillCommands();
    // this assumes other path starts with MoveTo
    commands.push_back(LineTo);
    commands.insert(commands.end(), ++other.commands.cbegin(), other.commands.cend());
  }
}

// alternatives:
// - convert arcs to cubics
// - store arcs as center, direction, (start point or sweep), end point
Point Path2D::getEndPoint(int ii) const
{
  if(isSimple() || commands[ii] != ArcTo)
    return points[ii];
  Point c = points[ii-2];
  real r = points[ii-1].x;
  real angle = points[ii].x + points[ii].y;  // startAngle + sweepAngle
  return Point(c.x + r*cos(angle), c.y + r*sin(angle));
}

Path2D Path2D::toReversed() const
{
  Path2D rev;
  if(isSimple()) {
    rev.points.insert(rev.points.end(), points.crbegin(), points.crend());
    return rev;
  }

  rev.moveTo(currentPosition());
  for(size_t ii = points.size() - 1; ii >= 1; --ii) {
    switch(commands[ii]) {
    case LineTo:
      rev.lineTo(getEndPoint(ii-1));
      break;
    case MoveTo:
      rev.moveTo(getEndPoint(ii-1));
      break;
    case QuadTo:
      rev.quadTo(points[ii-1], getEndPoint(ii-2));
      --ii;
      break;
    case CubicTo:
      rev.cubicTo(points[ii-1], points[ii-2], getEndPoint(ii-3));
      ii -= 2;
      break;
    case ArcTo:
      real start = points[ii].x;
      real sweep = points[ii].y;
      rev.addArc(points[ii-2].x, points[ii-2].y, points[ii-1].x, points[ii-1].y, start+sweep, -sweep);
      ii -= 2;
      break;
    }
  }
  return rev;
}

// from nvg__tesselateBezier
static void flattenBezier(Path2D* out, Point p1, Point p2, Point p3, Point p4, int level = 0)
{
  static constexpr real tessTol = 0.25;

  real x1 = p1.x, y1 = p1.y, x2 = p2.x, y2 = p2.y, x3 = p3.x, y3 = p3.y, x4 = p4.x, y4 = p4.y;
  real dx = x4 - x1;
  real dy = y4 - y1;
  real d2 = std::abs(((x2 - x4) * dy - (y2 - y4) * dx));
  real d3 = std::abs(((x3 - x4) * dy - (y3 - y4) * dx));

  if ((d2 + d3)*(d2 + d3) < tessTol * (dx*dx + dy*dy) || level >= 9)
    out->lineTo(x4, y4); //, type);
  else {
    real x12 = (x1+x2)*0.5;
    real y12 = (y1+y2)*0.5;
    real x23 = (x2+x3)*0.5;
    real y23 = (y2+y3)*0.5;
    real x34 = (x3+x4)*0.5;
    real y34 = (y3+y4)*0.5;
    real x123 = (x12+x23)*0.5;
    real y123 = (y12+y23)*0.5;
    real x234 = (x23+x34)*0.5;
    real y234 = (y23+y34)*0.5;
    real x1234 = (x123+x234)*0.5;
    real y1234 = (y123+y234)*0.5;

    flattenBezier(out, Point(x1,y1), Point(x12,y12), Point(x123,y123), Point(x1234,y1234), level+1);
    flattenBezier(out, Point(x1234,y1234), Point(x234,y234), Point(x34,y34), Point(x4,y4), level+1);
  }
}

Path2D Path2D::toFlat() const
{
  if(isSimple())
    return *this;

  Path2D flat;
  for(size_t ii = 0; ii < commands.size(); ++ii) {
    switch(commands[ii]) {
    case LineTo:
      flat.lineTo(points[ii]);
      break;
    case MoveTo:
      flat.moveTo(points[ii]);
      break;
    case QuadTo:
    {
      Point p0 = points[ii-1], p1 = points[ii], p2 = points[ii+1];
      flattenBezier(&flat, p0, Point(p0.x + 2.0/3.0*(p1.x - p0.x), p0.y + 2.0/3.0*(p1.y - p0.y)),
          Point(p2.x + 2.0/3.0*(p1.x - p2.x), p2.y + 2.0/3.0*(p1.y - p2.y)), p2);
      ++ii;
      break;
    }
    case CubicTo:
      flattenBezier(&flat, points[ii-1], points[ii], points[ii+1], points[ii+2]);
      ii += 2;
      break;
    case ArcTo:
      //ASSERT(0, "ArcTo not supported by flatten");
      ii += 2;
      break;
    }
  }
  return flat;
}

std::vector<Path2D> Path2D::getSubPaths() const
{
  if(!empty() && isSimple())
    return {*this};

  std::vector<Path2D> result;
  int m = 0;
  for(int n = 1; n <= size(); ++n) {
    if(n == size() || commands[n] == MoveTo) {
      result.emplace_back();
      result.back().points.insert(result.back().points.begin(), points.begin() + m, points.begin() + n);
      result.back().commands.insert(result.back().commands.begin(), commands.begin() + m, commands.begin() + n);
      m = n;
    }
  }
  return result;
}

// for now, we will assume container object caches bbox
Rect Path2D::getBBox() const
{
  Rect bbox;
  for(const Point& p : points)
    bbox.rectUnion(p);
  return bbox;
}

void Path2D::translate(real x, real y)
{
  for(Point& p : points) {
    p.x += x;
    p.y += y;
  }
}

void Path2D::scale(real sx, real sy)
{
  for(Point& p : points) {
    p.x *= sx;
    p.y *= sy;
  }
}

// TODO: this doesn't work for arcs!
// If transform include non-uniform scale or any shear, arcs have to be converted to beziers!
// One idea is to store center, start point, and stop point, and replace ArcTo with PosArc and NegArc
Path2D& Path2D::transform(const Transform2D& tf)
{
  if(!tf.isIdentity()) {
    for(Point& p : points)
      p = tf.mult(p);
  }
  return *this;
}

// dist from p0 to closest point of path (treating control points as polygon vertices)
real Path2D::distToPoint(const Point& p) const
{
  real dist = (p - point(0)).dist();
  for(int ii = 1; ii < size(); ++ii) {
    if(command(ii) != MoveTo)
      dist = std::min(dist, distToSegment(point(ii-1), point(ii), p));
  }
  return dist;
}

// this assumes even-odd fill and assumes lasso is a single closed polygon
// it only checks to see if all points of this path lie inside lasso, so can return incorrect result for
//  a concave lasso path if this path has line segment partially outside but w/ both endpoints inside
// This problem could be solved by instead checking the first point of this path for inclusion, then checking
//  for any intersection between segments of this path and lasso - w/ even-odd fill rule, any intersection
//  implies that part of path lies outside lasso
// Should use pointInPolygon() from geom.h, but don't want to mess with this now
bool Path2D::isEnclosedBy(const Path2D& lasso) const
{
  for(const Point& p : points) {
    // have to iterate over all points of lasso
    bool abelow = false, bbelow = false, outside = true;
    for(int ii = 0; ii < lasso.size(); ++ii) {
      abelow = bbelow;
      bbelow = lasso.point(ii).y > p.y;
      if(bbelow != abelow && ii > 0) {
        // figure out if the point crosses our ray (in the +x direction from p)
        Point a = lasso.point(ii-1);
        Point b = lasso.point(ii);
        if(a.x < p.x && b.x < p.x) {}
        else if(a.x > p.x && b.x > p.x)
          outside = !outside;
        else if(a.x + (p.y - a.y)*(b.x - a.x)/(b.y - a.y) > p.x)
          outside = !outside;
      }
    }
    if(outside)
      return false;
  }
  return true;
}

real Path2D::pathLength() const
{
  real dx = 0, dy = 0, length = 0;
  for(size_t ii = 1; ii < points.size(); ++ii) {
    dx = points[ii].x - points[ii-1].x;
    dy = points[ii].y - points[ii-1].y;
    length += sqrt(dx*dx + dy*dy);
  }
  return length;
}

// note that we assume closed paths are explicitly closed (e.g. with a lineTo to first point)
Point PathPointIter::next()
{
  //if(!hasNext()) return Point(NAN, NAN);
  if(sep2 == 0 || path.command(idx + 1) == Path2D::MoveTo || idx < 0)
    currPoint = tf.map(path.point(++idx));
  else {
    Point nextPoint = tf.map(path.point(idx + 1));
    real dist2 = (nextPoint - currPoint).dist2();
    if(dist2 > sep2)
      currPoint += (nextPoint - currPoint)*sqrt(sep2/dist2);
    else {
      do { ++idx; } while(idx + 1 < path.size() && path.command(idx + 1) != Path2D::MoveTo
          && (tf.map(path.point(idx + 1)) - currPoint).dist2() < sep2);
      currPoint = tf.map(path.point(idx));
    }
  }
  return currPoint;
}

// for(auto it = PathPointIter(path, 2); it; ++it) { point = it.point(); ... }
// auto it = PathPointIter(path, 2); while(it.hasNext()) { point = it.next(); ... }
// PathPointXXX pp(path, 2);  for(auto it = pp.begin(); it != pp.end(); ++it) { point = *it; ... }
// for(Point point : PathPointIter(path, 2)) { }

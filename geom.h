#pragma once

// from https://github.com/sgorsten/linalg
//#include "linalg.h"
#include <cmath>
#include <algorithm>  // std::min etc.
#include <vector>
#include <float.h>
#include <limits.h>

// for most use cases, float would probably be fine
typedef double real;

#define NaN real(NAN)
#define REAL_MAX  real(FLT_MAX)
// OK for IEEE-754, but not fixed point, etc
#define REAL_MIN  real(-FLT_MAX)

constexpr real degToRad(real deg) { return (deg*M_PI/180); }

// floating point comparison; we should also support relative and units-in-last-place compare
// see: https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
inline bool approxEq(real a, real b, real eps) { return fabs(a - b) < eps; }

inline real quantize(real a, real d) { return std::round(a/d)*d; }

class Point {
public:
  real x,y;
  Point(real _x, real _y) : x(_x), y(_y) {}
  Point() : Point(0,0) {}
  Point& translate(real dx, real dy) { x += dx; y += dy; return *this; }
  real dist(const Point& p) const { real dx = x - p.x; real dy = y - p.y; return std::sqrt(dx*dx + dy*dy); }
  real dist() const { return std::sqrt(x*x + y*y); }
  real dist2() const { return x*x + y*y; }
  bool isZero() const { return x == 0 && y == 0; }
  bool isNaN() const { return std::isnan(x) || std::isnan(y); }
  Point& normalize() { if(x != 0 || y != 0) { real d = dist(); x /= d; y /= d; } return *this; }

  Point& operator+=(const Point& p) { x += p.x; y += p.y; return *this; }
  Point& operator-=(const Point& p) { x -= p.x; y -= p.y; return *this; }
  Point& operator*=(real a) { x *= a; y *= a; return *this; }
  Point& operator/=(real a) { x /= a; y /= a; return *this; }
  Point& neg() { x = -x; y = -y; return *this; }
  Point operator-() const { return Point(*this).neg(); }
  friend bool operator==(const Point& p1, const Point& p2) { return p1.x == p2.x && p1.y == p2.y; }
  friend bool operator!=(const Point& p1, const Point& p2) { return !(p1 == p2); }
  friend bool approxEq(const Point& p1, const Point& p2, real eps)
      { return approxEq(p1.x, p2.x, eps) && approxEq(p1.y, p2.y, eps); }
  friend Point operator+(Point p1, const Point& p2) { return p1 += p2; }
  friend Point operator-(Point p1, const Point& p2) { return p1 -= p2; }
  friend Point operator*(Point p1, real a) { return p1 *= a; }
  friend Point operator/(Point p1, real a) { return p1 /= a; }
  friend Point operator*(real a, Point p1) { return p1 *= a; }
  //friend Point operator/(Dim a, Point p1) { return p1 /= a; }

  friend real dot(const Point& p1, const Point& p2) { return p1.x*p2.x + p1.y*p2.y; }
  friend real cross(const Point& a, const Point& b) { return a.x*b.y - a.y*b.x; }
  friend Point normal(const Point& v) { return Point(-v.y, v.x).normalize(); }  // +90 deg (CCW) rotation
};

// should we do left -> left() etc.?
struct Rect
{
  real left, top, right, bottom;

  Rect() : Rect(REAL_MAX, REAL_MAX, REAL_MIN, REAL_MIN) {}
  static Rect ltrb(real l, real t, real r, real b) { return Rect(l, t, r, b); }
  static Rect ltwh(real l, real t, real w, real h) { return Rect(l, t, l+w, t+h); }
  static Rect wh(real w, real h) { return Rect(0, 0, w, h); }
  static Rect centerwh(const Point& p, real w, real h) { return Rect(p.x, p.y, p.x, p.y).pad(w/2, h/2); }
  static Rect corners(const Point& a, const Point& b) { return Rect().rectUnion(a).rectUnion(b); }

  Rect& pad(real d);
  Rect& pad(real dx, real dy);
  Rect& round();
  Rect& translate(real dx, real dy);
  Rect& translate(const Point& p);
  Rect& scale(real sx, real sy);
  Rect& scale(real s) { return scale(s, s); }
  bool contains(const Rect& r) const;
  bool contains(const Point& p) const;
  bool overlaps(const Rect& r) const;
  Rect& rectUnion(const Rect& r);
  Rect& rectUnion(const Point& p);
  Rect& rectIntersect(const Rect& r);
  Point center() const;
  bool isValid() const;
  real width() const;
  real height() const;
  Point origin() const { return Point(left, top); }

  void setHeight(real h) { bottom = top + h; }
  void setWidth(real w) { right = left + w; }
  bool intersects(const Rect& r) const { return overlaps(r); }
  Rect united(const Rect& r) const { return Rect(*this).rectUnion(r); }
  Rect toSize() const { return Rect::wh(width(), height()); }

  friend bool operator==(const Rect& a, const Rect& b);
  friend bool operator!=(const Rect& a, const Rect& b) { return !(a == b); }
  friend bool approxEq(const Rect& a, const Rect& b, real eps);
  Rect& operator*=(real a) { return scale(a); }
  Rect& operator/=(real a) { return scale(1/a); }
  friend Rect operator*(Rect r, real a) { return r *= a; }
  friend Rect operator/(Rect r, real a) { return r /= a; }
  friend Rect operator*(real a, Rect r) { return r *= a; }
private:
  /*Rect(Dim _left = MAX_X_DIM, Dim _top = MAX_Y_DIM, Dim _right = MIN_X_DIM, Dim _bottom = MIN_Y_DIM)
      : left(_left), top(_top), right(_right), bottom(_bottom) {} */
  Rect(real _left, real _top, real _right, real _bottom)
      : left(_left), top(_top), right(_right), bottom(_bottom) {}

};

// we previously used a 3x3 matrix from linalg.h, but 3x3 mult requires 27 scalar mults, while only 12 scalar
//  mults are actually needed for 2D transform w/ translation; linalg was dominating CPU profile
// layout is:
// [ m0 m2 m4 ] [x]
// [ m1 m3 m5 ] [y]
// [  0  0  1 ] [1]
class Transform2D
{
public:
  real m[6];  //std::array<Dim, 6> m;

  Transform2D() : m{ 1, 0, 0, 1, 0, 0 } {}
  Transform2D(real m0, real m1, real m2, real m3, real m4, real m5) : m{m0, m1, m2, m3, m4, m5} {}
  Transform2D(real* array) { for(int ii = 0; ii < 6; ++ii) m[ii] = array[ii]; }

  Point mult(const Point& p) const;
  Rect mult(const Rect& r) const;
  real xoffset() const { return m[4]; }
  real yoffset() const { return m[5]; }
  real xscale() const { return m[0]; }
  real yscale() const { return m[3]; }
  real avgScale() const;
  real* asArray() { return &m[0]; }

  Rect mapRect(const Rect& r) const { return mult(r); }
  Point map(const Point& p) const { return mult(p); }

  bool isIdentity() const { return m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1 && m[4] == 0 && m[5] == 0; }
  bool isTranslate() const { return m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 1; }
  bool isRotating() const { return m[1] != 0 || m[2] != 0; }
  //bool isSimple() const { return m[1] == 0 && m[2] == 0; }
  Transform2D& reset() { *this = Transform2D(); return *this; }
  Transform2D inverse() const;

  Transform2D& translate(real dx, real dy) { m[4] += dx; m[5] += dy; return *this;  }
  Transform2D& translate(Point dr) { return translate(dr.x, dr.y);  }
  Transform2D& scale(real sx, real sy) { m[0] *= sx; m[1] *= sy; m[2] *= sx; m[3] *= sy; m[4] *= sx; m[5] *= sy; return *this; }
  Transform2D& scale(real s) { return scale(s, s); }
  Transform2D& rotate(real rad, Point pos = Point(0,0));
  Transform2D& shear(real sx, real sy);

  friend Transform2D operator*(const Transform2D& a, const Transform2D& b);

  friend bool operator==(const Transform2D& a, const Transform2D& b)
  {
    return a.m[0] == b.m[0] && a.m[1] == b.m[1] && a.m[2] == b.m[2]
        && a.m[3] == b.m[3] && a.m[4] == b.m[4] && a.m[5] == b.m[5];
  }
  friend bool operator!=(const Transform2D& a, const Transform2D& b) { return !(a == b); }
  friend bool approxEq(const Transform2D& a, const Transform2D& b, real eps);

  static Transform2D translating(real dx, real dy) { return Transform2D(1, 0, 0, 1, dx, dy); }
  static Transform2D translating(Point p) { return translating(p.x, p.y); }
  // Transform2D().scale() requires 6 unnecessary mults
  static Transform2D scaling(real sx, real sy) { return Transform2D(sx, 0, 0, sy, 0, 0); }
  static Transform2D scaling(real s) { return Transform2D(s, 0, 0, s, 0, 0); }
  static Transform2D rotating(real rad, Point pos = Point(0,0)) { return Transform2D().rotate(rad, pos); }
};


real calcAngle(Point a, Point b, Point c);
real distToSegment2(Point start, Point end, Point pt);
inline real distToSegment(Point start, Point end, Point pt) { return std::sqrt(distToSegment2(start, end, pt)); }
Point lineIntersection(Point a0, Point b0, Point a1, Point b1);
Point segmentIntersection(Point p0, Point p1, Point p2, Point p3);
bool pointInPolygon(const std::vector<Point>& poly, Point p);
real polygonArea(const std::vector<Point>& points);

// Ramer-Douglas-Peucker line simplification (O(n^2) version - O(n log n) is much less trivial)
// - we don't necessarily want this to be done in place in Path - see use for LassoSelector
// - end index is inclusive - fewer +/- 1s that way
// Consider implementing Visvalingam algorithm for comparison (uses area of triangles formed by adjacent
//  points as criteria instead of perpendicular distance)
template<class T>
std::vector<T> simplifyRDP(const std::vector<T>& points, int start, int end, real thresh)
{
  real maxdist2 = 0;
  int argmax = 0;
  Point p0(points[start].x, points[start].y);
  Point p1(points[end].x, points[end].y);
  for(int ii = start + 1; ii < end; ++ii) {
    real d2 = distToSegment2(p0, p1, Point(points[ii].x, points[ii].y));
    if(d2 > maxdist2) {
      maxdist2 = d2;
      argmax = ii;
    }
  }
  if(maxdist2 < thresh*thresh)
    return { points[start], points[end] };
  std::vector<T> left = simplifyRDP(points, start, argmax, thresh);
  std::vector<T> right = simplifyRDP(points, argmax, end, thresh);
  left.insert(left.end(), right.begin() + 1, right.end());
  return left;
}

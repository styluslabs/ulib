#include "geom.h"

// Rect class

Rect& Rect::translate(real dx, real dy)
{
  left += dx;  right += dx;  top += dy;  bottom += dy;
  return *this;
}

Rect& Rect::translate(const Point& p)
{
  return translate(p.x, p.y);
}

Rect& Rect::scale(real sx, real sy)
{
  left *= sx;  right *= sx;  top *= sy;  bottom *= sy;
  return *this;
}

bool Rect::contains(const Rect& r) const
{
  return left <= r.left && right >= r.right && top <= r.top && bottom >= r.bottom && r.isValid();
}

bool Rect::contains(const Point& p) const
{
  return left <= p.x && p.x <= right && top <= p.y && p.y <= bottom;
}

bool Rect::overlaps(const Rect& r) const
{
  return r.left <= right && r.right >= left && r.top <= bottom && r.bottom >= top;
}

// if one of Rects is invalid, union will just be the other Rect
Rect& Rect::rectUnion(const Rect& r)
{
  left = std::min(left, r.left);
  top = std::min(top, r.top);
  right = std::max(right, r.right);
  bottom = std::max(bottom, r.bottom);
  return *this;
}

Rect& Rect::rectUnion(const Point& p)
{
  left = std::min(left, p.x);
  top = std::min(top, p.y);
  right = std::max(right, p.x);
  bottom = std::max(bottom, p.y);
  return *this;
}

// intersection may be an invalid rect
Rect& Rect::rectIntersect(const Rect& r)
{
  left = std::max(left, r.left);
  top = std::max(top, r.top);
  right = std::min(right, r.right);
  bottom = std::min(bottom, r.bottom);
  return *this;
}

Rect& Rect::pad(real d)
{
  return pad(d, d);
}

Rect& Rect::pad(real dx, real dy)
{
  left -= dx;
  right += dx;
  top -= dy;
  bottom += dy;
  return *this;
}

Rect& Rect::round()
{
  left = std::floor(left);
  right = std::ceil(right);
  top = std::floor(top);
  bottom = std::ceil(bottom);
  return *this;
}

Point Rect::center() const
{
  return Point(0.5*(left + right), 0.5*(top + bottom));
}

bool Rect::isValid() const
{
  return left <= right && top <= bottom;
}

real Rect::width() const
{
  return right - left;
}

real Rect::height() const
{
  return bottom - top;
}

bool operator==(const Rect& a, const Rect& b)
{
  return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

bool approxEq(const Rect& a, const Rect& b, real eps)
{
  return approxEq(a.left, b.left, eps) && approxEq(a.top, b.top, eps)
      && approxEq(a.right, b.right, eps) && approxEq(a.bottom, b.bottom, eps);
}

// calculate the angle a-b-c (i.e. directed angle from b-a to b-c)
real calcAngle(Point a, Point b, Point c)
{
  a -= b;
  c -= b;
  return atan2(c.y, c.x) - atan2(a.y, a.x);
}

// distance from point `pt` to line segment `start`-`end` to `pt`
real distToSegment2(Point start, Point end, Point pt)
{
  const real l2 = (end - start).dist2();
  if(l2 == 0.0) // zero length segment
    return (start - pt).dist2();
  // Consider the line extending the segment, parameterized as start + t*(end - start).
  // We find projection of pt onto this line and clamp t to [0,1] to limit to segment
  const real t = std::max(real(0), std::min(real(1), dot(pt - start, end - start)/l2));
  const Point proj = start + t * (end - start);  // Projection falls on the segment
  return (proj - pt).dist2();
}

Point lineIntersection(Point a0, Point b0, Point a1, Point b1)
{
  real dx0 = a0.x - b0.x, dy0 = a0.y - b0.y, dx1 = a1.x - b1.x, dy1 = a1.y - b1.y;
  real denom = dx0*dy1 - dy0*dx1;
  if(denom == 0)
    return Point(NaN, NaN);
  real invd = 1/denom;
  real det0 = (a0.x*b0.y - a0.y*b0.x);
  real det1 = (a1.x*b1.y - a1.y*b1.x);
  return Point( (dx1*det0 - dx0*det1)*invd, (dy1*det0 - dy0*det1)*invd );
}

Point segmentIntersection(Point p0, Point p1, Point p2, Point p3)
{
  Point s1(p1.x - p0.x, p1.y - p0.y);
  Point s2(p3.x - p2.x, p3.y - p2.y);
  real det = -s2.x * s1.y + s1.x * s2.y;
  if(det != 0) {
    real invdet = 1/det;
    // s is position along p2->p3, t is position along p0->p1
    real s = (-s1.y * (p0.x - p2.x) + s1.x * (p0.y - p2.y))*invdet;
    real t = ( s2.x * (p0.y - p2.y) - s2.y * (p0.x - p2.x))*invdet;
    //if(s >= 0 && s <= 1 && t >= 0 && t <= 1)
    if(s >= 0 && s < 1 && t >= 0 && t < 1)
      return Point(p0.x + (t * s1.x), p0.y + (t * s1.y));
  }
  return Point(NaN, NaN);
}

// https://wrf.ecse.rpi.edu/Research/Short_Notes/pnpoly.html
// count crossings of horizontal ray in +x direction from point p
bool pointInPolygon(const std::vector<Point>& poly, Point p)
{
  bool in = false;
  for(size_t i = 0, j = poly.size()-1; i < poly.size(); j = i++) {
    if( ((poly[i].y > p.y) != (poly[j].y > p.y)) &&
        (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) )
      in = !in;
  }
  return in;
}

//bool halfPlane(const Point& p, const Point& cp1, const Point& cp2)
//{ return (cp2.x-cp1.x)*(p.y-cp1.y) > (cp2.y-cp1.y)*(p.x-cp1.x); }

real polygonArea(const std::vector<Point>& points)
{
  real area = 0;
  for(size_t ii = 0, jj = points.size() - 1; ii < points.size(); jj = ii++)
    area += (points[jj].x + points[ii].x)*(points[jj].y - points[ii].y);
  return area/2;
}

// Transform2D

Transform2D Transform2D::inverse() const
{
  // TODO: benchmark this optimization for common special case
  //if(m[1] == 0 && m[2] == 0) {
  //  Dim sx = 1/m[0];
  //  Dim sy = 1/m[3];
  //  return Transform2D(sx, 0, 0, sy, -m[4]*sx, -m[5]*sy);
  //}

  double det = (double)m[0] * m[3] - (double)m[2] * m[1];
  if(det > -1e-6 && det < 1e-6)
    return Transform2D();

  double invdet = 1.0 / det;
  return Transform2D(
    real(m[3] * invdet), real(-m[1] * invdet), real(-m[2] * invdet), real(m[0] * invdet),
    real(((double)m[2] * m[5] - (double)m[3] * m[4]) * invdet),
    real(((double)m[1] * m[4] - (double)m[0] * m[5]) * invdet)
  );
}

Transform2D& Transform2D::rotate(real rad, Point pos)
{
  real s = std::sin(rad);
  real c = std::cos(rad);
  *this = Transform2D(c, s, -s, c, pos.x - c*pos.x + s*pos.y, pos.y - s*pos.x - c*pos.y) * (*this);
  return *this;
}

Transform2D& Transform2D::shear(real sx, real sy)
{
  *this = Transform2D(1, sy, sx, 1, 0, 0) * (*this);
  return *this;
}

Transform2D operator*(const Transform2D& a, const Transform2D& b)
{
  return Transform2D(
    b.m[0] * a.m[0] + b.m[1] * a.m[2],          b.m[0] * a.m[1] + b.m[1] * a.m[3],
    b.m[2] * a.m[0] + b.m[3] * a.m[2],          b.m[2] * a.m[1] + b.m[3] * a.m[3],
    b.m[4] * a.m[0] + b.m[5] * a.m[2] + a.m[4], b.m[4] * a.m[1] + b.m[5] * a.m[3] + a.m[5]
  );
}

bool approxEq(const Transform2D& a, const Transform2D& b, real eps)
{
  for(int ii = 0; ii < 6; ++ii) {
    if(std::abs(a.m[ii] - b.m[ii]) >= eps)
      return false;
  }
  return true;
}

real Transform2D::avgScale() const
{
  return std::sqrt(std::sqrt(m[0]*m[0] + m[2]*m[2]) * std::sqrt(m[1]*m[1] + m[3]*m[3]));  // nanovg-2
  //return (std::sqrt(m[0]*m[0] + m[2]*m[2]) + std::sqrt(m[1]*m[1] + m[3]*m[3]))/2;  -- nanovg
}

// apply matrix transform to point
Point Transform2D::mult(const Point& p) const
{
  // column-major
  return Point(m[0]*p.x + m[2]*p.y + m[4], m[1]*p.x + m[3]*p.y + m[5]);
}

#ifndef NDEBUG
#include "platformutil.h"  // for ASSERT
#endif

Rect Transform2D::mult(const Rect& r) const
{
#ifndef NDEBUG
  ASSERT(r.isValid() && "Cannot map an invalid rect - could become a valid rect!");
#endif
  if(!isRotating()) {
    // column-major
    Rect s = Rect::ltrb(m[0]*r.left + m[4], m[3]*r.top + m[5], m[0]*r.right + m[4],  m[3]*r.bottom + m[5]);
    return Rect::ltrb(std::min(s.left, s.right), std::min(s.top, s.bottom), std::max(s.left, s.right), std::max(s.top, s.bottom));
  }
  Point p1 = mult(Point(r.left, r.top));
  Point p2 = mult(Point(r.left, r.bottom));
  Point p3 = mult(Point(r.right, r.top));
  Point p4 = mult(Point(r.right, r.bottom));
  return Rect().rectUnion(p1).rectUnion(p2).rectUnion(p3).rectUnion(p4);
}

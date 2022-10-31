#pragma once

#include <vector>
#include "geom.h"


// what about methods named after SVG path syntax, so we could do PainterPath().m(3,4).l(3,4).c(3,4,4,6).z()?
class Path2D
{
public:
  // if path parsing fails, last command will be "Error"
  enum PathCommand { MoveTo=1, LineTo, QuadTo, CubicTo, ArcTo }; //, Close, Error};

  std::vector<Point> points;
  std::vector<PathCommand> commands;
  enum FillRule { EvenOddFill, WindingFill } fillRule = WindingFill;
  //enum PathType {Path=1, Line, Polyline, Polygon, Rectangle, Ellipse, Circle};

  void moveTo(const Point& p);
  void lineTo(const Point& p);
  void quadTo(const Point& c, const Point& p);
  void cubicTo(const Point& c1, const Point& c2, const Point& p);

  void moveTo(real x, real y) { moveTo(Point(x,y)); }
  void lineTo(real x, real y) { lineTo(Point(x,y)); }
  void quadTo(real cx, real cy, real x, real y) { quadTo(Point(cx, cy), Point(x, y)); }
  void cubicTo(real c1x, real c1y, real c2x, real c2y, real x, real y)
    { cubicTo(Point(c1x, c1y), Point(c2x, c2y), Point(x, y)); }
  void addArc(real cx, real cy, real rx, real ry, real startRad, real sweepRad, real xAxisRotRad = 0);

  void addPoint(const Point& p, PathCommand cmd = LineTo);
  void addPoint(real x, real y, PathCommand cmd = LineTo) { addPoint(Point(x,y), cmd); }

  Path2D& addEllipse(real cx, real cy, real rx, real ry);
  Path2D& addLine(const Point& a, const Point& b) { moveTo(a.x, a.y); lineTo(b.x, b.y); return *this; }
  Path2D& addRect(const Rect& r);

  void closeSubpath();
  void connectPath(const Path2D& other);
  void setFillRule(FillRule rule) { fillRule = rule; }

  void reserve(size_t n, bool cmds = false) { points.reserve(n); if(cmds) commands.reserve(n); }
  bool isSimple() const { return commands.empty(); }
  int size() const { return points.size(); }
  bool empty() const { return points.empty(); }
  bool isClosed() const { return !points.empty() && points.front() == points.back(); }
  void clear() { points.clear(); commands.clear(); }
  void resize(size_t n) { points.resize(n);  if(!commands.empty()) commands.resize(n); }
  Rect getBBox() const;
  //bool isNearPoint(Dim x0, Dim y0, Dim radius) const;
  real distToPoint(const Point& p) const;
  bool isEnclosedBy(const Path2D& lasso) const;
  real pathLength() const;
  Point positionAlongPath(real offset, Point* normal_out) const;

  void translate(real x, real y);
  void scale(real sx, real sy);
  Path2D& transform(const Transform2D& tf);

  Path2D toReversed() const;
  Path2D toFlat() const;
  std::vector<Path2D> getSubPaths() const;

  // reading
  Point point(int idx) const { return points[idx]; }
  PathCommand command(int idx) const
  {
    if(idx < (int)commands.size())
      return commands[idx];
    return idx > 0 ? LineTo : MoveTo;
  }
  // get point from back
  Point rpoint(int idx) const { return points[size() - idx]; }
  Point currentPosition() const { return getEndPoint(size() - 1); }
  Rect controlPointRect() const { return getBBox(); }
  Rect boundingRect() const { return getBBox(); }

  // maybe look into http://www.angusj.com/delphi/clipper.php
  bool intersects(const Path2D& other) { return false; }
  Path2D subtracted(const Path2D& other) { return *this; }

  static bool PRESERVE_ARCS;

private:
  void fillCommands();
  void pushPoint(const Point& p) { points.push_back(p); }
  Point getEndPoint(int ii) const;
};

// Java-style iterator for iterating over points along path, optionally w/ a specified separation dist - in
//  which case points are interpolated or skipped as needed
class PathPointIter
{
public:
  PathPointIter(const Path2D& _path, Transform2D _tf = Transform2D(), real _sep = 0)
      : path(_path), tf(_tf), sep2(_sep*_sep) {}
  bool hasNext() const { return idx + 1 < path.size(); }  // operator bool()
  Point next();  // operator++()

private:
  const Path2D& path;
  Transform2D tf;
  real sep2;
  Point currPoint;
  int idx = -1;
};

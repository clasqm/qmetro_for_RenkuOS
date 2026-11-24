#ifndef MAPGRAPHICS_H
#define MAPGRAPHICS_H
#include <QPainter>
// Qt 5 no longer pulls QPainterPath in through <QPainter>, so the return types
// below would be incomplete; include it explicitly.
#include <QPainterPath>

QPainterPath splinePath(const QPolygonF &points);
QPainterPath arrowPath(const QPoint &Point1,const QPoint &Point2);
QPainterPath stairsPath(const QPolygonF &points);

#endif

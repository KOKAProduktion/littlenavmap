/*****************************************************************************
* Copyright 2015-2020 Alexander Barthel alex@littlenavmap.org
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#ifndef LITTLENAVMAP_TEXTPLACEMENT_H
#define LITTLENAVMAP_TEXTPLACEMENT_H

#include <QBitArray>
#include <QLineF>
#include <QList>
#include <QPoint>
#include <QCoreApplication>
#include <QColor>
#include <QVector>
#include <QRect>

namespace atools {
namespace geo {
class Line;
class LineString;
class Pos;
}
}

class QPainter;
class CoordinateConverter;

/* Contains methods for text placement along line strings. */
class TextPlacement
{
  Q_DECLARE_TR_FUNCTIONS(TextPlacement)

public:
  TextPlacement(QPainter *painterParam, const CoordinateConverter *coordinateConverter, const QRect& screenRectParam);

  /* Prepare for drawTextAlongLines and also fills data for getVisibleStartPoints and getStartPoints.
   * Lines do not have to form a connected linestring. */
  void calculateTextAlongLines(const QVector<atools::geo::Line>& lines, const QStringList& routeTexts);

  /* Calculate point coordinates and visibility */
  void calculateTextPositions(const atools::geo::LineString& points);

  /* Draw text after calling calculateTextAlongLines. Text is aligned with lines and kept in an
   * upwards readable position. Arrows are optionally added to end or start.
   *  the tree methods drawTextAlongLines, calculateTextAlongScreenLines and clearLineTextData work with a class state */
  void drawTextAlongLines();
  void clearLineTextData();

  void drawTextAlongOneLine(const QString& text, float bearing, const QPointF& textCoord, float textLineLength);

  /* Find text position along a great circle route
   *  @param x,y resulting text position
   *  @param line start and end coordinates of the line
   *  @param bearing text bearing at the returned position
   *  @param distanceMeter distance between points
   *  @param maxPoints Maximum number of points to sample
   */
  bool findTextPos(const atools::geo::Line& line, float distanceMeter, float textWidth, float textHeight, int maxPoints, int& x, int& y,
                   float *bearing) const;

  /* Bit array indicating which start point is visible or not.  Filled by calculateTextAlongLines */
  const QBitArray& getVisibleStartPoints() const
  {
    return visibleStartPoints;
  }

  /* Get all screen start points including the last one. Filled by calculateTextAlongLines */
  const QList<QPointF>& getStartPoints() const
  {
    return startPoints;
  }

  /* Arrows are prepended or appendend to the given text depending on direction */
  void setArrowRight(const QString& value)
  {
    arrowRight = value;
  }

  void setArrowLeft(const QString& value)
  {
    arrowLeft = value;
  }

  void setDrawFast(bool value)
  {
    fast = value;
  }

  /* Default is true which puts text always above the line. */
  void setTextOnTopOfLine(bool value)
  {
    textOnTopOfLine = value;
  }

  /* Line width in pixel used for text placement */
  void setLineWidth(float value)
  {
    lineWidth = value;
  }

  /* Set an array of colors with the same size as lines in calculateTextAlongLines */
  void setColors(const QVector<QColor>& value)
  {
    colors = value;
  }

  /* Show direction arrow even if text is empty */
  void setArrowForEmpty(bool value)
  {
    arrowForEmpty = value;
  }

  /* Place text on top of line. Otherwise above. */
  void setTextOnLineCenter(bool value)
  {
    textOnLineCenter = value;
  }

  /* Minimum line length before text is omitted */
  void setMinLengthForText(int value)
  {
    minLengthForText = value;
  }

  /* Maximum number of points along line to search for position */
  void setMaximumPoints(int value)
  {
    maximumPoints = value;
  }

private:
  bool findTextPosInternal(const atools::geo::Line& line, float distanceMeter, float textWidth, float textHeight, int numPoints,
                           bool allowPartial,
                           int& x, int& y, float& bearing) const;
  int findClosestInternal(const QVector<int>& fullyVisibleValid, const QVector<int>& pointsIdxValid, const QPolygonF& points,
                          const QVector<QPointF>& neighbors) const;

  /* Elide text with a buffer depending on font height */
  QString elideText(const QString& text, const QString& arrow, float lineLength);

  QList<QPointF> textCoords;
  QList<float> textBearings;
  QStringList texts;
  QList<float> textLineLengths;

  // Collect start and end points of legs and visibility
  QList<QPointF> startPoints;
  QBitArray visibleStartPoints;

  /* Evaluate 50 text placement positions along line */
  const float FIND_TEXT_POS_STEP = 0.02f;

  /* Minimum pixel length for a line to get a text label */
  int minLengthForText = 80;

  int maximumPoints = 50;

  bool fast = false, textOnTopOfLine = true, textOnLineCenter = false, arrowForEmpty = false;
  QPainter *painter = nullptr;
  const CoordinateConverter *converter = nullptr;
  QString arrowRight, arrowLeft;
  float lineWidth = 10.f;
  QVector<QColor> colors;
  QVector<QColor> colors2;

  QRect screenRect;

};

#endif // LITTLENAVMAP_TEXTPLACEMENT_H

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

#ifndef MAPACTIONSCONTROLLER_H
#define MAPACTIONSCONTROLLER_H

#include "webapi/abstractlnmactionscontroller.h"
#include "web/webmapcontroller.h"

#include "web/webflags.h"

#include "geo/rect.h"
#include <QPixmap>

class QPixmap;
class MapPaintWidget;
/**
 * @brief Map actions controller implementation.
 */
class MapActionsController :
        public AbstractLnmActionsController
{
    Q_OBJECT
public:
    Q_INVOKABLE MapActionsController(QObject *parent, bool verboseParam, AbstractInfoBuilder* infoBuilder);
    /**
     * @brief get map image
     */
    Q_INVOKABLE WebApiResponse imageAction(WebApiRequest request);

    explicit MapActionsController(QWidget *parent, bool verboseParam);
    virtual ~MapActionsController() override;

    MapActionsController(const MapActionsController& other) = delete;
    MapActionsController& operator=(const MapActionsController& other) = delete;

protected:

    /* Create or delete the map paint widget */
    void init();
    void deInit();

    /* Get pixmap with given width and height from current position. */
    MapPixmap getPixmap(int width, int height);

    /* Get pixmap with given width and height for a map object like an airport, the user aircraft or a route. */
    MapPixmap getPixmapObject(int width, int height, web::ObjectType type, const QString& ident, float distanceKm);

    /* Get map at given position and distance. Command can be used to zoom in/out or scroll from the given position:
     * "in", "out", "left", "right", "up" and "down".  */
    MapPixmap getPixmapPosDistance(int width, int height, atools::geo::Pos pos, float distanceKm, const QString& mapCommand, const QString& errorCase = QLatin1String(""));

    /* Zoom to rectangel on map. */
    MapPixmap getPixmapRect(int width, int height, atools::geo::Rect rect, const QString& errorCase = tr("Invalid rectangle"));

    MapPaintWidget *mapPaintWidget = nullptr;
    QWidget *parentWidget;
    bool verbose = false;
};

#endif // MAPACTIONSCONTROLLER_H
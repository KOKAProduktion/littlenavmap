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

#ifndef LITTLENAVMAP_PARKINGDIALOG_H
#define LITTLENAVMAP_PARKINGDIALOG_H

#include <QDialog>

namespace map {
struct MapAirport;
struct MapParking;
struct MapStart;
}

namespace Ui {
class ParkingDialog;
}

class QListWidget;
class QAbstractButton;

namespace atools {
namespace gui {
class ItemViewZoomHandler;
}
}

namespace internal {
struct StartPosition;
}

/*
 * Allows to select the flight plan departure parking or start position.
 * The latter one can be a runway or helipad.
 */
class ParkingDialog :
  public QDialog
{
  Q_OBJECT

public:
  ParkingDialog(QWidget *parent, const map::MapAirport& departureAirportParam);
  virtual ~ParkingDialog() override;

  /* Get selected parking spot
   * @return true if parking was selected */
  bool getSelectedParking(map::MapParking& parking) const;

  /* Get selected start position.
   * @return true if a start was selected */
  bool getSelectedStartPosition(map::MapStart& start) const;

private:
  void saveState();
  void restoreState();
  void updateButtons();
  void updateTable();
  void filterTextEdited();
  void buttonBoxClicked(QAbstractButton *button);
  void doubleClicked();

  QList<internal::StartPosition> entries;
  Ui::ParkingDialog *ui;
  atools::gui::ItemViewZoomHandler *zoomHandler = nullptr;
  const map::MapAirport& departureAirport;
};

#endif // LITTLENAVMAP_PARKINGDIALOG_H

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

#include "db/databasemanager.h"

#include "gui/textdialog.h"
#include "sql/sqldatabase.h"
#include "options/optiondata.h"
#include "common/constants.h"
#include "fs/db/databasemeta.h"
#include "db/databasedialog.h"
#include "settings/settings.h"
#include "fs/navdatabaseoptions.h"
#include "fs/navdatabaseprogress.h"
#include "common/formatter.h"
#include "fs/xp/scenerypacks.h"
#include "gui/helphandler.h"
#include "fs/navdatabase.h"
#include "sql/sqlutil.h"
#include "sql/sqltransaction.h"
#include "gui/errorhandler.h"
#include "gui/mainwindow.h"
#include "ui_mainwindow.h"
#include "navapp.h"
#include "gui/dialog.h"
#include "fs/userdata/userdatamanager.h"
#include "fs/userdata/logdatamanager.h"
#include "fs/online/onlinedatamanager.h"
#include "io/fileroller.h"
#include "atools.h"
#include "db/databaseprogressdialog.h"
#include "sql/sqlexception.h"
#include "track/trackmanager.h"
#include "util/version.h"
#include "fs/navdatabaseerrors.h"
#include "options/optionsdialog.h"
#include "fs/scenery/languagejson.h"

#include <QElapsedTimer>
#include <QDir>
#include <QSettings>

using atools::gui::ErrorHandler;
using atools::sql::SqlUtil;
using atools::fs::FsPaths;
using atools::fs::NavDatabaseOptions;
using atools::fs::NavDatabase;
using atools::settings::Settings;
using atools::sql::SqlDatabase;
using atools::sql::SqlTransaction;
using atools::sql::SqlQuery;
using atools::fs::db::DatabaseMeta;

const static int MAX_ERROR_BGL_MESSAGES = 400;
const static int MAX_ERROR_SCENERY_MESSAGES = 400;
const static int MAX_TEXT_LENGTH = 120;
const static int MAX_AGE_DAYS = 60;

DatabaseManager::DatabaseManager(MainWindow *parent)
  : QObject(parent), mainWindow(parent)
{
  databaseMetaText = QObject::tr(
    "<p><big>Last Update: %1. Database Version: %2. Program Version: %3.%4</big></p>");

  databaseAiracCycleText = QObject::tr(" AIRAC Cycle %1.");

  databaseInfoText = QObject::tr("<table>"
                                   "<tbody>"
                                     "<tr> "
                                       "<td width=\"60\"><b>Files:</b>"
                                       "</td>    "
                                       "<td width=\"60\">&nbsp;&nbsp;&nbsp;&nbsp;%L6"
                                       "</td> "
                                       "<td width=\"60\"><b>VOR:</b>"
                                       "</td> "
                                       "<td width=\"60\">&nbsp;&nbsp;&nbsp;&nbsp;%L8"
                                       "</td> "
                                       "<td width=\"60\"><b>Markers:</b>"
                                       "</td>     "
                                       "<td width=\"60\">&nbsp;&nbsp;&nbsp;&nbsp;%L11"
                                       "</td>"
                                     "</tr>"
                                     "<tr> "
                                       "<td width=\"60\"><b>Airports:</b>"
                                       "</td> "
                                       "<td width=\"60\">&nbsp;&nbsp;&nbsp;&nbsp;%L7"
                                       "</td> "
                                       "<td width=\"60\"><b>ILS:</b>"
                                       "</td> "
                                       "<td width=\"60\">&nbsp;&nbsp;&nbsp;&nbsp;%L9"
                                       "</td> "
                                       "<td width=\"60\"><b>Waypoints:</b>"
                                       "</td>  "
                                       "<td width=\"60\">&nbsp;&nbsp;&nbsp;&nbsp;%L12"
                                       "</td>"
                                     "</tr>"
                                     "<tr> "
                                       "<td width=\"60\">"
                                       "</td>"
                                       "<td width=\"60\">"
                                       "</td>"
                                       "<td width=\"60\"><b>NDB:</b>"
                                       "</td> "
                                       "<td width=\"60\">&nbsp;&nbsp;&nbsp;&nbsp;%L10"
                                       "</td> "
                                       "<td width=\"60\"><b>Airspaces:</b>"
                                       "</td>  "
                                       "<td width=\"60\">&nbsp;&nbsp;&nbsp;&nbsp;%L13"
                                       "</td>"
                                     "</tr>"
                                   "</tbody>"
                                 "</table>"
                                 );

  databaseTimeText = QObject::tr(
    "<b>%1</b><br/>" // Scenery:
    "<br/><br/>" // File:
    "<b>Time:</b> %2<br/>%3%4"
      "<b>Errors:</b> %5<br/><br/>"
      "<big>Found:</big></br>"
    ) + databaseInfoText;

  databaseLoadingText = QObject::tr(
    "<b>Scenery:</b> %1 (%2)<br/>"
    "<b>File:</b> %3<br/><br/>"
    "<b>Time:</b> %4<br/>"
    "<b>Errors:</b> %5<br/><br/>"
    "<big>Found:</big></br>"
    ) + databaseInfoText;

  dialog = new atools::gui::Dialog(mainWindow);

  // Keeps MSFS translations from table "translation" in memory
  languageIndex = new atools::fs::scenery::LanguageJson;

  // Also loads list of simulators
  restoreState();

  databaseDirectory = Settings::getPath() + QDir::separator() + lnm::DATABASE_DIR;
  if(!QDir().mkpath(databaseDirectory))
    qWarning() << "Cannot create db dir" << databaseDirectory;

  QString name = buildDatabaseFileName(FsPaths::NAVIGRAPH);
  if(name.isEmpty() && !QFile::exists(name))
    // Set to off if not database found
    navDatabaseStatus = dm::NAVDATABASE_OFF;

  // Find simulators by default registry entries
  simulators.fillDefault();

  // Find any stale databases that do not belong to a simulator and update installed and has database flags
  updateSimulatorFlags();

  for(auto it = simulators.constBegin(); it != simulators.constEnd(); ++it)
    qDebug() << Q_FUNC_INFO << it.key() << it.value();

  // Correct if current simulator is invalid
  correctSimulatorType();

  qDebug() << Q_FUNC_INFO << "fs type" << currentFsType;

  if(mainWindow != nullptr)
  {
    databaseDialog = new DatabaseDialog(mainWindow, simulators);
    databaseDialog->setReadInactive(readInactive);
    databaseDialog->setReadAddOnXml(readAddOnXml);

    connect(databaseDialog, &DatabaseDialog::simulatorChanged, this, &DatabaseManager::simulatorChangedFromComboBox);
  }

  SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_SIM);
  SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_NAV);
  SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_DLG_INFO_TEMP);
  SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_TEMP);

  databaseSim = new SqlDatabase(DATABASE_NAME_SIM);
  databaseNav = new SqlDatabase(DATABASE_NAME_NAV);

  if(mainWindow != nullptr)
  {
    // Open only for instantiation in main window and not in main function
    SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_USER);
    SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_TRACK);
    SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_LOGBOOK);
    SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_ONLINE);

    // Airspace databases
    SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_USER_AIRSPACE);
    SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_SIM_AIRSPACE);
    SqlDatabase::addDatabase(DATABASE_TYPE, DATABASE_NAME_NAV_AIRSPACE);

    // Variable databases (user can edit or program downloads data)
    databaseUser = new SqlDatabase(DATABASE_NAME_USER);
    databaseTrack = new SqlDatabase(DATABASE_NAME_TRACK);
    databaseLogbook = new SqlDatabase(DATABASE_NAME_LOGBOOK);
    databaseOnline = new SqlDatabase(DATABASE_NAME_ONLINE);

    // Airspace databases
    databaseUserAirspace = new SqlDatabase(DATABASE_NAME_USER_AIRSPACE);

    // ... as duplicate connections to sim and nav databases but independent of nav switch
    databaseSimAirspace = new SqlDatabase(DATABASE_NAME_SIM_AIRSPACE);
    databaseNavAirspace = new SqlDatabase(DATABASE_NAME_NAV_AIRSPACE);

    // Open user point database =================================
    openWriteableDatabase(databaseUser, "userdata", "user", true /* backup */);
    userdataManager = new atools::fs::userdata::UserdataManager(databaseUser);
    if(!userdataManager->hasSchema())
      userdataManager->createSchema();
    else
      userdataManager->updateSchema();

    // Open logbook database =================================
    openWriteableDatabase(databaseLogbook, "logbook", "logbook", true /* backup */);
    logdataManager = new atools::fs::userdata::LogdataManager(databaseLogbook);
    if(!logdataManager->hasSchema())
      logdataManager->createSchema();
    else
      logdataManager->updateSchema();

    // Open user airspace database =================================
    openWriteableDatabase(databaseUserAirspace, "userairspace", "userairspace", false /* backup */);
    if(!SqlUtil(databaseUserAirspace).hasTable("boundary"))
    {
      SqlTransaction transaction(databaseUserAirspace);
      // Create schema on demand
      createEmptySchema(databaseUserAirspace, true /* boundary only */);
      transaction.commit();
    }

    // Open track database =================================
    openWriteableDatabase(databaseTrack, "track", "track", false /* backup */);
    trackManager = new TrackManager(databaseTrack, databaseNav);
    trackManager->createSchema();
    // trackManager->initQueries();

    // Open online network database ==============================
    atools::settings::Settings& settings = atools::settings::Settings::instance();
    bool verbose = settings.getAndStoreValue(lnm::OPTIONS_WHAZZUP_PARSER_DEBUG, false).toBool();

    openWriteableDatabase(databaseOnline, "onlinedata", "online network", false /* backup */);
    onlinedataManager = new atools::fs::online::OnlinedataManager(databaseOnline, verbose);
    onlinedataManager->createSchema();
    onlinedataManager->initQueries();
  }
}

DatabaseManager::~DatabaseManager()
{
  // Delete simulator switch actions
  freeActions();

  delete databaseDialog;
  delete dialog;
  delete progressDialog;
  delete userdataManager;
  delete trackManager;
  delete logdataManager;
  delete onlinedataManager;

  closeAllDatabases();
  closeUserDatabase();
  closeTrackDatabase();
  closeLogDatabase();
  closeUserAirspaceDatabase();
  closeOnlineDatabase();

  delete databaseSim;
  delete databaseNav;
  delete databaseUser;
  delete databaseTrack;
  delete databaseLogbook;
  delete databaseOnline;
  delete databaseUserAirspace;
  delete databaseSimAirspace;
  delete databaseNavAirspace;
  delete languageIndex;

  SqlDatabase::removeDatabase(DATABASE_NAME_SIM);
  SqlDatabase::removeDatabase(DATABASE_NAME_NAV);
  SqlDatabase::removeDatabase(DATABASE_NAME_USER);
  SqlDatabase::removeDatabase(DATABASE_NAME_TRACK);
  SqlDatabase::removeDatabase(DATABASE_NAME_LOGBOOK);
  SqlDatabase::removeDatabase(DATABASE_NAME_DLG_INFO_TEMP);
  SqlDatabase::removeDatabase(DATABASE_NAME_TEMP);
  SqlDatabase::removeDatabase(DATABASE_NAME_USER_AIRSPACE);
  SqlDatabase::removeDatabase(DATABASE_NAME_SIM_AIRSPACE);
  SqlDatabase::removeDatabase(DATABASE_NAME_NAV_AIRSPACE);
}

bool DatabaseManager::checkIncompatibleDatabases(bool *databasesErased)
{
  bool ok = true;

  if(databasesErased != nullptr)
    *databasesErased = false;

  // Need empty block to delete sqlDb before removing driver
  {
    // Create a temporary database
    SqlDatabase sqlDb(DATABASE_NAME_TEMP);
    QStringList databaseNames, databaseFiles;

    // Collect all incompatible databases

    for(auto it = simulators.constBegin(); it != simulators.constEnd(); ++it)
    {
      QString dbName = buildDatabaseFileName(it.key());
      if(QFile::exists(dbName))
      {
        // Database file exists
        sqlDb.setDatabaseName(dbName);
        sqlDb.open();

        DatabaseMeta meta(&sqlDb);
        if(!meta.hasSchema())
          // No schema create an empty one anyway
          createEmptySchema(&sqlDb);
        else if(!meta.isDatabaseCompatible())
        {
          // Not compatible add to list
          databaseNames.append("<i>" + FsPaths::typeToName(it.key()) + "</i>");
          databaseFiles.append(dbName);
          qWarning() << "Incompatible database" << dbName;
        }
        sqlDb.close();
      }
    }

    // Delete the dummy database without dialog if needed
    QString dummyName = buildDatabaseFileName(atools::fs::FsPaths::NONE);
    sqlDb.setDatabaseName(dummyName);
    sqlDb.open();
    DatabaseMeta meta(&sqlDb);
    if(!meta.hasSchema() || !meta.isDatabaseCompatible())
    {
      qDebug() << Q_FUNC_INFO << "Updating dummy database" << dummyName;
      createEmptySchema(&sqlDb);
    }
    sqlDb.close();

    if(!databaseNames.isEmpty())
    {
      QString msg, trailingMsg;
      if(databaseNames.size() == 1)
      {
        msg = tr("The database for the simulator "
                 "below is not compatible with this program version or was incompletly loaded:<br/><br/>"
                 "%1<br/><br/>Erase it?<br/><br/>%2");
        trailingMsg = tr("You can reload the Scenery Library Database again after erasing.");
      }
      else
      {
        msg = tr("The databases for the simulators "
                 "below are not compatible with this program version or were incompletly loaded:<br/><br/>"
                 "%1<br/><br/>Erase them?<br/><br/>%2");
        trailingMsg = tr("You can reload these Scenery Library Databases again after erasing.");
      }

      // Avoid the splash screen hiding the dialog
      NavApp::closeSplashScreen();

      QMessageBox box(QMessageBox::Question, QApplication::applicationName(), msg.arg(databaseNames.join("<br/>")).arg(trailingMsg),
                      QMessageBox::No | QMessageBox::Yes, mainWindow);
      box.button(QMessageBox::No)->setText(tr("&No and Exit Application"));
      box.button(QMessageBox::Yes)->setText(tr("&Erase"));

      int result = box.exec();

      if(result == QMessageBox::No)
        // User does not want to erase incompatible databases - exit application
        ok = false;
      else if(result == QMessageBox::Yes)
      {
        NavApp::closeSplashScreen();
        QMessageBox *simpleProgressDialog = atools::gui::Dialog::showSimpleProgressDialog(mainWindow, tr("Deleting ..."));
        atools::gui::Application::processEventsExtended();

        int i = 0;
        for(const QString& dbfile : databaseFiles)
        {
          simpleProgressDialog->setText(tr("Erasing database for %1 ...").arg(databaseNames.at(i)));
          atools::gui::Application::processEventsExtended();
          simpleProgressDialog->repaint();
          atools::gui::Application::processEventsExtended();

          if(QFile::remove(dbfile))
          {
            qInfo() << "Removed" << dbfile;

            // Create new database
            sqlDb.setDatabaseName(dbfile);
            sqlDb.open();
            createEmptySchema(&sqlDb);
            sqlDb.close();

            if(databasesErased != nullptr)
              *databasesErased = true;
          }
          else
          {
            qWarning() << "Removing database failed" << dbfile;
            atools::gui::Dialog::warning(mainWindow,
                                         tr("Deleting of database<br/><br/>\"%1\"<br/><br/>failed.<br/><br/>"
                                            "Remove the database file manually and restart the program.").arg(dbfile));
            ok = false;
          }
          i++;
        }
        atools::gui::Dialog::deleteSimpleProgressDialog(simpleProgressDialog);
      }
    }
  }
  return ok;
}

void DatabaseManager::checkCopyAndPrepareDatabases()
{
  QString appDb = buildDatabaseFileNameAppDir(FsPaths::NAVIGRAPH);
  QString settingsDb = buildDatabaseFileName(FsPaths::NAVIGRAPH);
  bool hasApp = false, hasSettings = false, settingsNeedsPreparation = false;

  QDateTime appLastLoad = QDateTime::fromMSecsSinceEpoch(0), settingsLastLoad = QDateTime::fromMSecsSinceEpoch(0);
  QString appCycle, settingsCycle;
  QString appSource, settingsSource;

  // Open databases and get loading timestamp from metadata
  if(QFile::exists(appDb))
  {
    // Database in application directory
    const atools::fs::db::DatabaseMeta appMeta = metaFromFile(appDb);
    appLastLoad = appMeta.getLastLoadTime();
    appCycle = appMeta.getAiracCycle();
    appSource = appMeta.getDataSource();
    hasApp = true;
  }

  if(QFile::exists(settingsDb))
  {
    // Database in settings directory
    const atools::fs::db::DatabaseMeta settingsMeta = metaFromFile(settingsDb);
    settingsLastLoad = settingsMeta.getLastLoadTime();
    settingsCycle = settingsMeta.getAiracCycle();
    settingsSource = settingsMeta.getDataSource();
    settingsNeedsPreparation = settingsMeta.hasScript();
    hasSettings = true;
  }
  int appCycleNum = appCycle.toInt();
  int settingsCycleNum = settingsCycle.toInt();

  qInfo() << Q_FUNC_INFO << "settings database" << settingsDb << settingsLastLoad << settingsCycle;
  qInfo() << Q_FUNC_INFO << "app database" << appDb << appLastLoad << appCycle;
  qInfo() << Q_FUNC_INFO << "hasApp" << hasApp << "hasSettings" << hasSettings << "settingsNeedsPreparation" << settingsNeedsPreparation;

  if(hasApp)
  {
    int result = QMessageBox::Yes;

    // Compare cycles first and then compilation time
    if(appCycleNum > settingsCycleNum || (appCycleNum == settingsCycleNum && appLastLoad > settingsLastLoad))
    {
      if(hasSettings)
      {
        NavApp::closeSplashScreen();
        result = dialog->showQuestionMsgBox(
          lnm::ACTIONS_SHOW_OVERWRITE_DATABASE,
          tr("Your current navdata is older than the navdata included in the Little Navmap download archive.<br/><br/>"
             "Overwrite the current navdata file with the new one?"
             "<hr/>Current file to overwrite:<br/><br/>"
             "<i>%1<br/><br/>"
             "%2, cycle %3, compiled on %4</i>"
             "<hr/>New file:<br/><br/>"
             "<i>%5<br/><br/>"
             "%6, cycle %7, compiled on %8</i><hr/><br/>"
             ).
          arg(settingsDb).
          arg(settingsSource).
          arg(settingsCycle).
          arg(QLocale().toString(settingsLastLoad, QLocale::ShortFormat)).
          arg(appDb).
          arg(appSource).
          arg(appCycle).
          arg(QLocale().toString(appLastLoad, QLocale::ShortFormat)),
          tr("Do not &show this dialog again and skip copying in the future."),
          QMessageBox::Yes | QMessageBox::No, QMessageBox::No, QMessageBox::No);
      }

      if(result == QMessageBox::Yes)
      {
        // We have a database in the application folder and it is newer than the one in the settings folder
        QMessageBox *simpleProgressDialog = atools::gui::Dialog::showSimpleProgressDialog(mainWindow,
                                                                                          tr("Preparing %1 Database ...").
                                                                                          arg(FsPaths::typeToName(FsPaths::NAVIGRAPH)));
        atools::gui::Application::processEventsExtended();

        bool resultRemove = true, resultCopy = false;
        // Remove target
        if(hasSettings)
        {
          resultRemove = QFile(settingsDb).remove();
          qDebug() << Q_FUNC_INFO << "removed" << settingsDb << resultRemove;
        }

        // Copy to target
        if(resultRemove)
        {
          simpleProgressDialog->setText(tr("Preparing %1 Database: Copying file ...").arg(FsPaths::typeToName(FsPaths::NAVIGRAPH)));
          atools::gui::Application::processEventsExtended();
          simpleProgressDialog->repaint();
          atools::gui::Application::processEventsExtended();
          resultCopy = QFile(appDb).copy(settingsDb);
          qDebug() << Q_FUNC_INFO << "copied" << appDb << "to" << settingsDb << resultCopy;
        }

        // Create indexes and delete script afterwards
        if(resultRemove && resultCopy)
        {
          SqlDatabase tempDb(DATABASE_NAME_TEMP);
          openDatabaseFile(&tempDb, settingsDb, false /* readonly */, true /* createSchema */);
          simpleProgressDialog->setText(tr("Preparing %1 Database: Creating indexes ...").arg(FsPaths::typeToName(FsPaths::NAVIGRAPH)));
          atools::gui::Application::processEventsExtended();
          simpleProgressDialog->repaint();
          atools::gui::Application::processEventsExtended();
          NavDatabase::runPreparationScript(tempDb);

          simpleProgressDialog->setText(tr("Preparing %1 Database: Analyzing ...").arg(FsPaths::typeToName(FsPaths::NAVIGRAPH)));
          atools::gui::Application::processEventsExtended();
          simpleProgressDialog->repaint();
          atools::gui::Application::processEventsExtended();
          tempDb.analyze();
          closeDatabaseFile(&tempDb);
          settingsNeedsPreparation = false;
        }
        atools::gui::Dialog::deleteSimpleProgressDialog(simpleProgressDialog);

        if(!resultRemove)
          atools::gui::Dialog::warning(mainWindow,
                                       tr("Deleting of database<br/><br/>\"%1\"<br/><br/>failed.<br/><br/>"
                                          "Remove the database file manually and restart the program.").arg(settingsDb));

        if(!resultCopy)
          atools::gui::Dialog::warning(mainWindow,
                                       tr("Cannot copy database<br/><br/>\"%1\"<br/><br/>to<br/><br/>"
                                          "\"%2\"<br/><br/>.").arg(appDb).arg(settingsDb));
      }
    }
  }

  if(settingsNeedsPreparation && hasSettings)
  {
    NavApp::closeSplashScreen();
    QMessageBox *simpleProgressDialog = atools::gui::Dialog::showSimpleProgressDialog(mainWindow, tr("Preparing %1 Database ...").
                                                                                      arg(FsPaths::typeToName(FsPaths::NAVIGRAPH)));
    atools::gui::Application::processEventsExtended();
    simpleProgressDialog->repaint();
    atools::gui::Application::processEventsExtended();

    SqlDatabase tempDb(DATABASE_NAME_TEMP);
    openDatabaseFile(&tempDb, settingsDb, false /* readonly */, true /* createSchema */);

    // Delete all tables that are not used in Little Navmap versions > 2.4.5
    if(atools::util::Version(QApplication::applicationVersion()) > atools::util::Version(2, 4, 5))
      NavDatabase::runPreparationPost245(tempDb);

    // Executes all statements like create index in the table script and deletes it afterwards
    NavDatabase::runPreparationScript(tempDb);

    tempDb.vacuum();
    tempDb.analyze();
    closeDatabaseFile(&tempDb);

    atools::gui::Dialog::deleteSimpleProgressDialog(simpleProgressDialog);
  }
}

bool DatabaseManager::isAirportDatabaseXPlane(bool navdata) const
{
  if(navdata)
    // Fetch from navdatabase - X-Plane airport only if navdata is not used
    return atools::fs::FsPaths::isAnyXplane(currentFsType) && navDatabaseStatus == dm::NAVDATABASE_OFF;
  else
    // Fetch from sim database - X-Plane airport only if navdata is not used for all
    return atools::fs::FsPaths::isAnyXplane(currentFsType) && navDatabaseStatus != dm::NAVDATABASE_ALL;
}

QString DatabaseManager::getCurrentSimulatorBasePath() const
{
  return getSimulatorBasePath(currentFsType);
}

QString DatabaseManager::getSimulatorBasePath(atools::fs::FsPaths::SimulatorType type) const
{
  return simulators.value(type).basePath;
}

QString DatabaseManager::getSimulatorFilesPathBest(const FsPaths::SimulatorTypeVector& types) const
{
  FsPaths::SimulatorType type = simulators.getBestInstalled(types);
  switch(type)
  {
    // All not depending on installation path which might be changed by the user
    case atools::fs::FsPaths::FSX:
    case atools::fs::FsPaths::FSX_SE:
    case atools::fs::FsPaths::P3D_V3:
    case atools::fs::FsPaths::P3D_V4:
    case atools::fs::FsPaths::P3D_V5:
    case atools::fs::FsPaths::MSFS:
      // Ignore user changes of path for now
      return FsPaths::getFilesPath(type);

    case atools::fs::FsPaths::XPLANE_11:
    case atools::fs::FsPaths::XPLANE_12:
      {
        // Might change with base path by user
        QString base = getSimulatorBasePath(type);
        if(!base.isEmpty())
          return atools::buildPathNoCase({base, "Output", "FMS plans"});
      }
      break;

    case atools::fs::FsPaths::DFD:
    case atools::fs::FsPaths::ALL_SIMULATORS:
    case atools::fs::FsPaths::NONE:
      break;
  }
  return QString();
}

QString DatabaseManager::getSimulatorBasePathBest(const FsPaths::SimulatorTypeVector& types) const
{
  FsPaths::SimulatorType type = simulators.getBestInstalled(types);
  switch(type)
  {
    // All not depending on installation path which might be changed by the user
    case atools::fs::FsPaths::FSX:
    case atools::fs::FsPaths::FSX_SE:
    case atools::fs::FsPaths::P3D_V3:
    case atools::fs::FsPaths::P3D_V4:
    case atools::fs::FsPaths::P3D_V5:
    case atools::fs::FsPaths::XPLANE_11:
    case atools::fs::FsPaths::XPLANE_12:
    case atools::fs::FsPaths::MSFS:
      return FsPaths::getBasePath(type);

    case atools::fs::FsPaths::DFD:
    case atools::fs::FsPaths::ALL_SIMULATORS:
    case atools::fs::FsPaths::NONE:
      return QString();
  }
  return QString();
}

atools::sql::SqlDatabase *DatabaseManager::getDatabaseOnline() const
{
  return onlinedataManager->getDatabase();
}

void DatabaseManager::insertSimSwitchActions()
{
  qDebug() << Q_FUNC_INFO;
  Ui::MainWindow *ui = NavApp::getMainUi();

  freeActions();

  // Create group to get radio button like behavior
  simDbGroup = new QActionGroup(ui->menuDatabase);
  simDbGroup->setExclusive(true);

  // Sort keys to avoid random order
  QList<FsPaths::SimulatorType> keys = simulators.keys();
  QList<FsPaths::SimulatorType> sims;
  std::sort(keys.begin(), keys.end(), [](FsPaths::SimulatorType t1, FsPaths::SimulatorType t2) {
    return FsPaths::typeToShortName(t1) < FsPaths::typeToShortName(t2);
  });

  // Add real simulators first
  for(atools::fs::FsPaths::SimulatorType type : keys)
  {
    const FsPathType& pathType = simulators.value(type);

    if(pathType.isInstalled || pathType.hasDatabase)
      // Create an action for each simulator installation or database found
      sims.append(type);
  }

  int index = 1;
  bool foundSim = false, foundDb = false;
  for(atools::fs::FsPaths::SimulatorType type : sims)
  {
    insertSimSwitchAction(type, ui->menuViewAirspaceSource->menuAction(), ui->menuDatabase, index++);
    foundSim |= simulators.value(type).isInstalled;
    foundDb |= simulators.value(type).hasDatabase;
  }

  // Insert disabled action if nothing was found at all ===============================
  if(!foundDb && !foundSim)
    insertSimSwitchAction(atools::fs::FsPaths::NONE, ui->menuViewAirspaceSource->menuAction(), ui->menuDatabase, index++);

  menuDbSeparator = ui->menuDatabase->insertSeparator(ui->menuViewAirspaceSource->menuAction());

  // Update Reload scenery item ===============================
  ui->actionReloadScenery->setEnabled(foundSim);
  if(foundSim)
    ui->actionReloadScenery->setText(tr("&Load Scenery Library ..."));
  else
    ui->actionReloadScenery->setText(tr("Load Scenery Library (no simulator)"));

  // Noting to select if there is only one option ========================
  if(actions.size() == 1)
    actions.constFirst()->setDisabled(true);

  // Insert Navigraph menu ==================================
  QString file = buildDatabaseFileName(FsPaths::NAVIGRAPH);

  if(!file.isEmpty())
  {
    const atools::fs::db::DatabaseMeta meta = metaFromFile(file);
    QString cycle = meta.getAiracCycle();
    QString suffix;

    if(!cycle.isEmpty())
      suffix = tr(" - AIRAC Cycle %1").arg(cycle);
    else
      suffix = tr(" - No AIRAC Cycle");

    if(!meta.hasData())
      suffix += tr(" (database is empty)");

#ifdef DEBUG_INFORMATION
    suffix += " (" + meta.getLastLoadTime().toString() + " | " + meta.getDataSource() + ")";
#endif

    QString dbname = FsPaths::typeToName(FsPaths::NAVIGRAPH);
    navDbSubMenu = new QMenu(tr("&%1%2").arg(dbname).arg(suffix));
    navDbSubMenu->setToolTipsVisible(NavApp::isMenuToolTipsVisible());
    navDbGroup = new QActionGroup(navDbSubMenu);

    navDbActionAll = new QAction(tr("Use %1 for &all Features").arg(dbname), navDbSubMenu);
    navDbActionAll->setCheckable(true);
    navDbActionAll->setChecked(navDatabaseStatus == dm::NAVDATABASE_ALL);
    navDbActionAll->setStatusTip(tr("Use all of %1 database features").arg(dbname));
    navDbActionAll->setActionGroup(navDbGroup);
    navDbSubMenu->addAction(navDbActionAll);

    navDbActionBlend = new QAction(tr("Use %1 for &Navaids and Procedures").arg(dbname), navDbSubMenu);
    navDbActionBlend->setCheckable(true);
    navDbActionBlend->setChecked(navDatabaseStatus == dm::NAVDATABASE_MIXED);
    navDbActionBlend->setStatusTip(tr("Use only navaids, airways, airspaces and procedures from %1 database").arg(dbname));
    navDbActionBlend->setActionGroup(navDbGroup);
    navDbSubMenu->addAction(navDbActionBlend);

    navDbActionOff = new QAction(tr("Do &not use %1 database").arg(dbname), navDbSubMenu);
    navDbActionOff->setCheckable(true);
    navDbActionOff->setChecked(navDatabaseStatus == dm::NAVDATABASE_OFF);
    navDbActionOff->setStatusTip(tr("Do not use %1 database").arg(dbname));
    navDbActionOff->setActionGroup(navDbGroup);
    navDbSubMenu->addAction(navDbActionOff);

    ui->menuDatabase->insertMenu(ui->menuViewAirspaceSource->menuAction(), navDbSubMenu);
    menuNavDbSeparator = ui->menuDatabase->insertSeparator(ui->menuViewAirspaceSource->menuAction());

    connect(navDbActionAll, &QAction::triggered, this, &DatabaseManager::switchNavFromMainMenu);
    connect(navDbActionBlend, &QAction::triggered, this, &DatabaseManager::switchNavFromMainMenu);
    connect(navDbActionOff, &QAction::triggered, this, &DatabaseManager::switchNavFromMainMenu);
  }
}

void DatabaseManager::insertSimSwitchAction(atools::fs::FsPaths::SimulatorType type, QAction *before, QMenu *menu, int index)
{
  if(type == atools::fs::FsPaths::NONE)
  {
    QAction *action = new QAction(tr("No Scenery Library and no Simulator found"), menu);
    action->setToolTip(tr("No scenery library database and no simulator found"));
    action->setStatusTip(action->toolTip());
    action->setData(QVariant::fromValue<atools::fs::FsPaths::SimulatorType>(type));
    action->setActionGroup(simDbGroup);

    menu->insertAction(before, action);
    actions.append(action);
  }
  else
  {
    QString suffix;
    QStringList atts;
    const atools::fs::db::DatabaseMeta meta = metaFromFile(buildDatabaseFileName(type));
    if(atools::fs::FsPaths::isAnyXplane(type))
    {
      QString cycle = meta.getAiracCycle();
      if(!cycle.isEmpty())
        suffix = tr(" - AIRAC Cycle %1").arg(cycle);
    }

    // Built string for hint ===============
    if(!meta.hasData())
      atts.append(tr("empty"));
    else
    {
      if(meta.getDatabaseVersion() < meta.getApplicationVersion())
        atts.append(tr("prev. version - reload advised"));
      else if(meta.getLastLoadTime() < QDateTime::currentDateTime().addDays(-MAX_AGE_DAYS))
      {
        qint64 days = meta.getLastLoadTime().date().daysTo(QDate::currentDate());
        atts.append(tr("%1 days old - reload advised").arg(days));
      }
    }

    if(!simulators.value(type).isInstalled)
      atts.append(tr("no simulator"));

    if(!atts.isEmpty())
      suffix.append(tr(" (%1)").arg(atts.join(tr(", "))));

    QAction *action = new QAction(tr("&%1 %2%3").arg(index).arg(FsPaths::typeToName(type)).arg(suffix), menu);
    action->setToolTip(tr("Switch to %1 database").arg(FsPaths::typeToName(type)));
    action->setStatusTip(action->toolTip());
    action->setData(QVariant::fromValue<atools::fs::FsPaths::SimulatorType>(type));
    action->setCheckable(true);
    action->setActionGroup(simDbGroup);

    if(type == currentFsType)
    {
      QSignalBlocker blocker(action);
      Q_UNUSED(blocker)
      action->setChecked(true);
    }

    menu->insertAction(before, action);

    connect(action, &QAction::triggered, this, &DatabaseManager::switchSimFromMainMenu);
    actions.append(action);
  }
}

/* User changed simulator in main menu */
void DatabaseManager::switchNavFromMainMenu()
{
  qDebug() << Q_FUNC_INFO;

  if(navDbActionAll->isChecked())
  {
    QUrl url = atools::gui::HelpHandler::getHelpUrlWeb(lnm::helpOnlineNavdatabasesUrl, lnm::helpLanguageOnline());
    QString message = tr(
      "<p>Note that airport information is limited in this mode.<br/>"
      "This means that aprons, taxiways, parking positions, runway surface information and other information is not available.<br/>"
      "Smaller airports might be missing and runway layout might not match the runway layout in the simulator.</p>"
      "<p><a href=\"%1\">Click here for more information in the Little Navmap online manual</a></p>").arg(url.toString());

    dialog->showInfoMsgBox(lnm::ACTIONS_SHOW_NAVDATA_WARNING, message, QObject::tr("Do not &show this dialog again."));
  }

  QGuiApplication::setOverrideCursor(Qt::WaitCursor);

  // Disconnect all queries
  emit preDatabaseLoad();

  clearLanguageIndex();
  closeAllDatabases();

  QString text;
  if(navDbActionAll->isChecked())
  {
    navDatabaseStatus = dm::NAVDATABASE_ALL;
    text = tr("Enabled all features for %1.");
  }
  else if(navDbActionBlend->isChecked())
  {
    navDatabaseStatus = dm::NAVDATABASE_MIXED;
    text = tr("Enabled navaids, airways, airspaces and procedures for %1.");
  }
  else if(navDbActionOff->isChecked())
  {
    navDatabaseStatus = dm::NAVDATABASE_OFF;
    text = tr("Disabled %1.");
  }
  qDebug() << Q_FUNC_INFO << "usingNavDatabase" << navDatabaseStatus;

  openAllDatabases();
  loadLanguageIndex();

  QGuiApplication::restoreOverrideCursor();

  emit postDatabaseLoad(currentFsType);

  mainWindow->setStatusMessage(text.arg(FsPaths::typeToName(FsPaths::NAVIGRAPH)));

  saveState();

}

void DatabaseManager::switchSimFromMainMenu()
{
  QAction *action = dynamic_cast<QAction *>(sender());

  qDebug() << Q_FUNC_INFO << (action != nullptr ? action->text() : "null");

  if(action != nullptr && currentFsType != action->data().value<atools::fs::FsPaths::SimulatorType>())
  {
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);

    // Disconnect all queries
    emit preDatabaseLoad();

    clearLanguageIndex();
    closeAllDatabases();

    // Set new simulator
    currentFsType = action->data().value<atools::fs::FsPaths::SimulatorType>();
    openAllDatabases();
    loadLanguageIndex();

    QGuiApplication::restoreOverrideCursor();

    // Reopen all with new database
    emit postDatabaseLoad(currentFsType);
    mainWindow->setStatusMessage(tr("Switched to %1.").arg(FsPaths::typeToName(currentFsType)));

    saveState();
    checkDatabaseVersion();
  }

  // Check and uncheck manually since the QActionGroup is unreliable
  for(QAction *act : actions)
  {
    QSignalBlocker blocker(act);
    Q_UNUSED(blocker)
    act->setChecked(act->data().value<atools::fs::FsPaths::SimulatorType>() == currentFsType);
  }
}

void DatabaseManager::openWriteableDatabase(atools::sql::SqlDatabase *database, const QString& name,
                                            const QString& displayName, bool backup)
{
  QString databaseName = databaseDirectory + QDir::separator() + lnm::DATABASE_PREFIX + name + lnm::DATABASE_SUFFIX;

  QString databaseNameBackup = databaseDirectory + QDir::separator() + QFileInfo(databaseName).baseName() + "_backup" +
                               lnm::DATABASE_SUFFIX;

  try
  {
    if(backup)
    {
      // Roll copies
      // .../ABarthel/little_navmap_db/little_navmap_userdata_backup.sqlite
      // .../ABarthel/little_navmap_db/little_navmap_userdata_backup.sqlite.1
      atools::io::FileRoller roller(1);
      roller.rollFile(databaseNameBackup);

      // Copy database before opening
      bool result = QFile(databaseName).copy(databaseNameBackup);
      qInfo() << Q_FUNC_INFO << "Copied" << databaseName << "to" << databaseNameBackup << "result" << result;
    }

    openDatabaseFileInternal(database, databaseName, false /* readonly */, false /* createSchema */,
                             false /* exclusive */, false /* auto transactions */);
  }
  catch(atools::sql::SqlException& e)
  {
    QMessageBox::critical(mainWindow, QApplication::applicationName(),
                          tr("Cannot open %1 database. Reason:<br/><br/>"
                             "%2<br/><br/>"
                             "Is another instance of <i>%3</i> running?<br/><br/>"
                             "Exiting now.").
                          arg(displayName).
                          arg(e.getSqlError().databaseText()).
                          arg(QApplication::applicationName()));

    std::exit(1);
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
  }
}

void DatabaseManager::closeUserDatabase()
{
  closeDatabaseFile(databaseUser);
}

void DatabaseManager::closeTrackDatabase()
{
  closeDatabaseFile(databaseTrack);
}

void DatabaseManager::closeUserAirspaceDatabase()
{
  closeDatabaseFile(databaseUserAirspace);
}

void DatabaseManager::closeLogDatabase()
{
  closeDatabaseFile(databaseLogbook);
}

void DatabaseManager::closeOnlineDatabase()
{
  closeDatabaseFile(databaseOnline);
}

void DatabaseManager::clearLanguageIndex()
{
  languageIndex->clear();
}

void DatabaseManager::loadLanguageIndex()
{
  if(SqlUtil(databaseSim).hasTableAndRows("translation"))
    languageIndex->readFromDb(databaseSim, OptionData::instance().getLanguage());
}

void DatabaseManager::openAllDatabases()
{
  QString simDbFile = buildDatabaseFileName(currentFsType);
  QString navDbFile = buildDatabaseFileName(FsPaths::NAVIGRAPH);

  // Airspace databases are independent of switch
  QString simAirspaceDbFile = simDbFile;
  QString navAirspaceDbFile = navDbFile;

  if(navDatabaseStatus == dm::NAVDATABASE_ALL)
    simDbFile = navDbFile;
  else if(navDatabaseStatus == dm::NAVDATABASE_OFF)
    navDbFile = simDbFile;
  // else if(usingNavDatabase == MIXED)

  openDatabaseFile(databaseSim, simDbFile, true /* readonly */, true /* createSchema */);
  openDatabaseFile(databaseNav, navDbFile, true /* readonly */, true /* createSchema */);

  openDatabaseFile(databaseSimAirspace, simAirspaceDbFile, true /* readonly */, true /* createSchema */);
  openDatabaseFile(databaseNavAirspace, navAirspaceDbFile, true /* readonly */, true /* createSchema */);
}

void DatabaseManager::openDatabaseFile(atools::sql::SqlDatabase *db, const QString& file, bool readonly, bool createSchema)
{
  try
  {
    openDatabaseFileInternal(db, file, readonly, createSchema, true /* exclusive */, true /* auto transactions */);
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
  }
}

void DatabaseManager::openDatabaseFileInternal(atools::sql::SqlDatabase *db, const QString& file, bool readonly,
                                               bool createSchema, bool exclusive, bool autoTransactions)
{
  atools::settings::Settings& settings = atools::settings::Settings::instance();
  int databaseCacheKb = settings.getAndStoreValue(lnm::SETTINGS_DATABASE + "CacheKb", 50000).toInt();
  bool foreignKeys = settings.getAndStoreValue(lnm::SETTINGS_DATABASE + "ForeignKeys", false).toBool();

  // cache_size * 1024 bytes if value is negative
  QStringList databasePragmas({QString("PRAGMA cache_size=-%1").arg(databaseCacheKb), "PRAGMA page_size=8196"});

  if(exclusive)
  {
    // Best settings for loading databases accessed write only - unsafe
    databasePragmas.append("PRAGMA locking_mode=EXCLUSIVE");
    databasePragmas.append("PRAGMA journal_mode=TRUNCATE");
    databasePragmas.append("PRAGMA synchronous=OFF");
  }
  else
  {
    // Best settings for online and user databases which are updated often - read/write
    databasePragmas.append("PRAGMA locking_mode=NORMAL");
    databasePragmas.append("PRAGMA journal_mode=DELETE");
    databasePragmas.append("PRAGMA synchronous=NORMAL");
  }

  if(!readonly)
    databasePragmas.append("PRAGMA busy_timeout=2000");

  qDebug() << Q_FUNC_INFO << "Opening database" << file;
  db->setDatabaseName(file);

  // Set foreign keys only on demand because they can decrease loading performance
  if(foreignKeys)
    databasePragmas.append("PRAGMA foreign_keys = ON");
  else
    databasePragmas.append("PRAGMA foreign_keys = OFF");

  bool autocommit = db->isAutocommit();
  db->setAutocommit(false);
  db->setAutomaticTransactions(autoTransactions);
  db->open(databasePragmas);

  db->setAutocommit(autocommit);

  if(createSchema)
  {
    if(!hasSchema(db))
    {
      if(db->isReadonly())
      {
        // Reopen database read/write
        db->close();
        db->setReadonly(false);
        db->open(databasePragmas);
      }

      createEmptySchema(db);
    }
  }

  if(readonly && !db->isReadonly())
  {
    // Readonly requested - reopen database
    db->close();
    db->setReadonly();
    db->open(databasePragmas);
  }

  DatabaseMeta(db).logInfo();
}

void DatabaseManager::closeAllDatabases()
{
  closeDatabaseFile(databaseSim);
  closeDatabaseFile(databaseNav);
  closeDatabaseFile(databaseSimAirspace);
  closeDatabaseFile(databaseNavAirspace);
}

void DatabaseManager::closeDatabaseFile(atools::sql::SqlDatabase *db)
{
  try
  {
    if(db != nullptr && db->isOpen())
    {
      qDebug() << Q_FUNC_INFO << "Closing database" << db->databaseName();
      db->close();
    }
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
  }
}

atools::sql::SqlDatabase *DatabaseManager::getDatabaseSim()
{
  return databaseSim;
}

atools::sql::SqlDatabase *DatabaseManager::getDatabaseNav()
{
  return databaseNav;
}

atools::sql::SqlDatabase *DatabaseManager::getDatabaseSimAirspace()
{
  return databaseSimAirspace;
}

atools::sql::SqlDatabase *DatabaseManager::getDatabaseNavAirspace()
{
  return databaseNavAirspace;
}

void DatabaseManager::checkForChangedNavAndSimDatabases()
{
  if(!showingDatabaseChangeWarning)
  {
    showingDatabaseChangeWarning = true;
    if(QGuiApplication::applicationState() & Qt::ApplicationActive)
    {
#ifdef DEBUG_INFORMATION
      qDebug() << Q_FUNC_INFO;
#endif

      QStringList files;
      if(databaseSim != nullptr && databaseSim->isOpen() && databaseSim->isFileModified())
        files.append(QDir::toNativeSeparators(databaseSim->databaseName()));

      if(databaseNav != nullptr && databaseNav->isOpen() && databaseNav->isFileModified())
        files.append(QDir::toNativeSeparators(databaseNav->databaseName()));
      files.removeDuplicates();
      if(!files.isEmpty())
      {
        QMessageBox::warning(mainWindow, QApplication::applicationName(),
                             tr("<p style=\"white-space:pre\">"
                                  "Detected a modification of one or more database files:<br/><br/>"
                                  "&quot;%1&quot;"
                                  "<br/><br/>"
                                  "Always close <i>%2</i> before copying, overwriting or updating scenery library databases.</p>").
                             arg(files.join(tr("&quot;<br/>&quot;"))).
                             arg(QApplication::applicationName()));

        databaseNav->recordFileMetadata();
        databaseSim->recordFileMetadata();
      }
    }
    showingDatabaseChangeWarning = false;
  }
}

void DatabaseManager::run()
{
  qDebug() << Q_FUNC_INFO;

  if(simulators.value(currentFsType).isInstalled)
    // Use what is currently displayed on the map
    selectedFsType = currentFsType;

  databaseDialog->setCurrentFsType(selectedFsType);
  databaseDialog->setReadInactive(readInactive);
  databaseDialog->setReadAddOnXml(readAddOnXml);

  updateDialogInfo(selectedFsType);

  // try until user hits cancel or the database was loaded successfully
  atools::fs::ResultFlags resultFlags = atools::fs::NONE;
  while(runInternal(resultFlags))
    ;

  updateSimulatorFlags();
  insertSimSwitchActions();

  saveState();

  if(!resultFlags.testFlag(atools::fs::COMPILE_ABORTED))
  {
    if(currentFsType == atools::fs::FsPaths::MSFS)
    {
      // Notify user and correct scenery mode after loading MSFS ==============================================

      if(resultFlags.testFlag(atools::fs::COMPILE_MSFS_NAVIGRAPH_FOUND))
      {
        if(navDatabaseStatus != dm::NAVDATABASE_MIXED)
        {
          // Navigraph update for MSFS used - Use Navigraph for Navaids and Procedures

          int result = dialog->showQuestionMsgBox(lnm::ACTIONS_SHOW_DATABASE_MSFS_NAVIGRAPH,
                                                  tr("<p>You are using MSFS with the Navigraph navdata update.</p>"
                                                       "<p>You have to update the Little Navmap navdata with the "
                                                         "Navigraph FMS Data Manager and use the right scenery library mode "
                                                         "\"Use Navigraph for Navaids and Procedures\" "
                                                         "to avoid issues with airport information in Little Navmap.</p>"
                                                         "<p>You can change the mode manually in the menu \"Scenery Library\" -> "
                                                           "\"Navigraph\" -> \"Use Navigraph for Navaids and Procedures\".</p>"
                                                           "<p>Correct the scenery library mode now?</p>", "Sync texts with menu items"),
                                                  tr("Do not &show this dialog again and always correct mode after loading."),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes, QMessageBox::Yes);

          if(result == QMessageBox::Yes)
          {
            navDbActionBlend->setChecked(true);
            switchNavFromMainMenu(); // Need to call manually since triggered does not signal on programmatic activation
          }
        }
      }
      else
      {
        if(navDatabaseStatus != dm::NAVDATABASE_OFF)
        {
          // not use the Navigraph update for MSFS - Do not use Navigraph Database

          int result = dialog->showQuestionMsgBox(lnm::ACTIONS_SHOW_DATABASE_MSFS_NAVIGRAPH_OFF,
                                                  tr("<p>You are using MSFS without the Navigraph navdata update.</p>"
                                                       "<p>You have to use the scenery library mode \"Do not use Navigraph Database\" "
                                                         "to avoid issues with airport information in Little Navmap.</p>"
                                                         "<p>You can change this manually in menu \"Scenery Library\" -> "
                                                           "\"Navigraph\" -> \"Do not use Navigraph Database\".</p>"
                                                           "<p>Correct the scenery library mode now?</p>", "Sync texts with menu items"),
                                                  tr("Do not &show this dialog again and always correct mode after loading."),
                                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes, QMessageBox::Yes);

          if(result == QMessageBox::Yes)
          {
            navDbActionOff->setChecked(true);
            switchNavFromMainMenu(); // Need to call manually since triggered does not signal on programmatic activation
          }
        }
      }
    }
    else if(navDatabaseStatus == dm::NAVDATABASE_ALL)
    {
      // Notify user and correct scenery mode  ==============================================
      int result = dialog->showQuestionMsgBox(lnm::ACTIONS_SHOW_DATABASE_MSFS_NAVIGRAPH_ALL,
                                              tr("<p>Your current scenery library mode is \"Use Navigraph for all Features\".</p>"
                                                   "<p>Note that airport information is limited in this mode. "
                                                     "This means that aprons, taxiways, parking positions, runway surfaces and more are not available, "
                                                     "smaller airports will be missing and the runway layout might not match the one in the simulator.</p>"
                                                     "<p>You can change this manually in menu \"Scenery Library\" -> "
                                                       "\"Navigraph\" -> \"Use Navigraph for Navaids and Procedures\".</p>"
                                                       "<p>Correct the scenery library mode now?</p>", "Sync texts with menu items"),
                                              tr("Do not &show this dialog again and always correct mode after loading."),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes, QMessageBox::Yes);

      if(result == QMessageBox::Yes)
      {
        navDbActionBlend->setChecked(true);
        switchNavFromMainMenu(); // Need to call manually since triggered does not signal on programmatic activation
      }
    }
  } // if(!resultFlags.testFlag(atools::fs::COMPILE_ABORTED))
}

/* Shows scenery database loading dialog.
 * @return true if execution was successfull. false if it was cancelled */
bool DatabaseManager::runInternal(atools::fs::ResultFlags& resultFlags)
{
  qDebug() << Q_FUNC_INFO;

  bool reopenDialog = true;
  try
  {
    // Show loading dialog
    int retval = databaseDialog->exec();

    // Copy the changed path structures also if the dialog was closed only
    updateSimulatorPathsFromDialog();

    // Get the simulator database we'll update/load
    selectedFsType = databaseDialog->getCurrentFsType();

    readInactive = databaseDialog->isReadInactive();
    readAddOnXml = databaseDialog->isReadAddOnXml();

    if(retval == QDialog::Accepted)
    {
      using atools::gui::Dialog;

      bool configValid = true;
      QStringList errors;
      if(!NavDatabase::isBasePathValid(databaseDialog->getBasePath(), errors, selectedFsType))
      {
        QString resetPath(tr("<p>Click \"Reset paths\" in the dialog \"Load Scenery Library\" for a possible fix.</p>"));
        if(selectedFsType == atools::fs::FsPaths::MSFS)
        {
          // Check if base path is valid - all simulators ========================================================
          Dialog::warning(databaseDialog,
                          tr("<p style='white-space:pre'>Cannot read base path \"%1\".<br/><br/>"
                             "Reason:<br/>"
                             "%2<br/><br/>"
                             "Either the \"OneStore\" or the \"Steam\" paths have to exist.<br/>"
                             "The path \"Community\" is always needed for add-ons.</p>%3").
                          arg(databaseDialog->getBasePath()).arg(errors.join("<br/>")).arg(resetPath));
        }
        else
        {
          Dialog::warning(databaseDialog,
                          tr("<p style='white-space:pre'>Cannot read base path \"%1\".<br/><br/>"
                             "Reason:<br/>"
                             "%2</p>%3").
                          arg(databaseDialog->getBasePath()).arg(errors.join("<br/>")).arg(resetPath));
        }
        configValid = false;
      }

      // Do further checks if basepath is valid =================
      if(configValid)
      {
        if(atools::fs::FsPaths::isAnyXplane(selectedFsType))
        {
          // Check scenery_packs.ini for X-Plane ========================================================
          QString filepath;
          if(!readInactive && !atools::fs::xp::SceneryPacks::exists(databaseDialog->getBasePath(), errors, filepath))
          {
            Dialog::warning(databaseDialog,
                            tr("<p style='white-space:pre'>Cannot read scenery configuration \"%1\".<br/><br/>"
                               "Reason:<br/>"
                               "%2<br/><br/>"
                               "Enable the option \"Read inactive or disabled Scenery Entries\"<br/>"
                               "or start X-Plane once to create the file.</p>").
                            arg(filepath).arg(errors.join("<br/>")));
            configValid = false;
          }
        }
        else if(selectedFsType != atools::fs::FsPaths::MSFS)
        {
          // Check scenery.cfg for FSX and P3D ========================================================
          QString sceneryCfgCodec = (selectedFsType == atools::fs::FsPaths::P3D_V4 ||
                                     selectedFsType == atools::fs::FsPaths::P3D_V5) ? "UTF-8" : QString();

          if(!NavDatabase::isSceneryConfigValid(databaseDialog->getSceneryConfigFile(), sceneryCfgCodec, errors))
          {
            Dialog::warning(databaseDialog,
                            tr("<p style='white-space:pre'>Cannot read scenery configuration \"%1\".<br/><br/>"
                               "Reason:<br/>"
                               "%2</p>").
                            arg(databaseDialog->getSceneryConfigFile()).arg(errors.join("<br/>")));
            configValid = false;
          }
        }
      } // if(configValid)

      // Start compilation if all is valid ====================================================
      if(configValid)
      {
        // Compile into a temporary database file
        QString selectedFilename = buildDatabaseFileName(selectedFsType);
        QString tempFilename = buildCompilingDatabaseFileName();

        if(QFile::remove(tempFilename))
          qInfo() << "Removed" << tempFilename;
        else
          qWarning() << "Removing" << tempFilename << "failed";

        QFile journal(tempFilename + "-journal");
        if(journal.exists() && journal.size() == 0)
        {
          if(journal.remove())
            qInfo() << "Removed" << journal.fileName();
          else
            qWarning() << "Removing" << journal.fileName() << "failed";
        }

        SqlDatabase tempDb(DATABASE_NAME_TEMP);
        openDatabaseFile(&tempDb, tempFilename, false /* readonly */, true /* createSchema */);

        if(loadScenery(&tempDb, resultFlags))
        {
          // Successfully loaded
          reopenDialog = false;

          clearLanguageIndex();
          closeDatabaseFile(&tempDb);

          emit preDatabaseLoad();
          closeAllDatabases();

          // Remove old database
          if(QFile::remove(selectedFilename))
            qInfo() << "Removed" << selectedFilename;
          else
            qWarning() << "Removing" << selectedFilename << "failed";

          // Rename temporary file to new database
          if(QFile::rename(tempFilename, selectedFilename))
            qInfo() << "Renamed" << tempFilename << "to" << selectedFilename;
          else
            qWarning() << "Renaming" << tempFilename << "to" << selectedFilename << "failed";

          // Syncronize display with loaded database
          currentFsType = selectedFsType;

          openAllDatabases();
          loadLanguageIndex();
          emit postDatabaseLoad(currentFsType);
        }
        else
        {
          closeDatabaseFile(&tempDb);
          if(QFile::remove(tempFilename))
            qInfo() << "Removed" << tempFilename;
          else
            qWarning() << "Removing" << tempFilename << "failed";

          QFile journal2(tempFilename + "-journal");
          if(journal2.exists() && journal2.size() == 0)
          {
            if(journal2.remove())
              qInfo() << "Removed" << journal2.fileName();
            else
              qWarning() << "Removing" << journal2.fileName() << "failed";
          }
        }
      } // if(configValid)
    }
    else
    {
      // User hit close
      resultFlags |= atools::fs::COMPILE_ABORTED;
      reopenDialog = false;
    }
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
  }
  return reopenDialog;
}

/* Opens progress dialog and loads scenery
 * @return true if loading was successfull. false if cancelled or an error occured */
bool DatabaseManager::loadScenery(atools::sql::SqlDatabase *db, atools::fs::ResultFlags& resultFlags)
{
  using atools::fs::NavDatabaseOptions;

  bool success = true;
  // Get configuration file path from resources or overloaded path
  QString config = Settings::getOverloadedPath(lnm::DATABASE_NAVDATAREADER_CONFIG);
  qInfo() << Q_FUNC_INFO << "Config file" << config << "Database" << db->databaseName();

  QSettings settings(config, QSettings::IniFormat);

  NavDatabaseOptions navDatabaseOpts;
  navDatabaseOpts.loadFromSettings(settings);

  navDatabaseOpts.setReadInactive(readInactive);
  navDatabaseOpts.setReadAddOnXml(readAddOnXml);
  navDatabaseOpts.setLanguage(OptionsDialog::getLocale());

  // Add exclude paths from option dialog
  const OptionData& optionData = OptionData::instance();
  navDatabaseOpts.addToAddonDirectoryExcludes(optionData.getDatabaseAddonExclude());

  for(const QString& fileOrPath : optionData.getDatabaseExclude())
  {
    QFileInfo fileInfo(fileOrPath);

    if(fileInfo.exists())
    {
      if(QFileInfo(fileOrPath).isDir())
      {
        qInfo() << Q_FUNC_INFO << "Directory exclusion" << fileOrPath;
        navDatabaseOpts.addToDirectoryExcludesGui({fileOrPath});
      }
      else
      {
        qInfo() << Q_FUNC_INFO << "File exclusion" << fileOrPath;
        navDatabaseOpts.addToFilePathExcludesGui({fileOrPath});
      }
    }
    else
      qWarning() << Q_FUNC_INFO << "Exclusion does not exist" << fileOrPath;
  }

  navDatabaseOpts.setSimulatorType(selectedFsType);

  delete progressDialog;
  progressDialog = new DatabaseProgressDialog(mainWindow, atools::fs::FsPaths::typeToShortName(selectedFsType));

  QString basePath = simulators.value(selectedFsType).basePath;
  navDatabaseOpts.setSceneryFile(simulators.value(selectedFsType).sceneryCfg);
  navDatabaseOpts.setBasepath(basePath);

  if(selectedFsType == atools::fs::FsPaths::MSFS)
  {
    navDatabaseOpts.setMsfsCommunityPath(FsPaths::getMsfsCommunityPath(basePath));
    navDatabaseOpts.setMsfsOfficialPath(FsPaths::getMsfsOfficialPath(basePath));
  }
  else
  {
    navDatabaseOpts.setMsfsCommunityPath(QString());
    navDatabaseOpts.setMsfsOfficialPath(QString());
  }

  QElapsedTimer timer;
  progressTimerElapsed = 0L;

  progressDialog->setLabelText(
    databaseTimeText.arg(tr("Counting files ...")).
    arg(QString()).
    arg(QString()).arg(QString()).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0));

  // Dialog does not close when clicking cancel
  progressDialog->show();

  atools::gui::Application::processEventsExtended();
  progressDialog->repaint();
  atools::gui::Application::processEventsExtended();

  navDatabaseOpts.setProgressCallback(std::bind(&DatabaseManager::progressCallback, this, std::placeholders::_1, timer));

  // Let the dialog close and show the busy pointer
  QApplication::processEvents();
  atools::fs::NavDatabaseErrors errors;

  qInfo() << Q_FUNC_INFO << "==========================================================";
  qInfo() << Q_FUNC_INFO << navDatabaseOpts;
  qInfo() << Q_FUNC_INFO << "==========================================================";

  try
  {
    atools::fs::NavDatabase navDatabase(&navDatabaseOpts, db, &errors, GIT_REVISION);
    QString sceneryCfgCodec = (selectedFsType == atools::fs::FsPaths::P3D_V4 ||
                               selectedFsType == atools::fs::FsPaths::P3D_V5) ? "UTF-8" : QString();
    resultFlags = navDatabase.create(sceneryCfgCodec);
    qDebug() << Q_FUNC_INFO << "resultFlags" << resultFlags;
  }
  catch(atools::Exception& e)
  {
    // Show dialog if something went wrong but do not exit
    NavApp::closeSplashScreen();
    ErrorHandler(progressDialog).handleException(e, currentBglFilePath.isEmpty() ?
                                                 QString() : tr("Processed files:\n%1\n").arg(currentBglFilePath));
    success = false;
  }
  catch(...)
  {
    // Show dialog if something went wrong but do not exit
    NavApp::closeSplashScreen();
    ErrorHandler(progressDialog).handleUnknownException(currentBglFilePath.isEmpty() ?
                                                        QString() : tr("Processed files:\n%1\n").arg(currentBglFilePath));
    success = false;
  }

  QApplication::processEvents();

  // Show errors that occured during loading, if any
  if(errors.getTotalErrors() > 0)
  {
    QString errorTexts;
    errorTexts.append(tr("<h3>Found %1 errors in %2 scenery entries when loading the scenery database</h3>").
                      arg(errors.getTotalErrors()).arg(errors.sceneryErrors.size()));

    errorTexts.append(tr("<b>If you wish to report this error attach the log and configuration files "
                           "to your report, add all other available information and send it to one "
                           "of the contact addresses below.</b>"
                           "<hr/>%1"
                             "<hr/>%2").
                      arg(atools::gui::Application::getContactHtml()).
                      arg(atools::gui::Application::getReportPathHtml()));

    errorTexts.append(tr("<hr/>Some files or scenery directories could not be read.<br/>"
                         "You should check if the airports of the affected sceneries display "
                         "correctly and show the correct information.<hr/>"));

    int numScenery = 0;
    for(const atools::fs::NavDatabaseErrors::SceneryErrors& scErr : errors.sceneryErrors)
    {
      if(numScenery >= MAX_ERROR_SCENERY_MESSAGES)
      {
        errorTexts.append(tr("<b>More scenery entries ...</b>"));
        break;
      }

      int numBgl = 0;
      errorTexts.append(tr("<b>Scenery Title: %1</b><br/>").arg(scErr.scenery.getTitle()));

      for(const QString& err : scErr.sceneryErrorsMessages)
        errorTexts.append(err + "<br/>");

      for(const atools::fs::NavDatabaseErrors::SceneryFileError& bglErr : scErr.fileErrors)
      {
        if(numBgl >= MAX_ERROR_BGL_MESSAGES)
        {
          errorTexts.append(tr("<b>More files ...</b>"));
          break;
        }
        numBgl++;

        errorTexts.append(tr("<b>File:</b> \"%1\"<br/><b>Error:</b> %2<br/>").
                          arg(bglErr.filepath).arg(bglErr.errorMessage));
      }
      errorTexts.append("<br/>");
      numScenery++;
    }

    TextDialog errorDialog(progressDialog,
                           QApplication::applicationName() + tr(" - Load Scenery Library Errors"),
                           "SCENERY.html#errors"); // anchor for future use
    errorDialog.setHtmlMessage(errorTexts, true /* print to log */);
    errorDialog.exec();
  }

  QApplication::processEvents();
  if(!progressDialog->wasCanceled() && success)
  {
    // Show results and wait until user selects ok
    progressDialog->setOkButton();
    progressDialog->exec();
  }
  else
    // Loading was cancelled
    success = false;

  delete progressDialog;
  progressDialog = nullptr;

  return success;
}

/* Simulator was changed in scenery database loading dialog */
void DatabaseManager::simulatorChangedFromComboBox(FsPaths::SimulatorType value)
{
  selectedFsType = value;
  updateDialogInfo(selectedFsType);
}

/* Called by atools::fs::NavDatabase. Updates progress bar and statistics */
bool DatabaseManager::progressCallback(const atools::fs::NavDatabaseProgress& progress, QElapsedTimer& timer)
{
  if(progressDialog->wasCanceled())
    return true;

  if(progress.isFirstCall())
  {
    timer.start();
    progressDialog->setValue(progress.getCurrent());
    progressDialog->setMinimum(0);
    progressDialog->setMaximum(progress.getTotal());
  }

  // Update only four times a second
  if((timer.elapsed() - progressTimerElapsed) > 250 || progress.isLastCall())
  {
    progressDialog->setValue(progress.getCurrent());

    if(progress.isNewOther())
    {
      currentBglFilePath.clear();

      // Run script etc.
      progressDialog->setLabelText(
        databaseTimeText.arg(atools::elideTextShortMiddle(progress.getOtherAction(), MAX_TEXT_LENGTH)).
        arg(formatter::formatElapsed(timer)).
        arg(QString()).
        arg(QString()).
        arg(progress.getNumErrors()).
        arg(progress.getNumFiles()).
        arg(progress.getNumAirports()).
        arg(progress.getNumVors()).
        arg(progress.getNumIls()).
        arg(progress.getNumNdbs()).
        arg(progress.getNumMarker()).
        arg(progress.getNumWaypoints()).
        arg(progress.getNumBoundaries()));
    }
    else if(progress.isNewSceneryArea() || progress.isNewFile())
    {
      currentBglFilePath = progress.getBglFilePath();

      // Switched to a new scenery area
      progressDialog->setLabelText(
        databaseLoadingText.arg(atools::elideTextShortMiddle(progress.getSceneryTitle(), MAX_TEXT_LENGTH)).
        arg(atools::elideTextShortMiddle(progress.getSceneryPath(), MAX_TEXT_LENGTH)).
        arg(atools::elideTextShortMiddle(progress.getBglFileName(), MAX_TEXT_LENGTH)).
        arg(formatter::formatElapsed(timer)).
        arg(progress.getNumErrors()).
        arg(progress.getNumFiles()).
        arg(progress.getNumAirports()).
        arg(progress.getNumVors()).
        arg(progress.getNumIls()).
        arg(progress.getNumNdbs()).
        arg(progress.getNumMarker()).
        arg(progress.getNumWaypoints()).
        arg(progress.getNumBoundaries()));
    }
    else if(progress.isLastCall())
    {
      currentBglFilePath.clear();
      progressDialog->setValue(progress.getTotal());

      // Last report
      progressDialog->setLabelText(
        databaseTimeText.arg(tr("<big>Done.</big>")).
        arg(formatter::formatElapsed(timer)).
        arg(QString()).
        arg(QString()).
        arg(progress.getNumErrors()).
        arg(progress.getNumFiles()).
        arg(progress.getNumAirports()).
        arg(progress.getNumVors()).
        arg(progress.getNumIls()).
        arg(progress.getNumNdbs()).
        arg(progress.getNumMarker()).
        arg(progress.getNumWaypoints()).
        arg(progress.getNumBoundaries()));
    }

    QApplication::processEvents();
    progressTimerElapsed = timer.elapsed();
  }

  return progressDialog->wasCanceled();
}

/* Checks if the current database has a schema. Exits program if this fails */
bool DatabaseManager::hasSchema(atools::sql::SqlDatabase *db)
{
  try
  {
    return DatabaseMeta(db).hasSchema();
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
  }
}

/* Checks if the current database contains data. Exits program if this fails */
bool DatabaseManager::hasData(atools::sql::SqlDatabase *db)
{
  try
  {
    return DatabaseMeta(db).hasData();
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
  }
}

/* Checks if the current database is compatible with this program. Exits program if this fails */
bool DatabaseManager::isDatabaseCompatible(atools::sql::SqlDatabase *db)
{
  try
  {
    return DatabaseMeta(db).isDatabaseCompatible();
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
  }
}

void DatabaseManager::createEmptySchema(atools::sql::SqlDatabase *db, bool boundary)
{
  try
  {
    NavDatabaseOptions opts;
    if(boundary)
      // Does not use a transaction
      NavDatabase(&opts, db, nullptr, GIT_REVISION).createAirspaceSchema();
    else
    {
      NavDatabase(&opts, db, nullptr, GIT_REVISION).createSchema();
      DatabaseMeta(db).updateVersion();
    }
  }
  catch(atools::Exception& e)
  {
    ATOOLS_HANDLE_EXCEPTION(e);
  }
  catch(...)
  {
    ATOOLS_HANDLE_UNKNOWN_EXCEPTION;
  }
}

bool DatabaseManager::hasInstalledSimulators() const
{
  return !simulators.getAllInstalled().isEmpty();
}

bool DatabaseManager::hasSimulatorDatabases() const
{
  return !simulators.getAllHavingDatabase().isEmpty();
}

void DatabaseManager::saveState()
{
  Settings& s = Settings::instance();
  s.setValueVar(lnm::DATABASE_PATHS, QVariant::fromValue(simulators));
  s.setValue(lnm::DATABASE_SIMULATOR, atools::fs::FsPaths::typeToShortName(currentFsType));
  s.setValue(lnm::DATABASE_LOADINGSIMULATOR, atools::fs::FsPaths::typeToShortName(selectedFsType));
  s.setValue(lnm::DATABASE_LOAD_INACTIVE, readInactive);
  s.setValue(lnm::DATABASE_LOAD_ADDONXML, readAddOnXml);
  s.setValue(lnm::DATABASE_USE_NAV, static_cast<int>(navDatabaseStatus));
}

void DatabaseManager::restoreState()
{
  Settings& s = Settings::instance();
  simulators = s.valueVar(lnm::DATABASE_PATHS).value<SimulatorTypeMap>();
  currentFsType = atools::fs::FsPaths::stringToType(s.valueStr(lnm::DATABASE_SIMULATOR, QString()));
  selectedFsType = atools::fs::FsPaths::stringToType(s.valueStr(lnm::DATABASE_LOADINGSIMULATOR));
  readInactive = s.valueBool(lnm::DATABASE_LOAD_INACTIVE, false);
  readAddOnXml = s.valueBool(lnm::DATABASE_LOAD_ADDONXML, true);
  navDatabaseStatus = static_cast<dm::NavdatabaseStatus>(s.valueInt(lnm::DATABASE_USE_NAV, dm::NAVDATABASE_MIXED));
}

/* Updates metadata, version and object counts in the scenery loading dialog */
void DatabaseManager::updateDialogInfo(atools::fs::FsPaths::SimulatorType value)
{
  QString metaText;

  QString databaseFile = buildDatabaseFileName(value);
  SqlDatabase tempDb(DATABASE_NAME_DLG_INFO_TEMP);

  if(QFileInfo::exists(databaseFile))
  { // Open temp database to show statistics
    tempDb.setDatabaseName(databaseFile);
    tempDb.setReadonly();
    tempDb.open();
  }

  atools::util::Version applicationVersion = DatabaseMeta::getApplicationVersion();
  if(tempDb.isOpen())
  {
    DatabaseMeta dbmeta(tempDb);
    atools::util::Version databaseVersion = dbmeta.getDatabaseVersion();

    if(!dbmeta.isValid())
      metaText = databaseMetaText.arg(tr("None")).arg(tr("None")).arg(applicationVersion.getVersionString()).arg(QString());
    else
    {
      QString cycleText;
      if(!dbmeta.getAiracCycle().isEmpty())
        cycleText = databaseAiracCycleText.arg(dbmeta.getAiracCycle());

      metaText = databaseMetaText.
                 arg(dbmeta.getLastLoadTime().isValid() ? dbmeta.getLastLoadTime().toString() : tr("None")).
                 arg(databaseVersion.getVersionString()).arg(applicationVersion.getVersionString()).arg(cycleText);
    }
  }
  else
    metaText = databaseMetaText.arg(tr("None")).arg(tr("None")).arg(applicationVersion.getVersionString()).arg(QString());

  QString tableText;
  if(tempDb.isOpen() && hasSchema(&tempDb))
  {
    atools::sql::SqlUtil util(tempDb);

    // Get row counts for the dialog
    tableText = databaseInfoText.arg(util.rowCount("bgl_file")).
                arg(util.rowCount("airport")).
                arg(util.rowCount("vor")).
                arg(util.rowCount("ils")).
                arg(util.rowCount("ndb")).
                arg(util.rowCount("marker")).
                arg(util.rowCount("waypoint")).
                arg(util.rowCount("boundary"));
  }
  else
    tableText = databaseInfoText.arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0).arg(0);

  databaseDialog->setHeader(metaText + tr("<p><big>Currently Loaded:</big></p><p>%1</p>").arg(tableText));

  if(tempDb.isOpen())
    tempDb.close();
}

/* Create database name including simulator short name */
QString DatabaseManager::buildDatabaseFileName(atools::fs::FsPaths::SimulatorType type)
{
  return databaseDirectory +
         QDir::separator() + lnm::DATABASE_PREFIX +
         atools::fs::FsPaths::typeToShortName(type).toLower() + lnm::DATABASE_SUFFIX;
}

/* Create database name including simulator short name in application directory */
QString DatabaseManager::buildDatabaseFileNameAppDir(atools::fs::FsPaths::SimulatorType type)
{
  return QCoreApplication::applicationDirPath() +
         QDir::separator() + lnm::DATABASE_DIR +
         QDir::separator() + lnm::DATABASE_PREFIX +
         atools::fs::FsPaths::typeToShortName(type).toLower() + lnm::DATABASE_SUFFIX;
}

QString DatabaseManager::buildCompilingDatabaseFileName()
{
  return databaseDirectory + QDir::separator() + lnm::DATABASE_PREFIX + "compiling" + lnm::DATABASE_SUFFIX;
}

void DatabaseManager::freeActions()
{
  if(menuDbSeparator != nullptr)
  {
    menuDbSeparator->deleteLater();
    menuDbSeparator = nullptr;
  }
  if(menuNavDbSeparator != nullptr)
  {
    menuNavDbSeparator->deleteLater();
    menuNavDbSeparator = nullptr;
  }
  if(simDbGroup != nullptr)
  {
    simDbGroup->deleteLater();
    simDbGroup = nullptr;
  }
  if(navDbActionAll != nullptr)
  {
    navDbActionAll->deleteLater();
    navDbActionAll = nullptr;
  }
  if(navDbActionBlend != nullptr)
  {
    navDbActionBlend->deleteLater();
    navDbActionBlend = nullptr;
  }
  if(navDbActionOff != nullptr)
  {
    navDbActionOff->deleteLater();
    navDbActionOff = nullptr;
  }
  if(navDbSubMenu != nullptr)
  {
    navDbSubMenu->deleteLater();
    navDbSubMenu = nullptr;
  }
  if(navDbGroup != nullptr)
  {
    navDbGroup->deleteLater();
    navDbGroup = nullptr;
  }
  for(QAction *action : actions)
    action->deleteLater();
  actions.clear();
}

/* Uses the simulator map copy from the dialog to update the changed paths */
void DatabaseManager::updateSimulatorPathsFromDialog()
{
  const SimulatorTypeMap& dlgPaths = databaseDialog->getPaths();

  for(auto it = dlgPaths.constBegin(); it != dlgPaths.constEnd(); ++it)
  {
    const FsPaths::SimulatorType type = it.key();
    if(simulators.contains(type))
    {
      simulators[type].basePath = dlgPaths.value(type).basePath;
      simulators[type].sceneryCfg = dlgPaths.value(type).sceneryCfg;
    }
  }
}

/* Updates the flags for installed simulators and removes all entries where neither database
 * not simulator installation was found */
void DatabaseManager::updateSimulatorFlags()
{
  for(atools::fs::FsPaths::SimulatorType type : FsPaths::getAllSimulatorTypes())
    // Already present or not - update database status since file exists
    simulators[type].hasDatabase = QFile::exists(buildDatabaseFileName(type));
}

void DatabaseManager::correctSimulatorType()
{
  if(currentFsType == atools::fs::FsPaths::NONE ||
     (!simulators.value(currentFsType).hasDatabase && !simulators.value(currentFsType).isInstalled))
    currentFsType = simulators.getBest();

  if(currentFsType == atools::fs::FsPaths::NONE)
    currentFsType = simulators.getBestInstalled();

  // Correct if loading simulator is invalid - get the best installed
  if(selectedFsType == atools::fs::FsPaths::NONE || !simulators.getAllInstalled().contains(selectedFsType))
    selectedFsType = simulators.getBestInstalled();
}

const atools::fs::db::DatabaseMeta DatabaseManager::metaFromFile(const QString& file)
{
  SqlDatabase tempDb(DATABASE_NAME_TEMP);
  tempDb.setDatabaseName(file);
  tempDb.setReadonly();
  tempDb.open();

  DatabaseMeta meta(tempDb);
  meta.deInit(); // Detach from database
  closeDatabaseFile(&tempDb);
  return meta;
}

void DatabaseManager::checkDatabaseVersion()
{
  const atools::fs::db::DatabaseMeta *databaseMetaSim = NavApp::getDatabaseMetaSim();
  if(navDatabaseStatus != dm::NAVDATABASE_ALL && databaseMetaSim != nullptr && databaseMetaSim->hasData())
  {
    QStringList msg;
    if(databaseMetaSim->getDatabaseVersion() < databaseMetaSim->getApplicationVersion())
      msg.append(tr("The scenery library database was created using a previous version of Little Navmap."));

    if(databaseMetaSim->getLastLoadTime() < QDateTime::currentDateTime().addMonths(-MAX_AGE_DAYS))
    {
      qint64 days = databaseMetaSim->getLastLoadTime().date().daysTo(QDate::currentDate());
      msg.append(tr("Scenery library database was not reloaded for more than %1 days.").arg(days));
    }

    if(!msg.isEmpty())
    {
      qDebug() << Q_FUNC_INFO << msg;

      dialog->showWarnMsgBox(lnm::ACTIONS_SHOW_DATABASE_OLD,
                             tr("<p>%1</p>"
                                  "<p>It is advised to reload the scenery library database after each Little Navmap update, "
                                    "after installing new add-on scenery or after a flight simulator update to "
                                    "enable new features or benefit from bug fixes.</p>"
                                    "<p>You can do this in menu \"Scenery Library\" -> "
                                      "\"Reload Scenery Library\".</p>").arg(msg.join(tr("<br/>"))),
                             tr("Do not &show this dialog again."));
    }
  }
}

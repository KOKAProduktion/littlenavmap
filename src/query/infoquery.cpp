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

#include "query/infoquery.h"

#include "common/constants.h"
#include "query/querytypes.h"
#include "settings/settings.h"
#include "sql/sqldatabase.h"
#include "sql/sqlquery.h"
#include "sql/sqlrecord.h"
#include "sql/sqlutil.h"

using atools::sql::SqlQuery;
using atools::sql::SqlDatabase;
using atools::sql::SqlRecord;
using atools::sql::SqlRecordList;
using atools::sql::SqlUtil;

InfoQuery::InfoQuery(SqlDatabase *sqlDb, atools::sql::SqlDatabase *sqlDbNav, atools::sql::SqlDatabase *sqlDbTrack)
  : dbSim(sqlDb), dbNav(sqlDbNav), dbTrack(sqlDbTrack)
{
  atools::settings::Settings& settings = atools::settings::Settings::instance();
  airportCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "AirportCache", 100).toInt());
  vorCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "VorCache", 100).toInt());
  ndbCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "NdbCache", 100).toInt());
  msaCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "MsaCache", 100).toInt());
  holdingCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "HoldingCache", 100).toInt());
  runwayEndCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "RunwayEndCache", 100).toInt());
  comCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "ComCache", 100).toInt());
  runwayCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "RunwayCache", 100).toInt());
  helipadCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "HelipadCache", 100).toInt());
  startCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "StartCache", 100).toInt());
  approachCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "ApproachCache", 100).toInt());
  transitionCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "TransitionCache", 100).toInt());
  airportSceneryCache.setMaxCost(settings.getAndStoreValue(lnm::SETTINGS_INFOQUERY + "AirportSceneryCache", 100).toInt());
}

InfoQuery::~InfoQuery()
{
  deInitQueries();
}

const SqlRecord *InfoQuery::getAirportInformation(int airportId)
{
  airportQuery->bindValue(":id", airportId);
  return query::cachedRecord(airportCache, airportQuery, airportId);
}

const atools::sql::SqlRecordList *InfoQuery::getAirportSceneryInformation(const QString& ident)
{
  airportSceneryQuery->bindValue(":id", ident);
  return query::cachedRecordList(airportSceneryCache, airportSceneryQuery, ident);
}

const SqlRecordList *InfoQuery::getComInformation(int airportId)
{
  comQuery->bindValue(":id", airportId);
  return query::cachedRecordList(comCache, comQuery, airportId);
}

const SqlRecordList *InfoQuery::getApproachInformation(int airportId)
{
  approachQuery->bindValue(":id", airportId);
  return query::cachedRecordList(approachCache, approachQuery, airportId);
}

const SqlRecordList *InfoQuery::getTransitionInformation(int approachId)
{
  transitionQuery->bindValue(":id", approachId);
  return query::cachedRecordList(transitionCache, transitionQuery, approachId);
}

const SqlRecordList *InfoQuery::getRunwayInformation(int airportId)
{
  runwayQuery->bindValue(":id", airportId);
  return query::cachedRecordList(runwayCache, runwayQuery, airportId);
}

const SqlRecordList *InfoQuery::getHelipadInformation(int airportId)
{
  helipadQuery->bindValue(":id", airportId);
  return query::cachedRecordList(helipadCache, helipadQuery, airportId);
}

const SqlRecordList *InfoQuery::getStartInformation(int airportId)
{
  startQuery->bindValue(":id", airportId);
  return query::cachedRecordList(startCache, startQuery, airportId);
}

const atools::sql::SqlRecord *InfoQuery::getRunwayEndInformation(int runwayEndId)
{
  runwayEndQuery->bindValue(":id", runwayEndId);
  return query::cachedRecord(runwayEndCache, runwayEndQuery, runwayEndId);
}

const atools::sql::SqlRecord *InfoQuery::getVorInformation(int vorId)
{
  vorQuery->bindValue(":id", vorId);
  return query::cachedRecord(vorCache, vorQuery, vorId);
}

const atools::sql::SqlRecord InfoQuery::getVorByIdentAndRegion(const QString& ident, const QString& region)
{
  vorIdentRegionQuery->bindValue(":ident", ident);
  vorIdentRegionQuery->bindValue(":region", region);
  vorIdentRegionQuery->exec();
  atools::sql::SqlRecord rec;

  if(vorIdentRegionQuery->next())
    rec = vorIdentRegionQuery->record();

  vorIdentRegionQuery->finish();
  return rec;
}

const atools::sql::SqlRecord *InfoQuery::getNdbInformation(int ndbId)
{
  ndbQuery->bindValue(":id", ndbId);
  return query::cachedRecord(ndbCache, ndbQuery, ndbId);
}

const SqlRecord *InfoQuery::getMsaInformation(int msaId)
{
  if(msaQuery != nullptr)
  {
    msaQuery->bindValue(":id", msaId);
    return query::cachedRecord(msaCache, msaQuery, msaId);
  }
  else
    return nullptr;
}

const SqlRecord *InfoQuery::getHoldingInformation(int holdingId)
{
  if(holdingQuery != nullptr)
  {
    holdingQuery->bindValue(":id", holdingId);
    return query::cachedRecord(holdingCache, holdingQuery, holdingId);
  }
  else
    return nullptr;
}

atools::sql::SqlRecord InfoQuery::getTrackMetadata(int trackId)
{
  SqlQuery query(dbTrack);
  query.prepare("select m.* from track t join trackmeta m on t.trackmeta_id = m.trackmeta_id where track_id = :id");
  query.bindValue(":id", trackId);
  query.exec();
  if(query.next())
    return query.record();
  else
    return SqlRecord();
}

void InfoQuery::initQueries()
{
  deInitQueries();

  // TODO limit number of columns - remove star query
  airportQuery = new SqlQuery(dbSim);
  airportQuery->prepare("select * from airport "
                        "join bgl_file on airport.file_id = bgl_file.bgl_file_id "
                        "join scenery_area on bgl_file.scenery_area_id = scenery_area.scenery_area_id "
                        "where airport_id = :id");

  airportSceneryQuery = new SqlQuery(dbSim);
  airportSceneryQuery->prepare("select * from airport_file f "
                               "join bgl_file b on f.file_id = b.bgl_file_id  "
                               "join scenery_area s on b.scenery_area_id = s.scenery_area_id "
                               "where f.ident = :id order by f.airport_file_id");

  comQuery = new SqlQuery(dbSim);
  comQuery->prepare("select * from com where airport_id = :id order by type, frequency");

  vorQuery = new SqlQuery(dbNav);
  vorQuery->prepare("select * from vor "
                    "join bgl_file on vor.file_id = bgl_file.bgl_file_id "
                    "join scenery_area on bgl_file.scenery_area_id = scenery_area.scenery_area_id "
                    "where vor_id = :id");

  // Same as above for airport MSA table
  SqlDatabase *msaDb = SqlUtil::getDbWithTableAndRows("airport_msa", {dbNav, dbSim});
  qDebug() << Q_FUNC_INFO << "Airport MSA database" << (msaDb == nullptr ? "None" : msaDb->databaseName());

  if(msaDb != nullptr)
  {
    msaQuery = new SqlQuery(msaDb);
    msaQuery->prepare("select * from airport_msa "
                      "join bgl_file on airport_msa.file_id = bgl_file.bgl_file_id "
                      "join scenery_area on bgl_file.scenery_area_id = scenery_area.scenery_area_id "
                      "where airport_msa_id = :id");
  }

  // Check for holding table in nav (Navigraph) database and then in simulator database (X-Plane only)
  SqlDatabase *holdingDb = SqlUtil::getDbWithTableAndRows("holding", {dbNav, dbSim});
  qDebug() << Q_FUNC_INFO << "Holding database" << (holdingDb == nullptr ? "None" : holdingDb->databaseName());

  if(holdingDb != nullptr)
  {
    holdingQuery = new SqlQuery(holdingDb);
    holdingQuery->prepare("select * from holding "
                          "join bgl_file on holding.file_id = bgl_file.bgl_file_id "
                          "join scenery_area on bgl_file.scenery_area_id = scenery_area.scenery_area_id "
                          "where holding_id = :id");
  }

  ndbQuery = new SqlQuery(dbNav);
  ndbQuery->prepare("select * from ndb "
                    "join bgl_file on ndb.file_id = bgl_file.bgl_file_id "
                    "join scenery_area on bgl_file.scenery_area_id = scenery_area.scenery_area_id "
                    "where ndb_id = :id");

  runwayQuery = new SqlQuery(dbSim);
  runwayQuery->prepare("select * from runway where airport_id = :id order by heading");

  runwayEndQuery = new SqlQuery(dbSim);
  runwayEndQuery->prepare("select * from runway_end where runway_end_id = :id");

  helipadQuery = new SqlQuery(dbSim);
  helipadQuery->prepare("select h.*, s.number as start_number, s.runway_name from helipad h "
                        " left outer join start s on s.start_id= h.start_id "
                        " where h.airport_id = :id order by s.runway_name");

  startQuery = new SqlQuery(dbSim);
  startQuery->prepare("select * from start where airport_id = :id order by type asc, runway_name");

  vorIdentRegionQuery = new SqlQuery(dbNav);
  vorIdentRegionQuery->prepare("select * from vor where ident = :ident and region = :region");

  approachQuery = new SqlQuery(dbNav);
  approachQuery->prepare("select a.runway_name, r.runway_end_id, a.* from approach a "
                         "left outer join runway_end r on a.runway_end_id = r.runway_end_id "
                         "where a.airport_id = :id "
                         "order by a.runway_name, a.type, a.fix_ident");

  transitionQuery = new SqlQuery(dbNav);
  transitionQuery->prepare("select * from transition where approach_id = :id order by fix_ident");
}

void InfoQuery::deInitQueries()
{
  airportCache.clear();
  vorCache.clear();
  ndbCache.clear();
  msaCache.clear();
  holdingCache.clear();
  runwayEndCache.clear();
  comCache.clear();
  runwayCache.clear();
  helipadCache.clear();
  startCache.clear();
  approachCache.clear();
  transitionCache.clear();
  airportSceneryCache.clear();

  delete airportQuery;
  airportQuery = nullptr;

  delete airportSceneryQuery;
  airportSceneryQuery = nullptr;

  delete comQuery;
  comQuery = nullptr;

  delete vorQuery;
  vorQuery = nullptr;

  delete msaQuery;
  msaQuery = nullptr;

  delete holdingQuery;
  holdingQuery = nullptr;

  delete ndbQuery;
  ndbQuery = nullptr;

  delete runwayQuery;
  runwayQuery = nullptr;

  delete helipadQuery;
  helipadQuery = nullptr;

  delete startQuery;
  startQuery = nullptr;

  delete runwayEndQuery;
  runwayEndQuery = nullptr;

  delete vorIdentRegionQuery;
  vorIdentRegionQuery = nullptr;

  delete approachQuery;
  approachQuery = nullptr;

  delete transitionQuery;
  transitionQuery = nullptr;
}

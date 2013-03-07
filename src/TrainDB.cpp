/*
 * Copyright (c) 2012 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "TrainDB.h"
#include "ErgFile.h"

// DB Schema Version - YOU MUST UPDATE THIS IF THE TRAIN DB SCHEMA CHANGES

// Revision History
// Rev Date         Who                What Changed
// 01  21 Dec 2012  Mark Liversedge    Initial Build

static int TrainDBSchemaVersion = 1;
TrainDB *trainDB;

TrainDB::TrainDB(QDir home) : home(home)
{
    // we live above the rider directory
	initDatabase(home);
}

void TrainDB::closeConnection()
{
    dbconn.close();
}

TrainDB::~TrainDB()
{
    closeConnection();
}

void
TrainDB::initDatabase(QDir home)
{


    if(dbconn.isOpen()) return;

    // get a connection
    db = QSqlDatabase::addDatabase("QSQLITE", "1");
    db.setDatabaseName(home.absolutePath() + "/trainDB"); 
    dbconn = db.database("1");

    if (!dbconn.isOpen()) {
        QMessageBox::critical(0, qApp->translate("TrainDB","Cannot open database"),
                       qApp->translate("TrainDB","Unable to establish a database connection.\n"
                                       "This feature requires SQLite support. Please read "
                                       "the Qt SQL driver documentation for information how "
                                       "to build it.\n\n"
                                       "Click Cancel to exit."), QMessageBox::Cancel);
    } else {

        // create database - does nothing if its already there
        createDatabase();
    }
}

// rebuild effectively drops and recreates all tables
// but not the version table, since its about deleting
// user data (e.g. when rescanning their hard disk)
void
TrainDB::rebuildDB()
{
    dropWorkoutTable();
    createWorkoutTable();
    dropVideoTable();
    createVideoTable();
}

bool TrainDB::createVideoTable()
{
    QSqlQuery query(dbconn);
    bool rc;
    bool createTables = true;

    // does the table exist?
    rc = query.exec("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");
    if (rc) {
        while (query.next()) {

            QString table = query.value(0).toString();
            if (table == "videos") {
                createTables = false;
                break;
            }
        }
    }
    // we need to create it!
    if (rc && createTables) {

        QString createVideoTable = "create table videos (filepath varchar primary key,"
                                    "filename varchar,"
                                    "timestamp integer,"
                                    "length integer);";

        rc = query.exec(createVideoTable);

        // insert the 'DVD' record for playing currently loaded DVD
        // need to resolve DVD playback in v3.1, there is an open feature request for this.
        // rc = query.exec("INSERT INTO videos (filepath, filename) values (\"\", \"DVD\");");

        // add row to version database
        query.exec("DELETE FROM version where table_name = \"videos\"");

        // insert into table
        query.prepare("INSERT INTO version (table_name, schema_version, creation_date) values (?,?,?);");
        query.addBindValue("videos");
	    query.addBindValue(TrainDBSchemaVersion);
	    query.addBindValue(QDateTime::currentDateTime().toTime_t());
        rc = query.exec();
    }
    return rc;
}

bool TrainDB::createWorkoutTable()
{
    QSqlQuery query(dbconn);
    bool rc;
    bool createTables = true;

    // does the table exist?
    rc = query.exec("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");
    if (rc) {
        while (query.next()) {

            QString table = query.value(0).toString();
            if (table == "workouts") {
                createTables = false;
                break;
            }
        }
    }
    // we need to create it!
    if (rc && createTables) {

        QString createMetricTable = "create table workouts (filepath varchar primary key,"
                                    "filename,"
                                    "timestamp integer,"
                                    "description varchar,"
                                    "source varchar,"
                                    "ftp integer,"
                                    "length integer,"
                                    "coggan_tss integer,"
                                    "coggan_if integer,"
                                    "elevation integer,"
                                    "grade double );";

        rc = query.exec(createMetricTable);

        // adding a space at the front of string to make manual mode always
        // appear first in a sorted list is a bit of a hack, but works ok
        QString manualErg = QString("INSERT INTO workouts (filepath, filename) values (\"//1\", \"%1\");")
                         .arg(tr(" Manual Erg Mode"));
        rc = query.exec(manualErg);

        QString manualCrs = QString("INSERT INTO workouts (filepath, filename) values (\"//2\", \"%1\");")
                         .arg(tr(" Manual Slope Mode"));
        rc = query.exec(manualCrs);

        // add row to version database
        query.exec("DELETE FROM version where table_name = \"workouts\"");

        // insert into table
        query.prepare("INSERT INTO version (table_name, schema_version, creation_date) values (?,?,?);");
        query.addBindValue("workouts");
	    query.addBindValue(TrainDBSchemaVersion);
	    query.addBindValue(QDateTime::currentDateTime().toTime_t());
        rc = query.exec();
    }
    return rc;
}

bool TrainDB::dropVideoTable()
{
    QSqlQuery query("DROP TABLE videos", dbconn);
    bool rc = query.exec();
    return rc;
}

bool TrainDB::dropWorkoutTable()
{
    QSqlQuery query("DROP TABLE workouts", dbconn);
    bool rc = query.exec();
    return rc;
}

bool TrainDB::createDatabase()
{
    // check schema version and if missing recreate database
    checkDBVersion();

    // Workouts
	createWorkoutTable();
	createVideoTable();

    return true;
}

void TrainDB::checkDBVersion()
{
    // can we get a version number?
    QSqlQuery query("SELECT table_name, schema_version, creation_date from version;", dbconn);

    bool rc = query.exec();

    if (!rc) {
        // we couldn't read the version table properly
        // it must be out of date!!

        QSqlQuery dropM("DROP TABLE version", dbconn);
        dropM.exec();

        // recreate version table and add one entry
        QSqlQuery version("CREATE TABLE version ( table_name varchar primary key, schema_version integer, creation_date date );", dbconn);
        version.exec();

        // wipe away whatever (if anything is there)
        dropWorkoutTable();
        createWorkoutTable();

        dropVideoTable();
        createVideoTable();
        return;
    }

    // ok we checked out ok, so lets adjust db schema to reflect
    // tne current version / crc
    bool dropWorkout = false;
    bool dropVideo = false;
    while (query.next()) {

        QString table_name = query.value(0).toString();
        int currentversion = query.value(1).toInt();

        if (table_name == "workouts" && currentversion != TrainDBSchemaVersion) dropWorkout = true;
        if (table_name == "videos" && currentversion != TrainDBSchemaVersion) dropVideo = true;
    }
    query.finish();

    // "workouts" table, is it up-to-date?
    if (dropWorkout) dropWorkoutTable();
    if (dropVideo) dropVideoTable();
}

int TrainDB::getCount()
{
    // how many workouts are there?
    QSqlQuery query("SELECT count(*) from workouts;", dbconn);
    bool rc = query.exec();

    if (rc) {
        while (query.next()) {
            return query.value(0).toInt();
        }
    }

    return 0;
}

int TrainDB::getDBVersion()
{
    int schema_version = -1;

    // can we get a version number?
    QSqlQuery query("SELECT schema_version from version;", dbconn);

    bool rc = query.exec();

    if (rc) {
        while (query.next()) {
            if (query.value(0).toInt() > schema_version)
                schema_version = query.value(0).toInt();
        }
    }
    query.finish();
    return schema_version;
}

/*----------------------------------------------------------------------
 * CRUD routines
 *----------------------------------------------------------------------*/

bool TrainDB::deleteWorkout(QString pathname)
{
	QSqlQuery query(dbconn);
    QDateTime timestamp = QDateTime::currentDateTime();

    // zap the current row - if there is one
    query.prepare("DELETE FROM workouts WHERE filepath = ?;");
    query.addBindValue(pathname);

    return query.exec();
}

bool TrainDB::importWorkout(QString pathname, ErgFile *ergFile)
{
	QSqlQuery query(dbconn);
    QDateTime timestamp = QDateTime::currentDateTime();

    // zap the current row - if there is one
    query.prepare("DELETE FROM workouts WHERE filepath = ?;");
    query.addBindValue(pathname);
    query.exec();

    // construct an insert statement
    QString insertStatement = "insert into workouts ( filepath, "
                                    "filename,"
                                    "timestamp,"
                                    "description,"
                                    "source,"
                                    "ftp,"
                                    "length,"
                                    "coggan_tss,"
                                    "coggan_if,"
                                    "elevation,"
                                    "grade ) values ( ?,?,?,?,?,?,?,?,?,?,? );";
	query.prepare(insertStatement);

    // filename, timestamp, ride date
	query.addBindValue(pathname);
	query.addBindValue(QFileInfo(pathname).fileName());
	query.addBindValue(timestamp);
    query.addBindValue(ergFile->Name);
	query.addBindValue(ergFile->Source);
	query.addBindValue(ergFile->Ftp);
	query.addBindValue((int)ergFile->Duration);
	query.addBindValue(ergFile->TSS);
	query.addBindValue(ergFile->IF);
	query.addBindValue(ergFile->ELE);
	query.addBindValue(ergFile->GRADE);

    // go do it!
	bool rc = query.exec();

	return rc;
}

bool TrainDB::importVideo(QString pathname)
{
	QSqlQuery query(dbconn);
    QDateTime timestamp = QDateTime::currentDateTime();

    // zap the current row - if there is one
    query.prepare("DELETE FROM videos WHERE filepath = ?;");
    query.addBindValue(pathname);
    query.exec();

    // construct an insert statement
    QString insertStatement = "insert into videos ( filepath,filename ) values ( ?,? );";
	query.prepare(insertStatement);

    // filename, timestamp, ride date
	query.addBindValue(pathname);
	query.addBindValue(QFileInfo(pathname).fileName());

    // go do it!
	bool rc = query.exec();

	return rc;
}

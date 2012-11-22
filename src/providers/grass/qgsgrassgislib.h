/***************************************************************************
    qgsgrassgislib.h  -  Fake GRASS gis lib
                             -------------------
    begin                : Nov 2012
    copyright            : (C) 2012 by Radim Blazek
    email                : radim dot blazek at gmail dot com
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGSGRASSGISLIB_H
#define QGSGRASSGISLIB_H

// GRASS header files
extern "C"
{
#include <grass/gis.h>
#include <grass/form.h>
}

#include <stdexcept>
#include "qgsexception.h"
#include <qgsproviderregistry.h>
#include <qgsrectangle.h>
#include <qgsrasterdataprovider.h>

#include <QLibrary>
#include <QProcess>
#include <QString>
#include <QMap>
#include <QHash>
#include <QTemporaryFile>
class QgsCoordinateReferenceSystem;
class QgsRectangle;

class GRASS_LIB_EXPORT QgsGrassGisLib
{
  public:
    // Region term is used in modules (g.region), internaly it is hold in structure
    // Cell_head, but variables keeping that struture are usually called window
    /*
    class Region
    {
      QgsRectangle extent;
      double ewRes; // east-west resolution
      double nsRes; // north south resolution
    };
    */

    class Raster
    {
      public:
        QgsRasterDataProvider *provider;
        int band;
        Raster(): provider( 0 ), band( 1 ) {}

    };

    static GRASS_LIB_EXPORT QgsGrassGisLib* instance();

    QgsGrassGisLib();

    int G__gisinit( const char * version, const char * programName );
    char *G_find_cell2( const char * name, const char * mapset );
    int G_open_cell_old( const char *name, const char *mapset );
    int G_raster_map_is_fp( const char *name, const char *mapset );
    int G_read_fp_range( const char *name, const char *mapset, struct FPRange *drange );
    int G_get_c_raster_row( int fd, CELL * buf, int row );

    void * resolve( const char * symbol );

    // Print error function set to be called by GRASS lib
    static int errorRoutine( const char *msg, int fatal );

    // Error called by fake lib
    void fatal( QString msg );

  private:
    /** pointer to canonical Singleton object */
    static QgsGrassGisLib* _instance;

    /** Original GRASS library handle */
    QLibrary mLibrary;

    /** Raster maps, key is fake file descriptor  */
    QMap<int, Raster> mRasters;

    /** Current region extent */
    QgsRectangle mExtent;
    /** Current region rows */
    int mRows;
    /** Current region columns */
    int mColumns;
};

#endif // QGSGRASSGISLIB_H

/***************************************************************************
    qgsgrassgislib.cpp  -  Fake GRASS gis lib
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
#include <signal.h>

#include "qgsgrassgislib.h"

#include "qgslogger.h"
#include "qgsapplication.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsrectangle.h"
#include "qgsconfig.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QTextStream>
#include <QTemporaryFile>
#include <QHash>

#include <QTextCodec>

extern "C"
{
#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <grass/gprojects.h>
#include <grass/Vect.h>
#include <grass/version.h>
}

#if !defined(GRASS_VERSION_MAJOR) || \
    !defined(GRASS_VERSION_MINOR) || \
    GRASS_VERSION_MAJOR<6 || \
    (GRASS_VERSION_MAJOR == 6 && GRASS_VERSION_MINOR <= 2)
#define G__setenv(name,value) G__setenv( ( char * ) (name), (char *) (value) )
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#endif


QgsGrassGisLib *QgsGrassGisLib::_instance = 0;

QgsGrassGisLib GRASS_LIB_EXPORT *QgsGrassGisLib::instance( )
{
  if ( _instance == 0 )
  {
    _instance = new QgsGrassGisLib();
  }
  return _instance;
}

QgsGrassGisLib::QgsGrassGisLib()
{
  // Load original GRASS library
  QString libPath = QString( GRASS_LIBRARY_GIS );
  QgsDebugMsg( "libPath = " + libPath );
  mLibrary.setFileName( libPath );
  if ( !mLibrary.load() )
  {
    QgsDebugMsg( "Cannot load original GRASS library" );
    return;
  }
}

int QgsGrassGisLib::errorRoutine( const char *msg, int fatal )
{
  QgsDebugMsg( QString( "error_routine (fatal = %1): %2" ).arg( fatal ).arg( msg ) );
  // Crash to get backtrace
  //int *x = 0; *x = 1;
  qFatal( "Fatal error" ); // core dump
  return 1;
}

void QgsGrassGisLib::fatal( QString msg )
{
  QgsLogger::fatal( msg );  // calls qFatal which does core dump
}

void * QgsGrassGisLib::resolve( const char * symbol )
{
  //QgsDebugMsg( QString("symbol = %1").arg(symbol));
  void * fn = mLibrary.resolve( symbol );
  if ( !fn )
  {
    QgsDebugMsg( "Cannot resolve symbol" );
  }
  return fn;
}

int GRASS_LIB_EXPORT QgsGrassGisLib::G__gisinit( const char * version, const char * programName )
{
  // We use this function also to init our fake lib
  QgsDebugMsg( QString( "version = %1 programName = %2" ).arg( version ).arg( programName ) );

  // Init providers path
  int argc = 1;
  char **argv = new char*[1];
  argv[0] = qstrdup( programName );

  QCoreApplication app( argc, argv ); // to init paths

  // unfortunately it seems impossible to get QGIS prefix
  // QCoreApplication::applicationDirPath() returns $GISBASE/lib on Linux
#if 0
  QDir dir( QCoreApplication::applicationDirPath() );
  dir.cdUp();
  QString prefixPath = dir.absolutePath();
#endif

  QString prefixPath = getenv( "QGIS_PREFIX" );
  if ( prefixPath.isEmpty() )
  {
    fatal( "Cannot get QGIS_PREFIX" );
  }

  QgsApplication::setPrefixPath( prefixPath, true );

  QgsDebugMsg( "Plugin path: " + QgsApplication::pluginPath() );
  QgsProviderRegistry::instance( QgsApplication::pluginPath() );

  G_set_error_routine( &errorRoutine );
  G_set_gisrc_mode( G_GISRC_MODE_MEMORY );
  G_setenv( "OVERWRITE", "1" );  // avoid checking if map exists

  G_suppress_masking();
  G__init_null_patterns();

  // Read region fron environment variable
  // QGIS_GRASS_REGION=west,south,east,north,cols,rows
  QString regionStr = getenv( "QGIS_GRASS_REGION" );
  QStringList regionList = regionStr.split( "," );
  if ( regionList.size() != 6 )
  {
    fatal( "Cannot read region from QGIS_GRASS_REGION environment variable" );
  }

  double xMin, yMin, xMax, yMax;
  int cols, rows;
  bool xMinOk, yMinOk, xMaxOk, yMaxOk, colsOk, rowsOk;
  xMin = regionList.value( 0 ).toDouble( &xMinOk );
  yMin = regionList.value( 1 ).toDouble( &yMinOk );
  xMax = regionList.value( 2 ).toDouble( &xMaxOk );
  yMax = regionList.value( 3 ).toDouble( &yMaxOk );
  cols = regionList.value( 4 ).toInt( &colsOk );
  rows = regionList.value( 5 ).toInt( &rowsOk );

  if ( !xMinOk || !yMinOk || !xMaxOk || !yMaxOk || !colsOk || !rowsOk )
  {
    fatal( "Cannot parse QGIS_GRASS_REGION" );
  }

  struct Cell_head window;
  window.west = xMin;
  window.south = yMin;
  window.east = xMax;
  window.north = yMax;
  window.rows = rows;
  window.cols = cols;

  char* err = G_adjust_Cell_head( &window, 1, 1 );
  if ( err )
  {
    fatal( QString( err ) );
  }
  G_set_window( &window );

  mExtent = QgsRectangle( xMin, yMin, xMax, yMax );
  mRows = rows;
  mColumns = cols;
  return 0;
}

int G__gisinit( const char * version, const char * programName )
{
  return QgsGrassGisLib::instance()->G__gisinit( version, programName );
}

typedef int G_parser_type( int argc, char **argv );
int G_parser( int argc, char **argv )
{
  QgsDebugMsg( "Entered" );
  G_parser_type* fn = ( G_parser_type* ) cast_to_fptr( QgsGrassGisLib::instance()->resolve( "G_parser" ) );
  int ret = fn( argc, argv );

  if ( ret == 0 ) // parsed OK
  {
    // It would be useful to determin region from input raster layers if no one
    // is given by environment variable but there seems to be no way to get
    // access to module options. Everything is in static variables in parser.c
    // and there are no access functions to them.
  }
  return ret;
}

// Defined here just because parser in cmake does not recognize this kind of params
typedef int G_set_error_routine_type( int ( * )( const char *, int ) );
int G_set_error_routine( int ( *error_routine )( const char *, int ) )
{
  //QgsDebugMsg( "Entered" );
  G_set_error_routine_type* fn = ( G_set_error_routine_type* ) cast_to_fptr( QgsGrassGisLib::instance()->resolve( "G_set_error_routine" ) );
  return fn( error_routine );
}

typedef int G_warning_type( const char *, ... );
int G_warning( const char * msg, ... )
{
  //QgsDebugMsg( "Entered" );
  G_warning_type* fn = ( G_warning_type* ) cast_to_fptr( QgsGrassGisLib::instance()->resolve( "G_warning" ) );
  va_list ap;
  va_start( ap, msg );
  int ret = fn( msg, ap );
  va_end( ap );
  return ret;
}

char * QgsGrassGisLib::G_find_cell2( const char * name, const char * mapset )
{
  Q_UNUSED( mapset );
  QgsDebugMsg( "name = " + QString( name ) );
  QString ms = "qgis";
  return qstrdup( ms.toAscii() );  // memory lost
}

char *G_find_cell2( const char* name, const char *mapset )
{
  return QgsGrassGisLib::instance()->G_find_cell2( name, mapset );
}

int QgsGrassGisLib::G_open_cell_old( const char *name, const char *mapset )
{
  Q_UNUSED( mapset );
  QgsDebugMsg( "name = " + QString( name ) );

  QString providerKey = "gdal";
  QString dataSource = QString( name );

  Raster raster;
  raster.provider = ( QgsRasterDataProvider* )QgsProviderRegistry::instance()->provider( providerKey, dataSource );
  if ( !raster.provider )
  {
    fatal( "Cannot load raster provider with data source: " + dataSource );
  }
  int fd = mRasters.size();
  mRasters.insert( fd, raster );
  return fd;
}

int G_open_cell_old( const char *name, const char *mapset )
{
  return QgsGrassGisLib::instance()->G_open_cell_old( name, mapset );
}

int QgsGrassGisLib::G_raster_map_is_fp( const char *name, const char *mapset )
{
  Q_UNUSED( name );
  Q_UNUSED( mapset );
  return 1; // all maps as DCELL for now
}

int G_raster_map_is_fp( const char *name, const char *mapset )
{
  return QgsGrassGisLib::instance()->G_raster_map_is_fp( name, mapset );
}

int QgsGrassGisLib::G_read_fp_range( const char *name, const char *mapset, struct FPRange *drange )
{
  Q_UNUSED( name );
  Q_UNUSED( mapset );
  // TODO: find/open map and get statistics, problem - not all datasources have
  // cached min/max, we can calc estimation or exact, but exact is slow.
  // Hopefully the range is not crutial for most modules
  G_init_fp_range( drange );
  //G_update_fp_range( std::numeric_limits<double>::max(), drange);
  //G_update_fp_range( -1.0 * std::numeric_limits<double>::max(), drange);
  // For now to get some reasonable results
  G_update_fp_range( 0, drange );
  G_update_fp_range( 2000, drange );

  return 1;
}

int G_read_fp_range( const char *name, const char *mapset, struct FPRange *range )
{
  return QgsGrassGisLib::instance()->G_read_fp_range( name, mapset, range );
}

int G_debug( int level, const char *msg, ... )
{
  Q_UNUSED( level );
  va_list ap;
  va_start( ap, msg );

  QString message = QString().vsprintf( msg, ap );
  va_end( ap );
  QgsDebugMsg( message );
  return 1;
}

int G_set_quant_rules( int fd, struct Quant *q )
{
  Q_UNUSED( fd );
  Q_UNUSED( q );
  return 0;
}

int QgsGrassGisLib::G_get_c_raster_row( int fd, CELL * buf, int row )
{
  Raster raster = mRasters.value( fd );
  if ( !raster.provider ) return -1;

  // Create extent for current row
  QgsRectangle blockRect = mExtent;
  double yRes = mExtent.height() / mRows;
  double yMax = mExtent.yMaximum() - row * yRes;
  blockRect.setYMaximum( yMax );
  blockRect.setYMinimum( yMax - yRes );

  QgsRasterBlock *block = raster.provider->block( raster.band, blockRect, mColumns, 1 );
  if ( !block ) return -1;

  block->convert( QgsRasterBlock::Int32 );
  for ( int i = 0; i < mColumns; i++ )
  {
    if ( block->isNoData( 0, i ) )
    {
      G_set_null_value( &( buf[i] ), 1, CELL_TYPE );
    }
    else
    {
      memcpy( &( buf[i] ), block->bits( 0, i ), 4 );
    }
  }
  delete block;
  return 1;
}

int G_get_c_raster_row( int fd, CELL * buf, int row )
{
  return QgsGrassGisLib::instance()->G_get_c_raster_row( fd, buf, row );
}

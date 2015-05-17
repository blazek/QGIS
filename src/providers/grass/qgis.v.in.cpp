/***************************************************************************
    qgis.v.in.cpp
    ---------------------
    begin                : May 2015
    copyright            : (C) 2015 by Radim Blazek
    email                : radim dot blazek at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

extern "C"
{
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#ifdef WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <grass/version.h>
#include <grass/gis.h>
//#include <grass/config.h>
#include <grass/dbmi.h>

#if GRASS_VERSION_MAJOR < 7
#include <grass/Vect.h>
#else
#include <grass/vector.h>
#endif
}

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QIODevice>

#include "qgsfeature.h"
#include "qgsgeometry.h"
#include "qgsrectangle.h"
#include "qgsrasterblock.h"
#include "qgsgrass.h"

static struct line_pnts *line = Vect_new_line_struct();

//void writePoint( struct line_pnts *line, QgsPoint point )
void writePoint( struct Map_info* map, QgsPoint point, struct line_cats *cats )
{
  Vect_reset_line( line );
  Vect_append_point( line, point.x(), point.y(), 0 );
  Vect_write_line( map, GV_POINT, line, cats );
}

void writePolyline( struct Map_info* map, int type, QgsPolyline polyline, struct line_cats *cats )
{
  Vect_reset_line( line );
  foreach ( QgsPoint point, polyline )
  {
    Vect_append_point( line, point.x(), point.y(), 0 );
  }
  Vect_write_line( map, type, line, cats );
}


int main( int argc, char **argv )
{
  struct Option *mapOption;

  G_gisinit( argv[0] );
  G_define_module();
  mapOption = G_define_standard_option( G_OPT_V_OUTPUT );

  if ( G_parser( argc, argv ) )
    exit( EXIT_FAILURE );

  struct Map_info map, tmpMap;
  Vect_open_new( &map, mapOption->answer, 0 );
  QDateTime now = QDateTime::currentDateTime();
  QString tmpName = QString( "%1_tmp_%2" ).arg( mapOption->answer ).arg( now.toString( "yyyyMMddhhmmss" ) );
  Vect_open_new( &tmpMap, tmpName.toUtf8().data(), 0 );

  QFile stdinFile;
  stdinFile.open( 0, QIODevice::ReadOnly );
  QDataStream stdinStream( &stdinFile );

  QFile stdoutFile;
  stdoutFile.open( 0, QIODevice::ReadOnly );
  QDataStream stdoutStream( &stdoutFile );

  //QgsRectangle extent;
  qint32 typeQint32;
  stdinStream >> typeQint32;
  QGis::WkbType wkbType = ( QGis::WkbType )typeQint32;
  QGis::WkbType wkbFlatType = QGis::flatType( wkbType );
#if 0
  int type = 0;
  switch ( QGis::singleType( wkbFlatType ) )
  {
    case QGis::WKBPoint:
      type = GV_POINT;
      break;
    case QGis::WKBLineString:
      type = GV_LINE;
      break;
    case QGis::WKBPolygon:
      type = GV_BOUNDARY;
      break;
    default:
      type = QGis::WKBUnknown;
  }
#endif

  QgsFields srcFields;
  stdinStream >> srcFields;
  // TODO: find (in QgsGrassVectorImport) if there is unique 'id' or 'cat' field and use it as cat
  int keyNum = 1;
  QString key;
  while ( true )
  {
    key = "cat" + ( keyNum == 1 ? "" : QString::number( keyNum ) );
    if ( srcFields.indexFromName( key ) == -1 )
    {
      break;
    }
    keyNum++;
  }

  QgsFields fields;
  fields.append( QgsField( key, QVariant::Int ) );
  fields.extend( srcFields );

  struct field_info *fieldInfo = Vect_default_field_info( &map, 1, NULL, GV_1TABLE );
  if ( Vect_map_add_dblink( &map, 1, NULL, fieldInfo->table, key.toLatin1().data(),
                            fieldInfo->database, fieldInfo->driver ) != 0 )
  {
    G_fatal_error( "Cannot add link" );
  }

  dbDriver *driver = db_start_driver_open_database( fieldInfo->driver, fieldInfo->database );
  if ( !driver )
  {
    G_fatal_error( "Cannot open database %s by driver %s", fieldInfo->database, fieldInfo->driver );
  }
  try
  {
    QgsGrass::createTable( driver, QString( fieldInfo->table ), fields );
  }
  catch ( QgsGrass::Exception &e )
  {
    G_fatal_error( "Cannot create table: %s", e.what() );
  }

  QgsFeature feature;
  //struct line_pnts *line = Vect_new_line_struct();
  struct line_cats *cats = Vect_new_cats_struct();

  qint32 featureCount = 0;
  while ( true )
  {
    stdinStream >> feature;
    if ( !feature.isValid() )
    {
      break;
    }
    Vect_reset_cats( cats );
    Vect_cat_set( cats, 1, ( int )feature.id() );

    QgsGeometry* geometry = feature.geometry();
    if ( geometry )
    {
      if ( wkbFlatType == QGis::WKBPoint )
      {
        QgsPoint point = geometry->asPoint();
        writePoint( &map, point, cats );
      }
      else if ( wkbFlatType == QGis::WKBMultiPoint )
      {
        QgsMultiPoint multiPoint = geometry->asMultiPoint();
        foreach ( QgsPoint point, multiPoint )
        {
          writePoint( &map, point, cats );
        }
      }
      else if ( wkbFlatType == QGis::WKBLineString )
      {
        QgsPolyline polyline = geometry->asPolyline();
        writePolyline( &map, GV_LINE, polyline, cats );
      }
      else if ( wkbFlatType == QGis::WKBMultiLineString )
      {
        QgsMultiPolyline multiPolyline = geometry->asMultiPolyline();
        foreach ( QgsPolyline polyline, multiPolyline )
        {
          writePolyline( &map, GV_LINE, polyline, cats );
        }
      }
      else if ( wkbFlatType == QGis::WKBPolygon )
      {
        QgsPolygon polygon = geometry->asPolygon();
        foreach ( QgsPolyline polyline, polygon )
        {
          writePolyline( &map, GV_BOUNDARY, polyline, cats );
        }
      }
      else if ( wkbFlatType == QGis::WKBMultiPolygon )
      {
        QgsMultiPolygon multiPolygon = geometry->asMultiPolygon();
        foreach ( QgsPolygon polygon, multiPolygon )
        {
          foreach ( QgsPolyline polyline, polygon )
          {
            writePolyline( &map, GV_BOUNDARY, polyline, cats );
          }
        }
      }
      else
      {
        G_fatal_error( "Geometry type not supported" );
      }

      QgsAttributes attributes = feature.attributes();
      attributes.insert( 0, QVariant( feature.id() ) );
      try
      {
        QgsGrass::insertRow( driver, QString( fieldInfo->table ), attributes );
      }
      catch ( QgsGrass::Exception &e )
      {
        G_fatal_error( "Cannot insert: %s", e.what() );
      }
    }
    featureCount++;
  }
  db_close_database_shutdown_driver( driver );
  stdoutStream << featureCount;

  Vect_copy_map_lines( &tmpMap, &map );
  Vect_close( &tmpMap );
  Vect_delete( tmpName.toUtf8().data() );

  Vect_build( &map );
  Vect_close( &map );

  // TODO history

  exit( EXIT_SUCCESS );
}

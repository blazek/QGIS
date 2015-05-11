/***************************************************************************
    qgis.r.in.cpp
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
#include <grass/raster.h>
#include <grass/display.h>

#ifdef _MSC_VER
#include <float.h>
#endif
}

#include <QDataStream>
#include <QFile>
#include <QIODevice>

#include "qgsrectangle.h"
#include "qgsgrass.h"

//#ifdef _MSC_VER
//#define INFINITY (DBL_MAX+DBL_MAX)
//#define NAN (INFINITY-INFINITY)
//#endif

#if GRASS_VERSION_MAJOR >= 7
#define G_allocate_raster_buf Rast_allocate_buf
#define G_close_cell Rast_close
#define G_get_raster_map_type Rast_get_map_type
#define G_get_raster_row Rast_get_row
#define G_is_null_value Rast_is_null_value
#define G_open_raster_new Rast_open_new
#define G_short_history Rast_short_history
#define G_command_history Rast_command_history
#define G_write_history Rast_write_history
#endif

int main( int argc, char **argv )
{
  char *name;
  struct GModule *module;
  struct Option *map;
  struct Cell_head window;
  RASTER_MAP_TYPE raster_type;
  int cf;

  G_gisinit( argv[0] );

  module = G_define_module();
  module->description = ( "Output raster map layers in a format suitable for display in QGIS" );

  map = G_define_standard_option( G_OPT_R_OUTPUT );

  if ( G_parser( argc, argv ) )
    exit( EXIT_FAILURE );

  name = map->answer;

  QFile stdinFile;
  stdinFile.open(0, QIODevice::ReadOnly);

  QDataStream stdinStream(&stdinFile);

  QgsRectangle extent;
  qint32 rows, cols;
  stdinStream >> extent >> cols >> rows;

  //G_fatal_error("i = %d", i);
  //G_fatal_error( extent.toString().toAscii().data() );

  QString err = QgsGrass::setRegion( &window, extent, rows, cols);
  if ( !err.isEmpty() )
  {
    G_fatal_error("Cannot set region: %s", err.toUtf8().data());
  }

  G_set_window( &window );

  qint32 type;
  stdinStream >> type;
  raster_type = (RASTER_MAP_TYPE) type;

  cf = G_open_raster_new(name, raster_type);
  if (cf < 0)
  {
    G_fatal_error( "Unable to create raster map <%s>", name);
  }

  G_close_cell(cf);
  struct History history;
  G_short_history(name, "raster", &history);
  G_command_history(&history);
  G_write_history(name, &history);

  exit( EXIT_SUCCESS );
}

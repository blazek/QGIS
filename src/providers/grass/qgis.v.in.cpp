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

//#include <grass/gprojects.h>

//#ifdef _MSC_VER
//#include <float.h>
//#endif
}

#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QIODevice>

#include "qgsrectangle.h"
#include "qgsrasterblock.h"
#include "qgsgrass.h"

int main( int argc, char **argv )
{
  char *name;
  struct GModule *module;
  struct Option *map;
 // struct Cell_head window;
  int cf;

  G_gisinit( argv[0] );

  module = G_define_module();

  map = G_define_standard_option( G_OPT_V_OUTPUT );

  if ( G_parser( argc, argv ) )
    exit( EXIT_FAILURE );

  struct Map_info Map, Tmp;
  Vect_open_new(&Map, map->answer, 0);
  QDateTime now = QDateTime::currentDateTime();
  QString tmpName = QString("%1_tmp_%2").arg(map->answer).arg(now.toString("yyyyMMddhhmmss"));
  Vect_open_new(&Tmp, tmpName.toUtf8().data(), 0);

  QFile stdinFile;
  stdinFile.open(0, QIODevice::ReadOnly);

  QDataStream stdinStream(&stdinFile);

  //QgsRectangle extent;
  qint32 typeQint32;
  stdinStream >> typeQint32;
  QGis::WkbType wkbType = (QGis::WkbType)typeQint32;

  //G_fatal_error("wkbType = %d", wkbType);

  Vect_copy_map_lines(&Tmp, &Map);
  Vect_close(&Tmp);
  Vect_delete(tmpName.toUtf8().data());

  Vect_build(&Map);
  Vect_close(&Map);

  // TODO history

  exit( EXIT_SUCCESS );
}

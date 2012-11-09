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
  QgsDebugMsg( "libPath = " + libPath);
  mLibrary.setFileName ( libPath );
  if ( !mLibrary.load() )
  {
    QgsDebugMsg( "Cannot load original GRASS library" );
    return;
  }
}

int QgsGrassGisLib::errorRoutine( const char *msg, int fatal )
{
  QgsDebugMsg( QString( "error_routine (fatal = %1): %2" ).arg( fatal ).arg( msg ) );
  return 1;
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

int GRASS_LIB_EXPORT QgsGrassGisLib::G__gisinit(const char * version, const char * programName)
{
  QgsDebugMsg( QString("version = %1 programName = %2").arg(version).arg(programName));
  G_set_error_routine( &errorRoutine );
  return 0;
}

int G__gisinit(const char * version, const char * programName)
{
  return QgsGrassGisLib::instance()->G__gisinit(version,programName);
}

// Defined here just because parser in cmake does not recognize this kind of params
typedef int G_set_error_routine_type(int (*) (const char *, int));
int G_set_error_routine(int (*error_routine) (const char *, int))
{
  //QgsDebugMsg( "Entered" );
  G_set_error_routine_type* fn = (G_set_error_routine_type*) cast_to_fptr (QgsGrassGisLib::instance()->resolve( "G_set_error_routine" ));
  return fn(error_routine);
}


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
#include <qgsrectangle.h>

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
    static GRASS_LIB_EXPORT QgsGrassGisLib* instance();

    QgsGrassGisLib();

    int G__gisinit(const char * version, const char * programName);

    void * resolve( const char * symbol );

    static int errorRoutine( const char *msg, int fatal );

  private:
    /** pointer to canonical Singleton object */
    static QgsGrassGisLib* _instance;

    /** Original GRASS library handle */
    QLibrary mLibrary;
};

#endif // QGSGRASSGISLIB_H

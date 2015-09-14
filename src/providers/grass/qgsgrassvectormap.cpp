/***************************************************************************
                            qgsgrassvectormap.cpp
                             -------------------
    begin                : September, 2015
    copyright            : (C) 2015 by Radim Blazek
    email                : radim.blazek@gmail.com
 ***************************************************************************/
/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QFileInfo>
#include <QMessageBox>

#include "qgslogger.h"
#include "qgsgeometry.h"

#include "qgsgrass.h"
#include "qgsgrassvectormap.h"
#include "qgsgrassvectormaplayer.h"

extern "C"
{
#include <grass/version.h>
#include <grass/gprojects.h>
#include <grass/gis.h>
#include <grass/dbmi.h>
#if GRASS_VERSION_MAJOR < 7
#include <grass/Vect.h>
#else
#include <grass/vector.h>
#define BOUND_BOX bound_box
#endif
}

QList<QgsGrassVectorMap*> QgsGrassVectorMap::mMaps;

QgsGrassVectorMap::QgsGrassVectorMap( const QgsGrassObject & grassObject )
    : mGrassObject( grassObject )
    , mValid( false )
    , mFrozen( false )
    , mUpdate( false )
    , mVersion( 0 )
    , mMap( 0 )
    , mOldNumLines( 0 )
{
  QgsDebugMsg( "grassObject = " + grassObject.toString() );
  open();
}

QgsGrassVectorMap::~QgsGrassVectorMap()
{
  QgsDebugMsg( "grassObject = " + mGrassObject.toString() );
  // TODO close
  QgsGrass::vectDestroyMapStruct( mMap );
}

int QgsGrassVectorMap::userCount() const
{
  int count = 0;
  foreach ( QgsGrassVectorMapLayer *layer, mLayers )
  {
    count += layer->userCount();
  }
  return count;
}

bool QgsGrassVectorMap::open()
{
  // TODO: refresh layers (reopen)
  QgsDebugMsg( toString() );
  QgsGrass::init();
  QgsGrass::lock();
  QgsGrass::setLocation( mGrassObject.gisdbase(), mGrassObject.location() );

  // Find the vector
  const char *ms = G_find_vector2( mGrassObject.name().toUtf8().data(),  mGrassObject.mapset().toUtf8().data() );

  if ( !ms )
  {
    QgsDebugMsg( "Cannot find GRASS vector" );
    QgsGrass::unlock();
    return -1;
  }

  // Read the time of vector dir before Vect_open_old, because it may take long time (when the vector
  // could be owerwritten)
  QFileInfo di( mGrassObject.mapsetPath() + "/vector/" + mGrassObject.name() );
  mLastModified = di.lastModified();

  di.setFile( mGrassObject.mapsetPath() + "/vector/" + mGrassObject.name() + "/dbln" );
  mLastAttributesModified = di.lastModified();

  mMap = QgsGrass::vectNewMapStruct();
  // Do we have topology and cidx (level2)
  int level = -1;
  G_TRY
  {
    //Vect_set_open_level( 2 );
    level = Vect_open_old_head( mMap, mGrassObject.name().toUtf8().data(), mGrassObject.mapset().toUtf8().data() );
    Vect_close( mMap );
  }
  G_CATCH( QgsGrass::Exception &e )
  {
    QgsGrass::warning( e );
    level = -1;
  }

  if ( level == -1 )
  {
    QgsDebugMsg( "Cannot open GRASS vector head" );
    QgsGrass::unlock();
    return -1;
  }
  else if ( level == 1 )
  {
    QMessageBox::StandardButton ret = QMessageBox::question( 0, "Warning",
                                      QObject::tr( "GRASS vector map %1 does not have topology. Build topology?" ).arg( mGrassObject.name() ),
                                      QMessageBox::Ok | QMessageBox::Cancel );

    if ( ret == QMessageBox::Cancel )
    {
      QgsGrass::unlock();
      return -1;
    }
  }

  // Open vector
  G_TRY
  {
    Vect_set_open_level( level );
    Vect_open_old( mMap, mGrassObject.name().toUtf8().data(), mGrassObject.mapset().toUtf8().data() );
  }
  G_CATCH( QgsGrass::Exception &e )
  {
    QgsGrass::warning( QString( "Cannot open GRASS vector: %1" ).arg( e.what() ) );
    QgsGrass::unlock();
    return -1;
  }

  if ( level == 1 )
  {
    G_TRY
    {
#if defined(GRASS_VERSION_MAJOR) && defined(GRASS_VERSION_MINOR) && \
    ( ( GRASS_VERSION_MAJOR == 6 && GRASS_VERSION_MINOR >= 4 ) || GRASS_VERSION_MAJOR > 6 )
      Vect_build( mMap );
#else
      Vect_build( mMap, stderr );
#endif
    }
    G_CATCH( QgsGrass::Exception &e )
    {
      QgsGrass::warning( QString( "Cannot build topology: %1" ).arg( e.what() ) );
      QgsGrass::unlock();
      return -1;
    }
  }
  QgsDebugMsg( "GRASS map successfully opened" );

  QgsGrass::unlock();
  mValid = true;
  return true;
}


/* returns mapId or -1 on error */
QgsGrassVectorMap * QgsGrassVectorMap::openMap( const QgsGrassObject & grassObject )
{
  QgsDebugMsg( "grassObject = " + grassObject.toString() );

  // Check if this map is already opened
  foreach ( QgsGrassVectorMap *map, mMaps )
  {
    if ( map->grassObject() == grassObject )
    {
      QgsDebugMsg( "The map is already open" );
      return map;
    }
  }

  QgsGrassVectorMap *map = new QgsGrassVectorMap( grassObject );
  mMaps << map;
  return map;
}

bool QgsGrassVectorMap::startEdit()
{
  QgsDebugMsg( toString() );
  // Check number of maps (the problem may appear if static variables are not shared - runtime linker)
  if ( mMaps.size() == 0 )
  {
    QMessageBox::warning( 0, "Warning", "No maps opened in mMaps, probably problem in runtime linking, "
                          "static variables are not shared by provider and plugin." );
    return false;
  }

  /* Close map */
  mValid = false;

  QgsGrass::lock();
  //QgsGrass::setLocation( mGrassObject.gisdbase(), mGrassObject.location() );
  // Mapset must be set before Vect_close()
  QgsGrass::setMapset( mGrassObject.gisdbase(), mGrassObject.location(), mGrassObject.mapset() );
  QgsDebugMsg( "G_mapset() = " + QString( G_mapset() ) );

  // Set current mapset (mapset was previously checked by isGrassEditable() )
  // TODO: Should be done better / in other place ?
  //G__setenv(( char * )"MAPSET",  mGrassObject.mapset().toUtf8().data() );

  Vect_close( mMap );

  // TODO: Catch error
  int level = -1;
  try
  {
    level = Vect_open_update( mMap, mGrassObject.name().toUtf8().data(), mGrassObject.mapset().toUtf8().data() );
    if ( level < 2 )
    {
      QgsDebugMsg( "Cannot open GRASS vector for update on level 2." );
    }
  }
  catch ( QgsGrass::Exception &e )
  {
    Q_UNUSED( e );
    QgsDebugMsg( QString( "Cannot open GRASS vector for update: %1" ).arg( e.what() ) );
  }

  if ( level < 2 )
  {
    // reopen vector for reading
    try
    {
      Vect_set_open_level( 2 );
      level = Vect_open_old( mMap, mGrassObject.name().toUtf8().data(), mGrassObject.mapset().toUtf8().data() );
      if ( level < 2 )
      {
        QgsDebugMsg( QString( "Cannot reopen GRASS vector: %1" ).arg( QgsGrass::errorMessage() ) );
      }
    }
    catch ( QgsGrass::Exception &e )
    {
      Q_UNUSED( e );
      QgsDebugMsg( QString( "Cannot reopen GRASS vector: %1" ).arg( e.what() ) );
    }

    if ( level >= 2 )
    {
      mValid = true;
    }
    QgsGrass::unlock();
    return false;
  }
  Vect_set_category_index_update( mMap );

  // Write history
  Vect_hist_command( mMap );

  mOldNumLines = Vect_get_num_lines( mMap );
  QgsDebugMsg( QString( "Vector successfully reopened for update mOldNumLines = %1" ).arg( mOldNumLines ) );

  mUpdate = true;
  mValid = true;
  QgsGrass::unlock();
  return true;
}

bool QgsGrassVectorMap::closeEdit( bool newMap )
{
  Q_UNUSED( newMap );
  QgsDebugMsg( toString() );
  if ( !mValid || !mUpdate )
  {
    return false;
  }

  // mValid = false; cloes() is checking mValid

  QgsGrass::lock();
  // Mapset must be set before Vect_close()
  QgsGrass::setMapset( mGrassObject.gisdbase(), mGrassObject.location(), mGrassObject.mapset() );
  QgsDebugMsg( "G_mapset() = " + QString( G_mapset() ) );

  // Set current mapset (mapset was previously checked by isGrassEditable() )
  //G__setenv(( char * )"MAPSET",  mGrassObject.mapset().toUtf8().data() );
  //QgsDebugMsg( "G_mapset() = " + QString(G_mapset()) );

#if defined(GRASS_VERSION_MAJOR) && defined(GRASS_VERSION_MINOR) && \
    ( ( GRASS_VERSION_MAJOR == 6 && GRASS_VERSION_MINOR >= 4 ) || GRASS_VERSION_MAJOR > 6 )
  Vect_build_partial( mMap, GV_BUILD_NONE );
  Vect_build( mMap );
#else
  Vect_build_partial( mMap, GV_BUILD_NONE, NULL );
  Vect_build( mMap, stderr );
#endif

#if 0
  if ( Vect_save_topo( mMap ) )
  {
    QgsDebugMsg( "topo saved" );
  }
  else
  {
    QgsDebugMsg( "saving topo failed" );
  }
#endif

  // TODO?
#if 0
  // If a new map was created close the map and return
  if ( newMap )
  {
    QgsDebugMsg( QString( "mLayers.size() = %1" ).arg( mLayers.size() ) );
    mUpdate = false;
    // Map must be set as valid otherwise it is not closed and topo is not written
    mValid = true;
    // TODO refresh layers ?
    //closeLayer( mLayerId );
    QgsGrass::unlock();
    return true;
  }
#endif

  mUpdate = false;
  QgsGrass::unlock();
  // We do not need to reopen
  close();
  open();
  mVersion++;
  QgsDebugMsg( "edit closed" );
  return mValid;
}

QgsGrassVectorMapLayer * QgsGrassVectorMap::openLayer( int field )
{
  QgsDebugMsg( "entered" );

  // Check if this layer is already open
  foreach ( QgsGrassVectorMapLayer *layer, mLayers )
  {
    if ( !layer->isValid() )
    {
      continue;
    }

    if ( layer->field() == field )
    {
      QgsDebugMsg( "Layer exists" );
      layer->addUser();
      return layer;
    }
  }

  QgsGrassVectorMapLayer *layer = new QgsGrassVectorMapLayer( this, field ) ;
  layer->load();
  mLayers << layer;
  layer->addUser();
  return layer;
}

QgsGrassVectorMapLayer * QgsGrassVectorMap::openLayer( const QgsGrassObject & grassObject, int field )
{
  QgsDebugMsg( QString( "grassObject = %1 field = %2" ).arg( grassObject.toString() ).arg( field ) );

  QgsGrassVectorMap * map = openMap( grassObject );
  if ( !map )
  {
    QgsDebugMsg( "cannot open the map" );
    return 0;
  }

  return map->openLayer( field );
}

void QgsGrassVectorMap::closeLayer( QgsGrassVectorMapLayer * layer )
{
  if ( !layer || !layer->map() )
  {
    return;
  }

  QgsDebugMsg( QString( "Close layer %1 usersCount = %2" ).arg( layer->map()->grassObject().toString() )
               .arg( layer->userCount() ) );

  layer->removeUser();

  if ( layer->userCount() == 0 )   // No more users, free sources
  {
    QgsDebugMsg( "No more users -> clear" );
    layer->clear();
  }
  if ( layer->map()->userCount() == 0 )
  {
    QgsDebugMsg( "No more map users -> close" );
    layer->map()->close();
  }
  QgsDebugMsg( "layer closed" );
}

void QgsGrassVectorMap::close()
{
  QgsDebugMsg( toString() );
  QgsGrass::lock();
  if ( !mValid )
  {
    QgsDebugMsg( "map is not valid" );
  }
  else
  {
    // Mapset must be set before Vect_close()
    QgsGrass::setMapset( mGrassObject.gisdbase(), mGrassObject.location(), mGrassObject.mapset() );

    // TODO necessary?
#if 0
    bool mapsetunset = !G__getenv( "MAPSET" ) || !*G__getenv( "MAPSET" );
    if ( mapsetunset )
    {
      // TODO: Should be done better / in other place ?
      // TODO: Is it necessary for close ?
      G__setenv(( char * )"MAPSET", mGrassObject.mapset().toUtf8().data() );
    }
#endif
    G_TRY
    {
      Vect_close( mMap );
      QgsDebugMsg( "map closed" );
    }
    G_CATCH( QgsGrass::Exception &e )
    {
      QgsDebugMsg( "Vect_close failed:" + QString( e.what() ) );
    }
    // TODO necessary?
#if 0
    if ( mapsetunset )
    {
      G__setenv(( char * )"MAPSET", "" );
    }
#endif
  }
  QgsGrass::vectDestroyMapStruct( mMap );
  mMap = 0;
  //mOldNumLines = 0;
  mValid = false;
  QgsGrass::unlock();
}

void QgsGrassVectorMap::update()
{
  QgsDebugMsg( toString() );

  // Close and reopen
  close();
  open();
}

bool QgsGrassVectorMap::mapOutdated()
{
  QgsDebugMsg( "entered" );

  QString dp = mGrassObject.mapsetPath() + "/vector/" + mGrassObject.name();
  QFileInfo di( dp );

  if ( mLastModified < di.lastModified() )
  {
    // If the cidx file has been deleted, the map is currently being modified
    // by an external tool. Do not update until the cidx file has been recreated.
    if ( !QFileInfo( dp + "/cidx" ).exists() )
    {
      QgsDebugMsg( "The map is being modified and is unavailable : " + mGrassObject.toString() );
      return false;
    }
    QgsDebugMsg( "The map was modified : " + mGrassObject.toString() );
    return true;
  }
  return false;
}

bool QgsGrassVectorMap::attributesOutdated( )
{
  QgsDebugMsg( "entered" );


  QString dp = mGrassObject.mapsetPath() + "/vector/" + mGrassObject.name() + "/dbln";
  QFileInfo di( dp );

  if ( mLastAttributesModified < di.lastModified() )
  {
    QgsDebugMsg( "The attributes of the layer were modified : " + mGrassObject.toString() );

    return true;
  }
  return false;
}

int QgsGrassVectorMap::numLines()
{
  QgsDebugMsg( "entered" );

  return ( Vect_get_num_lines( mMap ) );
}

QString QgsGrassVectorMap::toString()
{
  return mGrassObject.mapsetPath() + "/" +  mGrassObject.name();
}

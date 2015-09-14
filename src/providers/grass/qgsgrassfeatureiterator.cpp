/***************************************************************************
    qgsgrassfeatureiterator.cpp
    ---------------------
    begin                : Juli 2012
    copyright            : (C) 2012 by Martin Dobias
    email                : wonder dot sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <QObject>
#include <QTextCodec>

#include "qgsgrass.h"
#include "qgsgrassfeatureiterator.h"
#include "qgsgrassprovider.h"
#include "qgsgrassvectormap.h"

#include "qgsapplication.h"
#include "qgsgeometry.h"
#include "qgslogger.h"
#include "qgsmessagelog.h"

extern "C"
{
#include <grass/version.h>

#if GRASS_VERSION_MAJOR < 7
#include <grass/Vect.h>
#else
#include <grass/vector.h>
#define BOUND_BOX bound_box
#endif
}

#if GRASS_VERSION_MAJOR < 7
#else

void copy_boxlist_and_destroy( struct boxlist *blist, struct ilist * list )
{
  Vect_reset_list( list );
  for ( int i = 0; i < blist->n_values; i++ )
  {
    Vect_list_append( list, blist->id[i] );
  }
  Vect_destroy_boxlist( blist );
}

#define Vect_select_lines_by_box(map, box, type, list) \
  { \
    struct boxlist *blist = Vect_new_boxlist(0);\
    Vect_select_lines_by_box( (map), (box), (type), blist); \
    copy_boxlist_and_destroy( blist, (list) );\
  }
#define Vect_select_areas_by_box(map, box, list) \
  { \
    struct boxlist *blist = Vect_new_boxlist(0);\
    Vect_select_areas_by_box( (map), (box), blist); \
    copy_boxlist_and_destroy( blist, (list) );\
  }
#endif

QMutex QgsGrassFeatureIterator::sMutex;

QgsGrassFeatureIterator::QgsGrassFeatureIterator( QgsGrassFeatureSource* source, bool ownSource, const QgsFeatureRequest& request )
    : QgsAbstractFeatureIteratorFromSource<QgsGrassFeatureSource>( source, ownSource, request )
    , mNextCidx( 0 )
    , mNextLid( 1 )
{
  QgsDebugMsg( "entered" );
  // WARNING: the iterater cannot use mutex lock for its whole life, because QgsVectorLayerFeatureIterator is opening
  // multiple iterators if features are edited -> lock only critical sections

  // Init structures
  mPoints = Vect_new_line_struct();
  mCats = Vect_new_cats_struct();
  mList = Vect_new_list();

  // Create selection
  int size = 1 + qMax( Vect_get_num_lines( mSource->map() ), Vect_get_num_areas( mSource->map() ) );
  QgsDebugMsg( QString( "mSelection.resize(%1)" ).arg( size ) );
  mSelection.resize( size );

  if ( !request.filterRect().isNull() )
  {
    setSelectionRect( request.filterRect(), request.flags() & QgsFeatureRequest::ExactIntersect );
  }
  else
  {
    //no filter - use all features
    mSelection.fill( true );
  }
}

void QgsGrassFeatureIterator::setSelectionRect( const QgsRectangle& rect, bool useIntersect )
{
  QgsDebugMsg( QString( "useIntersect = %1 rect = %2" ).arg( useIntersect ).arg( rect.toString() ) );
  //sMutex.lock();
  //QgsDebugMsg( "locked" );
  QgsGrass::lock();
  //apply selection rectangle
  mSelection.fill( false );

  BOUND_BOX box;
  box.N = rect.yMaximum(); box.S = rect.yMinimum();
  box.E = rect.xMaximum(); box.W = rect.xMinimum();
  box.T = PORT_DOUBLE_MAX; box.B = -PORT_DOUBLE_MAX;

  if ( !useIntersect )
  { // select by bounding boxes only
    if ( mSource->mLayerType == QgsGrassProvider::POINT || mSource->mLayerType == QgsGrassProvider::CENTROID ||
         mSource->mLayerType == QgsGrassProvider::LINE || mSource->mLayerType == QgsGrassProvider::FACE ||
         mSource->mLayerType == QgsGrassProvider::BOUNDARY ||
         mSource->mLayerType == QgsGrassProvider::TOPO_POINT || mSource->mLayerType == QgsGrassProvider::TOPO_LINE ||
         mSource->mEditing )
    {
      QgsDebugMsg( "Vect_select_lines_by_box" );
      int type = mSource->mGrassType;
      if ( mSource->mEditing )
      {
        type = GV_POINTS | GV_LINES;
      }
      QgsDebugMsg( QString( "type = %1" ).arg( type ) );
      Vect_select_lines_by_box( mSource->map(), &box, type, mList );
    }
    else if ( mSource->mLayerType == QgsGrassProvider::POLYGON )
    {
      Vect_select_areas_by_box( mSource->map(), &box, mList );
    }
    else if ( mSource->mLayerType == QgsGrassProvider::TOPO_NODE )
    {
      Vect_select_nodes_by_box( mSource->map(), &box, mList );
    }
  }
  else
  { // check intersection
    struct line_pnts *Polygon;

    Polygon = Vect_new_line_struct();

    // Using z coor -PORT_DOUBLE_MAX/PORT_DOUBLE_MAX we cover 3D, Vect_select_lines_by_polygon is
    // using dig_line_box to get the box, it is not perfect, Vect_select_lines_by_polygon
    // should clarify better how 2D/3D is treated
    Vect_append_point( Polygon, rect.xMinimum(), rect.yMinimum(), -PORT_DOUBLE_MAX );
    Vect_append_point( Polygon, rect.xMaximum(), rect.yMinimum(), PORT_DOUBLE_MAX );
    Vect_append_point( Polygon, rect.xMaximum(), rect.yMaximum(), 0 );
    Vect_append_point( Polygon, rect.xMinimum(), rect.yMaximum(), 0 );
    Vect_append_point( Polygon, rect.xMinimum(), rect.yMinimum(), 0 );

    if ( mSource->mLayerType == QgsGrassProvider::POINT || mSource->mLayerType == QgsGrassProvider::CENTROID ||
         mSource->mLayerType == QgsGrassProvider::LINE || mSource->mLayerType == QgsGrassProvider::FACE ||
         mSource->mLayerType == QgsGrassProvider::BOUNDARY ||
         mSource->mLayerType == QgsGrassProvider::TOPO_POINT || mSource->mLayerType == QgsGrassProvider::TOPO_LINE ||
         mSource->mEditing )
    {
      QgsDebugMsg( "Vect_select_lines_by_polygon" );
      int type = mSource->mGrassType;
      if ( mSource->mEditing )
      {
        type = GV_POINTS | GV_LINES;
      }
      QgsDebugMsg( QString( "type = %1" ).arg( type ) );
      Vect_select_lines_by_polygon( mSource->map(), Polygon, 0, NULL, type, mList );
    }
    else if ( mSource->mLayerType == QgsGrassProvider::POLYGON )
    {
      Vect_select_areas_by_polygon( mSource->map(), Polygon, 0, NULL, mList );
    }
    else if ( mSource->mLayerType == QgsGrassProvider::TOPO_NODE )
    {
      // There is no Vect_select_nodes_by_polygon but for nodes it is the same as by box
      Vect_select_nodes_by_box( mSource->map(), &box, mList );
    }

    Vect_destroy_line_struct( Polygon );
  }
  for ( int i = 0; i < mList->n_values; i++ )
  {
    int lid = mList->value[i];
    if ( lid < 1 || lid >= mSelection.size() ) // should not happen
    {
      QgsDebugMsg( QString( "lid %1 out of range <1,%2>" ).arg( lid ).arg( mSelection.size() ) );
      continue;
    }
    mSelection.setBit( lid );
  }
  QgsDebugMsg( QString( " %1 features selected" ).arg( mList->n_values ) );
  QgsGrass::unlock();

}

QgsGrassFeatureIterator::~QgsGrassFeatureIterator()
{
  close();
}

bool QgsGrassFeatureIterator::fetchFeature( QgsFeature& feature )
{
  QgsDebugMsgLevel( "entered", 3 );
  if ( mClosed )
  {
    return false;
  }

  feature.setValid( false );
  //feature.setGeometry( 0 );

  /* TODO: handle editing
  if ( P->isEdited() || P->isFrozen() || !P->mValid )
  {
    close();
    return false;
  }
  */

  // TODO: is this necessary? the same is checked below
#if 0
  if ( !QgsGrassProvider::isTopoType( mSource->mLayerType ) && ( mSource->mCidxFieldIndex < 0 || mNextCidx >= mSource->mCidxFieldNumCats ) )
  {
    QgsDebugMsgLevel( "entered", 3 );
    close();
    return false; // No features, no features in this layer
  }
#endif

  // TODO: locking each feature is too expensive - is it locking here necessary?
  // What happens with map structures if lines are written/rewritten/deleted? Just reading would be probably OK,
  QgsGrass::lock();
  bool filterById = mRequest.filterType() == QgsFeatureRequest::FilterFid;
  int cat = 0;
  int type = 0;
  int lid = 0;
  QgsFeatureId featureId = 0;

  if ( mSource->mEditing )
  {
    QgsDebugMsg( "newLids:" );
    foreach ( int oldLid, mSource->mLayer->map()->newLids().keys() )
    {
      QgsDebugMsg( QString( "%1 -> %2" ).arg( oldLid ).arg( mSource->mLayer->map()->newLids().value( oldLid ) ) );
    }
  }

  //int found = 0;
  if ( filterById )
  {
    featureId = mRequest.filterFid();
    lid = lidFormFid( mRequest.filterFid() );
    if ( mSource->mLayer->map()->newLids().contains( lid ) )
    {
      lid = mSource->mLayer->map()->newLids().value( mNextLid );
      QgsDebugMsg( QString( "line %1 rewritten -> realLid = %2" ).arg( lidFormFid( mRequest.filterFid() ) ).arg( lid ) );
    }
    if ( !Vect_line_alive( mSource->map(), lid ) )
    {
      close();
      QgsGrass::unlock();
      return false;
    }
    type = Vect_read_line( mSource->map(), 0, 0, lid );

    // TODO real cat when line/cat was rewritten?!
    cat = catFormFid( mRequest.filterFid() );
    QgsDebugMsg( QString( "lid = %1 cat = %2" ).arg( lid ).arg( cat ) );
  }
  else
  {
    // Get next line/area id
    while ( true )
    {
      // TODO: if selection is used, go only through the list of selected values
      cat = 0;
      type = 0;
      lid = 0;
      QgsDebugMsgLevel( QString( "mNextLid = %1 mNextCidx = %2 numLines() = %3" ).arg( mNextLid ).arg( mNextCidx ).arg( mSource->mLayer->map()->numLines() ), 3 );
      if ( mSource->mEditing )
      {
        // TODO should be numLines before editing started (?), but another layer
        // where editing started later mest have different, because its buffer does not have previous changes
        // -> editing of more layers must be synchronized or not allowed
        //if ( mNextLid > mSource->mLayer->map()->numOldLines() )
        if ( mNextLid > mSource->mLayer->map()->numLines() )
        {
          QgsDebugMsgLevel( "mNextLid > numLines()", 3 );
          break;
        }

        int realLid = mNextLid;
        if ( mSource->mLayer->map()->newLids().contains( mNextLid ) )
        {
          realLid = mSource->mLayer->map()->newLids().value( mNextLid );
          QgsDebugMsg( QString( "line %1 rewritten ->  realLid = %2" ).arg( mNextLid ).arg( realLid ) );
        }

        if ( !Vect_line_alive( mSource->map(), realLid ) ) // should not be necessary for rewritten lines
        {
          mNextLid++;
          continue;
        }

        int tmpType = Vect_read_line( mSource->map(), 0, mCats, realLid );
        if ( mCats->n_cats == 0 )
        {
          lid = realLid;
          type = tmpType;
          cat = 0;
          featureId = makeFeatureId( mNextLid, cat );
          mNextLid++;
        }
        else
        {
          if ( mNextCidx >= mCats->n_cats )
          {
            mNextCidx = 0;
            mNextLid++;
            continue;
          }
          else
          {
            // Show only cats of currently edited layer
            if ( mCats->field[mNextCidx] != mSource->mLayer->field() )
            {
              mNextCidx++;
              continue;
            }
            else
            {
              lid = realLid;
              type = tmpType;
              cat = mCats->cat[mNextCidx];
              featureId = makeFeatureId( mNextLid, cat );
              mNextCidx++;
            }
          }
        }
      }
      else if ( mSource->mLayerType == QgsGrassProvider::TOPO_POINT || mSource->mLayerType == QgsGrassProvider::TOPO_LINE )
      {
        if ( mNextLid > Vect_get_num_lines( mSource->map() ) )
        {
          break;
        }
        lid = mNextLid;
        type = Vect_read_line( mSource->map(), 0, 0, mNextLid++ );
        if ( !( type & mSource->mGrassType ) )
        {
          continue;
        }
        featureId = lid;
      }
      else if ( mSource->mLayerType == QgsGrassProvider::TOPO_NODE )
      {
        if ( mNextLid > Vect_get_num_nodes( mSource->map() ) )
        {
          break;
        }
        lid = mNextLid;
        type = 0;
        mNextLid++;
        featureId = lid;
      }
      else // standard layer
      {
        if ( mNextCidx >= mSource->mCidxFieldNumCats )
        {
          break;
        }
        int tmpLid, tmpType, tmpCat;
        Vect_cidx_get_cat_by_index( mSource->map(), mSource->mCidxFieldIndex, mNextCidx++, &tmpCat, &tmpType, &tmpLid );
        // Warning: selection array is only of type line/area of current layer -> check type first
        if ( !( tmpType & mSource->mGrassType ) )
        {
          continue;
        }

        // The 'id' is a unique id of a GRASS geometry object (point, line, area)
        // but it cannot be used as QgsFeatureId because one geometry object may
        // represent more features because it may have more categories.
        lid = tmpLid;
        cat = tmpCat;
        type = tmpType;
        featureId = makeFeatureId( lid, cat );
      }

      // TODO: fix selection for mEditing
      //if ( !mSource->mEditing && !mSelection[id] )
      if ( lid < 1 || lid >= mSelection.size() || !mSelection[lid] )
      {
        QgsDebugMsgLevel( QString( "lid = %1 not in selection" ).arg( lid ), 3 );
        continue;
      }
      else
      {
        QgsDebugMsgLevel( QString( "lid = %1 in selection" ).arg( lid ), 3 );
      }
      break;
    }
  }
  if ( lid == 0 || lid > mSource->mLayer->map()->numLines() )
  {
    QgsDebugMsg( QString( "lid = %1 -> close" ).arg( lid ) );
    close();
    QgsGrass::unlock();
    return false; // No more features
  }
  if ( type == 0 ) // should not happen
  {
    QgsDebugMsg( "unknown type" );
    close();
    QgsGrass::unlock();
    return false;
  }
  QgsDebugMsgLevel( QString( "lid = %1 type = %2 cat = %3 fatureId = %4" ).arg( lid ).arg( type ).arg( cat ).arg( featureId ), 3 );

  feature.setFeatureId( featureId );
  //feature.initAttributes( mSource->mFields.count() );
  QgsDebugMsgLevel( QString( "mSource->mFields.size() = %1" ).arg( mSource->mFields.size() ), 3 );
  feature.setFields( mSource->mFields ); // allow name-based attribute lookups

  if ( !( mRequest.flags() & QgsFeatureRequest::NoGeometry ) )
  {
    // TODO ???
#if 0
    // Changed geometry are always read from cache
    if ( mSource->mEditing && mSource->mChangedFeatures.contains( mRequest.filterFid() ) )
    {
      QgsDebugMsg( QString( "filterById = %1 mRequest.filterFid() = %2 mSource->mChangedFeatures.size() = %3" ).arg( filterById ).arg( mRequest.filterFid() ).arg( mSource->mChangedFeatures.size() ) );
      QgsFeature f = mSource->mChangedFeatures.value( mRequest.filterFid() );
      QgsDebugMsg( QString( "return features from mChangedFeatures id = %1" ).arg( f.id() ) );
      feature.setFeatureId( f.id() );
      feature.initAttributes( mSource->mFields.count() );
      feature.setFields( &( mSource->mFields ) ); // allow name-based attribute lookups
      feature.setAttributes( f.attributes() );
      feature.setGeometry( new QgsGeometry( *( f.geometry() ) ) );
      feature.setValid( true );
      QgsGrass::unlock();
      return true;
    }
    else
    {
    }
#endif
    setFeatureGeometry( feature, lid, type );
  }

  if ( !QgsGrassProvider::isTopoType( mSource->mLayerType ) )
  {
    QgsGrassProvider::TopoSymbol symbol = QgsGrassProvider::TopoUndefined;
    if ( mSource->mEditing )
    {
      symbol = topoSymbol( lid, type );
    }

    if ( mRequest.flags() & QgsFeatureRequest::SubsetOfAttributes )
      setFeatureAttributes( cat, &feature, mRequest.subsetOfAttributes(), symbol );
    else
      setFeatureAttributes( cat, &feature, symbol );
  }
  else
  {
    feature.setAttribute( 0, lid );
#if GRASS_VERSION_MAJOR < 7
    if ( mSource->mLayerType == QgsGrassProvider::TOPO_POINT || mSource->mLayerType == QgsGrassProvider::TOPO_LINE )
#else
    /* No more topo points in GRASS 7 */
    if ( mSource->mLayerType == QgsGrassProvider::TOPO_LINE )
#endif
    {
      feature.setAttribute( 1, QgsGrass::vectorTypeName( type ) );

      int node1, node2;;
      close();
      Vect_get_line_nodes( mSource->map(), lid, &node1, &node2 );
      feature.setAttribute( 2, node1 );
      if ( mSource->mLayerType == QgsGrassProvider::TOPO_LINE )
      {
        feature.setAttribute( 3, node2 );
      }
    }

    if ( mSource->mLayerType == QgsGrassProvider::TOPO_LINE )
    {
      if ( type == GV_BOUNDARY )
      {
        int left, right;
        Vect_get_line_areas( mSource->map(), lid, &left, &right );
        feature.setAttribute( 4, left );
        feature.setAttribute( 5, right );
      }
    }
    else if ( mSource->mLayerType == QgsGrassProvider::TOPO_NODE )
    {
      QString lines;
      int nlines = Vect_get_node_n_lines( mSource->map(), lid );
      for ( int i = 0; i < nlines; i++ )
      {
        int line = Vect_get_node_line( mSource->map(), lid, i );
        if ( i > 0 ) lines += ",";
        lines += QString::number( line );
      }
      feature.setAttribute( 1, lines );
    }
  }
  feature.setValid( true );
  QgsGrass::unlock();

  return true;
}




bool QgsGrassFeatureIterator::rewind()
{
  if ( mClosed )
    return false;

  /* TODO: handle editing
  if ( P->isEdited() || P->isFrozen() || !P->mValid )
    return false;
  */

  mNextCidx = 0;
  mNextLid = 1;

  return true;
}

bool QgsGrassFeatureIterator::close()
{
  QgsDebugMsg( "entered" );
  if ( mClosed )
  {
    QgsDebugMsg( "already closed" );
    return false;
  }

  iteratorClosed();

  // finalization
  Vect_destroy_line_struct( mPoints );
  mPoints = 0;
  Vect_destroy_cats_struct( mCats );
  mCats = 0;
  Vect_destroy_list( mList );
  mList = 0;

  mClosed = true;
  QgsDebugMsg( "closed" );
  //sMutex.unlock();
  return true;
}

void QgsGrassFeatureIterator::setFeatureGeometry( QgsFeature& feature, int id, int type )
{
  QgsDebugMsgLevel( QString( "id = %1 type = %2" ).arg( id ).arg( type ), 3 );
  unsigned char *wkb = 0;
  int wkbsize = 0;
  qint32 qgisType = mSource->mQgisType;

  if ( type & ( GV_POINTS | GV_LINES | GV_FACE ) || mSource->mLayerType == QgsGrassProvider::TOPO_NODE ) /* points or lines */
  {
    if ( mSource->mLayerType == QgsGrassProvider::TOPO_NODE )
    {
      double x, y, z;
      Vect_get_node_coor( mSource->map(), id, &x, &y, &z );
      Vect_reset_line( mPoints );
      Vect_append_point( mPoints, x, y, z );
    }
    else
    {
      Vect_read_line( mSource->map(), mPoints, 0, id );
    }
    qint32 npoints = mPoints->n_points;

    if ( mSource->mLayerType == QgsGrassProvider::TOPO_NODE )
    {
      wkbsize = 1 + 4 + 2 * 8;
    }
    else if ( type & GV_POINTS )
    {
      wkbsize = 1 + 4 + 2 * 8;
    }
    else if ( type & GV_LINES )
    {
      wkbsize = 1 + 4 + 4 + npoints * 2 * 8;
    }
    else // GV_FACE
    {
      wkbsize = 1 + 4 + 4 + 4 + npoints * 2 * 8;
    }
    wkb = new unsigned char[wkbsize];
    unsigned char *wkbp = wkb;
    wkbp[0] = ( unsigned char ) QgsApplication::endian();
    wkbp += 1;

    /* WKB type */
    if ( mSource->mEditing )
    {
      // TODO
      //QGis::WkbType wkbType;
      qint32 wkbType = QGis::WKBUnknown;
      if ( type & GV_LINES )
        wkbType = QGis::WKBLineString;
      else if ( type & GV_POINTS )
        wkbType = QGis::WKBPoint;

      memcpy( wkbp, &wkbType, 4 );
    }
    else
    {
      memcpy( wkbp, &qgisType, 4 );
    }
    wkbp += 4;

    /* Number of rings */
    if ( type & GV_FACE )
    {
      qint32 nrings = 1;
      memcpy( wkbp, &nrings, 4 );
      wkbp += 4;
    }

    /* number of points */
    if ( type & ( GV_LINES | GV_FACE ) )
    {
      QgsDebugMsg( QString( "set npoints = %1" ).arg( npoints ) );
      memcpy( wkbp, &npoints, 4 );
      wkbp += 4;
    }

    for ( int i = 0; i < npoints; i++ )
    {
      memcpy( wkbp, &( mPoints->x[i] ), 8 );
      memcpy( wkbp + 8, &( mPoints->y[i] ), 8 );
      wkbp += 16;
    }
  }
  else   // GV_AREA
  {
    Vect_get_area_points( mSource->map(), id, mPoints );
    int npoints = mPoints->n_points;

    wkbsize = 1 + 4 + 4 + 4 + npoints * 2 * 8; // size without islands
    wkb = new unsigned char[wkbsize];
    wkb[0] = ( unsigned char ) QgsApplication::endian();
    int offset = 1;

    /* WKB type */
    memcpy( wkb + offset, &qgisType, 4 );
    offset += 4;

    /* Number of rings */
    qint32 nisles = Vect_get_area_num_isles( mSource->map(), id );
    qint32 nrings = 1 + nisles;
    memcpy( wkb + offset, &nrings, 4 );
    offset += 4;

    /* Outer ring */
    memcpy( wkb + offset, &npoints, 4 );
    offset += 4;
    for ( int i = 0; i < npoints; i++ )
    {
      memcpy( wkb + offset, &( mPoints->x[i] ), 8 );
      memcpy( wkb + offset + 8, &( mPoints->y[i] ), 8 );
      offset += 16;
    }

    /* Isles */
    for ( int i = 0; i < nisles; i++ )
    {
      Vect_get_isle_points( mSource->map(), Vect_get_area_isle( mSource->map(), id, i ), mPoints );
      npoints = mPoints->n_points;

      // add space
      wkbsize += 4 + npoints * 2 * 8;
      wkb = ( unsigned char * ) realloc( wkb, wkbsize );

      memcpy( wkb + offset, &npoints, 4 );
      offset += 4;
      for ( int i = 0; i < npoints; i++ )
      {
        memcpy( wkb + offset, &( mPoints->x[i] ), 8 );
        memcpy( wkb + offset + 8, &( mPoints->y[i] ), 8 );
        offset += 16;
      }
    }
  }

  QgsDebugMsgLevel( QString( "wkbsize = %1" ).arg( wkbsize ), 3 );
  feature.setGeometryAndOwnership( wkb, wkbsize );
}

QgsFeatureId QgsGrassFeatureIterator::makeFeatureId( int grassId, int cat )
{
  // Because GRASS object id and category are both int and QgsFeatureId is qint64
  // we can create unique QgsFeatureId from GRASS id and cat
  return ( QgsFeatureId )grassId * 1000000000 + cat;
}

int QgsGrassFeatureIterator::lidFormFid( QgsFeatureId fid )
{
  return fid / 1000000000;
}

int QgsGrassFeatureIterator::catFormFid( QgsFeatureId fid )
{
  return fid % 1000000000;
}

QgsGrassProvider::TopoSymbol QgsGrassFeatureIterator::topoSymbol( int lid, int type )
{
  QgsGrassProvider::TopoSymbol symbol = QgsGrassProvider::TopoUndefined;
  if ( type == GV_POINT )
  {
    symbol = QgsGrassProvider::TopoPoint;
  }
  else if ( type == GV_CENTROID )
  {
    int area = Vect_get_centroid_area( mSource->map(), lid );
    if ( area == 0 )
      symbol = QgsGrassProvider::TopoCentroidOut;
    else if ( area > 0 )
      symbol = QgsGrassProvider::TopoCentroidIn;
    else
      symbol = QgsGrassProvider::TopoCentroidDupl; /* area < 0 */
  }
  else if ( type == GV_LINE )
  {
    symbol = QgsGrassProvider::TopoLine;
  }
  else if ( type == GV_BOUNDARY )
  {
    int left, right;
    Vect_get_line_areas( mSource->map(), lid, &left, &right );
    if ( left != 0 && right != 0 )
    {
      symbol = QgsGrassProvider::TopoBoundary2;
    }
    else if ( left == 0 && right == 0 )
    {
      symbol = QgsGrassProvider::TopoBoundary0;
    }
    else
    {
      symbol = QgsGrassProvider::TopoBoundary1;
    }
  }
  QgsDebugMsgLevel( QString( "lid = %1 type = %2 symbol = %3" ).arg( lid ).arg( type ).arg( symbol ), 3 );
  return symbol;
}

void QgsGrassFeatureIterator::setFeatureAttributes( int cat, QgsFeature *feature, QgsGrassProvider::TopoSymbol symbol )
{
  QgsDebugMsgLevel( QString( "setFeatureAttributes cat = %1" ).arg( cat ), 3 );
  QgsAttributeList attlist;
  int nFields =  mSource->mLayer->fields().size();
  if ( nFields > 0 )
  {
    for ( int i = 0; i <  mSource->mLayer->fields().size(); i++ )
    {
      attlist << i;
    }
  }
  else
  {
    attlist << 0;
  }
  return setFeatureAttributes( cat, feature, attlist, symbol );
}

void QgsGrassFeatureIterator::setFeatureAttributes( int cat, QgsFeature *feature, const QgsAttributeList& attlist, QgsGrassProvider::TopoSymbol symbol )
{
  QgsDebugMsgLevel( QString( "setFeatureAttributes cat = %1 symbol = %2" ).arg( cat ).arg( symbol ), 3 );
  int nFields = mSource->mLayer->fields().size();
  int nAttributes = nFields;
  if ( mSource->mEditing )
  {
    //nAttributes += 1;
  }
  feature->initAttributes( nAttributes );
  if ( mSource->mLayer->hasTable() )
  {
    for ( QgsAttributeList::const_iterator iter = attlist.begin(); iter != attlist.end(); ++iter )
    {
      if ( !mSource->mLayer->attributes().contains( cat ) )
      {
        QgsDebugMsgLevel( QString( "cat %1 not found in attributes" ).arg( cat ), 3 );
      }
      QVariant value = mSource->mLayer->attributes().value( cat ).value( *iter );
      if ( value.type() == QVariant::ByteArray )
      {
        value = QVariant( mSource->mEncoding->toUnicode( value.toByteArray() ) );
      }
      QgsDebugMsgLevel( QString( "iter = %1 value = %2" ).arg( *iter ).arg( value.toString() ), 3 );
      feature->setAttribute( *iter, value );
    }
  }
  else if ( attlist.contains( 0 ) ) // no table and first attribute requested -> add cat
  {
    QgsDebugMsgLevel( QString( "no table, set attribute 0 to cat %1" ).arg( cat ), 3 );
    feature->setAttribute( 0, QVariant( cat ) );
  }
  else
  {
    QgsDebugMsgLevel( "no table, cat not requested", 3 );
  }
  if ( mSource->mEditing )
  {
    // append topo_symbol
    int idx = nAttributes - 1;
    QgsDebugMsgLevel( QString( "set attribute %1 to symbol %2" ).arg( idx ).arg( symbol ), 3 );
    //feature->setAttribute( 0, QVariant( symbol ) ); // debug
    feature->setAttribute( idx, QVariant( symbol ) );
  }
}

//void QgsGrassFeatureIterator::lock()
//{
//}

//  ------------------ QgsGrassFeatureSource ------------------
QgsGrassFeatureSource::QgsGrassFeatureSource( const QgsGrassProvider* p )
    : mLayerType( p->mLayerType )
    , mGrassType( p->mGrassType )
    , mQgisType( p->mQgisType )
    , mCidxFieldIndex( p->mCidxFieldIndex )
    , mCidxFieldNumCats( p->mCidxFieldNumCats )
    , mFields( p->fields() )
    , mEncoding( p->mEncoding )
    , mEditing( p->mEditBuffer )
    //, mEditFids( p->mEditFids )
    //, mChangedFeatures( p->mChangedFeatures )
{
  mLayer = QgsGrassVectorMap::openLayer( p->grassObject(), p->mLayerField );

  Q_ASSERT( mLayer );
#if 0
  if ( mEditing )
  {
    mFields.clear();
    mFields.append( QgsField( "topo_symbol", QVariant::Int, "int" ) );
  }
#endif
}

QgsGrassFeatureSource::~QgsGrassFeatureSource()
{
  QgsGrassVectorMap::closeLayer( mLayer );
}

QgsFeatureIterator QgsGrassFeatureSource::getFeatures( const QgsFeatureRequest& request )
{
  QgsDebugMsg( "QgsGrassFeatureSource::getFeatures" );
  return QgsFeatureIterator( new QgsGrassFeatureIterator( this, false, request ) );
}

struct Map_info* QgsGrassFeatureSource::map()
{
  return  mLayer->map()->map();
}

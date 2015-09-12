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
  // TODO!!! enable mutex, disabled because QgsVectorLayerFeatureIterator is opening multiple iterators if features are edited
  //sMutex.lock();
  //QgsDebugMsg( "after lock" );

  // Init structures
  mPoints = Vect_new_line_struct();
  mCats = Vect_new_cats_struct();
  mList = Vect_new_list();

  // Create selection array
  allocateSelection( mSource->map() );
  resetSelection( 1 );

  if ( !request.filterRect().isNull() )
  {
    setSelectionRect( request.filterRect(), request.flags() & QgsFeatureRequest::ExactIntersect );
  }
  else
  {
    // TODO: implement fast lookup by feature id

    //no filter - use all features
    resetSelection( 1 );
  }
}

void QgsGrassFeatureIterator::setSelectionRect( const QgsRectangle& rect, bool useIntersect )
{
  QgsDebugMsg( QString( "useIntersect = %1 rect = %2" ).arg( useIntersect ).arg( rect.toString() ) );
  //apply selection rectangle
  resetSelection( 0 );

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
      QgsDebugMsg( "Vect_select_lines_by_polygon" );
      int type = mSource->mGrassType;
      if ( mSource->mEditing )
      {
        type = GV_POINTS | GV_LINES;
      }
      QgsDebugMsg( QString( "type = %1" ).arg( type ) );
      //Vect_select_lines_by_box( mSource->map(), &box, mSource->mGrassType, mList );
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
      //Vect_select_lines_by_polygon( mSource->map(), Polygon, 0, NULL, mSource->mGrassType, mList );
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
    if ( mList->value[i] <= mSelectionSize )
    {
      mSelection[mList->value[i]] = 1;
    }
    else
    {
      QgsDebugMsg( "Selected element out of range" );
    }
  }
  QgsDebugMsg( QString( " %1 features selected" ).arg( mList->n_values ) );
}

QgsGrassFeatureIterator::~QgsGrassFeatureIterator()
{
  close();
}

bool QgsGrassFeatureIterator::fetchFeature( QgsFeature& feature )
{
  if ( mClosed )
  {
    return false;
  }

  feature.setValid( false );
  //feature.setGeometry( 0 );
  int cat = -1, type = -1, lid = -1;
  QgsFeatureId featureId = -1;

  QgsDebugMsgLevel( "entered.", 3 );

  /* TODO: handle editing
  if ( P->isEdited() || P->isFrozen() || !P->mValid )
  {
    close();
    return false;
  }
  */

  // TODO: is this necessary? the same is checked below
  if ( !QgsGrassProvider::isTopoType( mSource->mLayerType )  && ( mSource->mCidxFieldIndex < 0 || mNextCidx >= mSource->mCidxFieldNumCats ) )
  {
    close();
    return false; // No features, no features in this layer
  }

  bool filterById = mRequest.filterType() == QgsFeatureRequest::FilterFid;

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
    // TODO real cat when line/cat was rewritten?!
    cat = catFormFid( mRequest.filterFid() );
    QgsDebugMsg( QString( "lid = %1 cat = %2" ).arg( lid ).arg( cat ) );
  }
  else
  {
    // Get next line/area id
    while ( true )
    {
      QgsDebugMsgLevel( QString( "mNextTopoId = %1" ).arg( mNextLid ), 3 );
      if ( mSource->mEditing )
      {
        // TODO should be numLines before editing started (?), but another layer
        // where editing started later mest have different, because its buffer does not have previous changes
        // -> editing of more layers must be synchronized or not allowed
        if ( mNextLid > mSource->mLayer->map()->numLines() )
        {
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

        type = Vect_read_line( mSource->map(), 0, mCats, realLid );
        if ( mCats->n_cats == 0 )
        {
          lid = realLid;
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
              cat = mCats->cat[mNextCidx];
              featureId = makeFeatureId( mNextLid, cat );
              mNextCidx++;
            }
          }
        }
      }
      else if ( mSource->mLayerType == QgsGrassProvider::TOPO_POINT || mSource->mLayerType == QgsGrassProvider::TOPO_LINE )
      {
        if ( mNextLid > Vect_get_num_lines( mSource->map() ) ) break;
        lid = mNextLid;
        type = Vect_read_line( mSource->map(), 0, 0, mNextLid++ );
        if ( !( type & mSource->mGrassType ) ) continue;
        featureId = lid;
      }
      else if ( mSource->mLayerType == QgsGrassProvider::TOPO_NODE )
      {
        if ( mNextLid > Vect_get_num_nodes( mSource->map() ) ) break;
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

        Vect_cidx_get_cat_by_index( mSource->map(), mSource->mCidxFieldIndex, mNextCidx++, &cat, &type, &lid );
        // Warning: selection array is only of type line/area of current layer -> check type first
        if ( !( type & mSource->mGrassType ) )
          continue;

        // The 'id' is a unique id of a GRASS geometry object (point, line, area)
        // but it cannot be used as QgsFeatureId because one geometry object may
        // represent more features because it may have more categories.
        featureId = makeFeatureId( lid, cat );
      }

      if ( filterById && featureId != mRequest.filterFid() )
        continue;

      // it is correct to use id with mSelection because mSelection is only used
      // for geometry selection
      // TODO: fix selection for mEditing
      //if ( !mSource->mEditing && !mSelection[id] )
      if ( !mSelection[lid] )
        continue;

      break;
    }
  }
  if ( lid < 1 || lid > mSource->mLayer->map()->numLines() )
  {
    close();
    return false; // No more features
  }
  QgsDebugMsgLevel( QString( "lid = %1 type = %2 cat = %3 fatureId = %4" ).arg( lid ).arg( type ).arg( cat ).arg( featureId ), 3 );

  feature.setFeatureId( featureId );
  feature.initAttributes( mSource->mFields.count() );
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
      return true;
    }
    else
    {
    }
#endif
    setFeatureGeometry( feature, lid, type );
  }

  if ( mSource->mEditing )
  {
    QgsGrassProvider::TopoSymbol symbol = topoSymbol( lid, type );
    // TODO: set all attributes
    feature.initAttributes( 1 );
    feature.setAttribute( 0, QVariant( symbol ) );
  }
  else if ( ! QgsGrassProvider::isTopoType( mSource->mLayerType ) )
  {
    if ( mRequest.flags() & QgsFeatureRequest::SubsetOfAttributes )
      setFeatureAttributes( cat, &feature, mRequest.subsetOfAttributes() );
    else
      setFeatureAttributes( cat, &feature );
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

      int node1, node2;
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
  QgsDebugMsg( "entered." );
  if ( mClosed )
    return false;

  iteratorClosed();

  // finalization
  Vect_destroy_line_struct( mPoints );
  Vect_destroy_cats_struct( mCats );
  Vect_destroy_list( mList );

  free( mSelection );

  sMutex.unlock();

  mClosed = true;
  return true;
}


//////////////////


void QgsGrassFeatureIterator::resetSelection( bool sel )
{
  QgsDebugMsg( "entered." );
  memset( mSelection, ( int ) sel, mSelectionSize );
  mNextCidx = 0;
  mNextLid = 1;
}


void QgsGrassFeatureIterator::allocateSelection( struct Map_info *map )
{
  int size;
  QgsDebugMsg( "entered." );

  int nlines = Vect_get_num_lines( map );
  int nareas = Vect_get_num_areas( map );

  if ( nlines > nareas )
  {
    size = nlines + 1;
  }
  else
  {
    size = nareas + 1;
  }
  QgsDebugMsg( QString( "nlines = %1 nareas = %2 size = %3" ).arg( nlines ).arg( nareas ).arg( size ) );

  mSelection = ( char * ) malloc( size );
  mSelectionSize = size;
}



void QgsGrassFeatureIterator::setFeatureGeometry( QgsFeature& feature, int id, int type )
{
  unsigned char *wkb;
  int wkbsize;

  // TODO int may be 64 bits (memcpy)
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
    int npoints = mPoints->n_points;

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
      QGis::WkbType wkbType;
      if ( type & GV_LINES )
        wkbType = QGis::WKBLineString;
      else if ( type & GV_POINTS )
        wkbType = QGis::WKBPoint;

      memcpy( wkbp, &wkbType, 4 );
    }
    else
    {
      memcpy( wkbp, &mSource->mQgisType, 4 );
    }
    wkbp += 4;

    /* Number of rings */
    if ( type & GV_FACE )
    {
      int nrings = 1;
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
    memcpy( wkb + offset, &mSource->mQgisType, 4 );
    offset += 4;

    /* Number of rings */
    int nisles = Vect_get_area_num_isles( mSource->map(), id );
    int nrings = 1 + nisles;
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
  return symbol;
}

void QgsGrassFeatureIterator::setFeatureAttributes( int cat, QgsFeature *feature )
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
  return setFeatureAttributes( cat, feature, attlist );
}

void QgsGrassFeatureIterator::setFeatureAttributes( int cat, QgsFeature *feature, const QgsAttributeList& attlist )
{
  QgsDebugMsgLevel( QString( "setFeatureAttributes cat = %1" ).arg( cat ), 3 );
  int nFields =  mSource->mLayer->fields().size();
  if ( nFields > 0 )
  {
    feature->initAttributes( nFields );

    for ( QgsAttributeList::const_iterator iter = attlist.begin(); iter != attlist.end(); ++iter )
    {
      QVariant value = mSource->mLayer->attributes().value( cat ).value( *iter );
      if ( value.type() == QVariant::ByteArray )
      {
        value = QVariant( mSource->mEncoding->toUnicode( value.toByteArray() ) );
      }
      feature->setAttribute( *iter, value );
    }
  }
  else if ( attlist.size() == 1 )
  {
    feature->initAttributes( 1 );
    feature->setAttribute( 0, QVariant( cat ) );
  }
}

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
  /*
    if ( mEditing )
    {
      mFields.clear();
      mFields.append( QgsField( "topo_symbol", QVariant::Int, "int" ) );
    }
  */
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

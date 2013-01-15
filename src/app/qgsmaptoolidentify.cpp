/***************************************************************************
    qgsmaptoolidentify.cpp  -  map tool for identifying features
    ---------------------
    begin                : January 2006
    copyright            : (C) 2006 by Martin Dobias
    email                : wonder.sk at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgscursors.h"
#include "qgsdistancearea.h"
#include "qgsfeature.h"
#include "qgsfield.h"
#include "qgsgeometry.h"
#include "qgslogger.h"
#include "qgsidentifyresults.h"
#include "qgsmapcanvas.h"
#include "qgsmaptopixel.h"
#include "qgsmessageviewer.h"
#include "qgsmaptoolidentify.h"
#include "qgsrasterlayer.h"
#include "qgscoordinatereferencesystem.h"
#include "qgsvectordataprovider.h"
#include "qgsvectorlayer.h"
#include "qgsproject.h"
#include "qgsmaplayerregistry.h"
#include "qgisapp.h"
#include "qgsrendererv2.h"

#include <QSettings>
#include <QMessageBox>
#include <QMouseEvent>
#include <QCursor>
#include <QPixmap>
#include <QStatusBar>
#include <QVariant>

QgsMapToolIdentify::QgsMapToolIdentify( QgsMapCanvas* canvas )
    : QgsMapTool( canvas )
{
  // set cursor
  QPixmap myIdentifyQPixmap = QPixmap(( const char ** ) identify_cursor );
  mCursor = QCursor( myIdentifyQPixmap, 1, 1 );
}

QgsMapToolIdentify::~QgsMapToolIdentify()
{
  if ( mResults )
  {
    mResults->done( 0 );
  }
}

QgsIdentifyResults *QgsMapToolIdentify::results()
{
  if ( !mResults )
  {
    mResults = new QgsIdentifyResults( mCanvas, mCanvas->window() );
    connect( mResults, SIGNAL( formatChanged( QgsRasterLayer * ) ), this, SLOT( formatChanged( QgsRasterLayer * ) ) );
  }

  return mResults;
}

void QgsMapToolIdentify::canvasMoveEvent( QMouseEvent *e )
{
  Q_UNUSED( e );
}

void QgsMapToolIdentify::canvasPressEvent( QMouseEvent *e )
{
  Q_UNUSED( e );
}

void QgsMapToolIdentify::canvasReleaseEvent( QMouseEvent *e )
{
  results()->clear();
  if ( !mCanvas || mCanvas->isDrawing() )
  {
    return;
  }

  mLastPoint = mCanvas->getCoordinateTransform()->toMapCoordinates( e->x(), e->y() );
  mLastExtent = mCanvas->extent();
  mLastMapUnitsPerPixel = mCanvas->mapUnitsPerPixel();
  identify( mLastPoint, mLastExtent, mLastMapUnitsPerPixel );
}

void QgsMapToolIdentify::identify( QgsPoint point, QgsRectangle viewExtent, double mapUnitsPerPixel )
{
  results()->clear();
  if ( !mCanvas || mCanvas->isDrawing() )
  {
    return;
  }

  QSettings settings;
  int identifyMode = settings.value( "/Map/identifyMode", 0 ).toInt();

  bool res = false;

  if ( identifyMode == 0 )
  {
    QgsMapLayer *layer = mCanvas->currentLayer();

    if ( !layer )
    {
      QMessageBox::warning( mCanvas,
                            tr( "No active layer" ),
                            tr( "To identify features, you must choose an active layer by clicking on its name in the legend" ) );
      return;
    }

    QApplication::setOverrideCursor( Qt::WaitCursor );

    res = identifyLayer( layer, point, viewExtent, mapUnitsPerPixel );

    QApplication::restoreOverrideCursor();
  }
  else
  {
    connect( this, SIGNAL( identifyProgress( int, int ) ), QgisApp::instance(), SLOT( showProgress( int, int ) ) );
    connect( this, SIGNAL( identifyMessage( QString ) ), QgisApp::instance(), SLOT( showStatusMessage( QString ) ) );

    QApplication::setOverrideCursor( Qt::WaitCursor );

    QStringList noIdentifyLayerIdList = QgsProject::instance()->readListEntry( "Identify", "/disabledLayers" );

    for ( int i = 0; i < mCanvas->layerCount(); i++ )
    {
      QgsMapLayer *layer = mCanvas->layer( i );

      emit identifyProgress( i, mCanvas->layerCount() );
      emit identifyMessage( tr( "Identifying on %1..." ).arg( layer->name() ) );

      if ( noIdentifyLayerIdList.contains( layer->id() ) )
        continue;

      if ( identifyLayer( layer, point, viewExtent, mapUnitsPerPixel ) )
      {
        res = true;
        if ( identifyMode == 1 )
          break;
      }
    }

    emit identifyProgress( mCanvas->layerCount(), mCanvas->layerCount() );
    emit identifyMessage( tr( "Identifying done." ) );

    disconnect( this, SIGNAL( identifyProgress( int, int ) ), QgisApp::instance(), SLOT( showProgress( int, int ) ) );
    disconnect( this, SIGNAL( identifyMessage( QString ) ), QgisApp::instance(), SLOT( showStatusMessage( QString ) ) );

    QApplication::restoreOverrideCursor();
  }

  if ( res )
  {
    results()->show();
  }
  else
  {
    QSettings mySettings;
    bool myDockFlag = mySettings.value( "/qgis/dockIdentifyResults", false ).toBool();
    if ( !myDockFlag )
    {
      results()->hide();
    }
    else
    {
      results()->clear();
    }
    QgisApp::instance()->statusBar()->showMessage( tr( "No features at this position found." ) );
  }
}

void QgsMapToolIdentify::activate()
{
  results()->activate();
  QgsMapTool::activate();
}

void QgsMapToolIdentify::deactivate()
{
  results()->deactivate();
  QgsMapTool::deactivate();
}

bool QgsMapToolIdentify::identifyLayer( QgsMapLayer *layer, QgsPoint point, QgsRectangle viewExtent, double mapUnitsPerPixel )
{
  bool res = false;

  if ( layer->type() == QgsMapLayer::RasterLayer )
  {
    res = identifyRasterLayer( qobject_cast<QgsRasterLayer *>( layer ), point, viewExtent, mapUnitsPerPixel );
  }
  else
  {
    res = identifyVectorLayer( qobject_cast<QgsVectorLayer *>( layer ), point );
  }

  return res;
}

bool QgsMapToolIdentify::identifyVectorLayer( QgsVectorLayer *layer, QgsPoint point )
{
  if ( !layer )
    return false;

  if ( layer->hasScaleBasedVisibility() &&
       ( layer->minimumScale() > mCanvas->mapRenderer()->scale() ||
         layer->maximumScale() <= mCanvas->mapRenderer()->scale() ) )
  {
    QgsDebugMsg( "Out of scale limits" );
    return false;
  }

  QMap< QString, QString > derivedAttributes;

  derivedAttributes.insert( tr( "(clicked coordinate)" ), point.toString() );

  // load identify radius from settings
  QSettings settings;
  double identifyValue = settings.value( "/Map/identifyRadius", QGis::DEFAULT_IDENTIFY_RADIUS ).toDouble();

  QString ellipsoid = QgsProject::instance()->readEntry( "Measure", "/Ellipsoid", GEO_NONE );

  if ( identifyValue <= 0.0 )
    identifyValue = QGis::DEFAULT_IDENTIFY_RADIUS;

  int featureCount = 0;

  QgsFeatureList featureList;

  // toLayerCoordinates will throw an exception for an 'invalid' point.
  // For example, if you project a world map onto a globe using EPSG 2163
  // and then click somewhere off the globe, an exception will be thrown.
  try
  {
    // create the search rectangle
    double searchRadius = mCanvas->extent().width() * ( identifyValue / 100.0 );

    QgsRectangle r;
    r.setXMinimum( point.x() - searchRadius );
    r.setXMaximum( point.x() + searchRadius );
    r.setYMinimum( point.y() - searchRadius );
    r.setYMaximum( point.y() + searchRadius );

    r = toLayerCoordinates( layer, r );

    layer->select( layer->pendingAllAttributesList(), r, true, true );
    QgsFeature f;
    while ( layer->nextFeature( f ) )
      featureList << QgsFeature( f );
  }
  catch ( QgsCsException & cse )
  {
    Q_UNUSED( cse );
    // catch exception for 'invalid' point and proceed with no features found
    QgsDebugMsg( QString( "Caught CRS exception %1" ).arg( cse.what() ) );
  }

  // init distance/area calculator
  QgsDistanceArea calc;
  if ( !featureList.count() == 0 )
  {
    calc.setEllipsoidalMode( mCanvas->hasCrsTransformEnabled() );
    calc.setEllipsoid( ellipsoid );
    calc.setSourceCrs( layer->crs().srsid() );
  }

  QgsFeatureList::iterator f_it = featureList.begin();

  bool filter = false;

  QgsFeatureRendererV2* renderer = layer->rendererV2();
  if ( renderer && renderer->capabilities() & QgsFeatureRendererV2::ScaleDependent )
  {
    // setup scale for scale dependent visibility (rule based)
    renderer->startRender( *( mCanvas->mapRenderer()->rendererContext() ), layer );
    filter = renderer->capabilities() & QgsFeatureRendererV2::Filter;
  }

  for ( ; f_it != featureList.end(); ++f_it )
  {
    QgsFeatureId fid = f_it->id();

    if ( filter && !renderer->willRenderFeature( *f_it ) )
      continue;

    featureCount++;

    // Calculate derived attributes and insert:
    // measure distance or area depending on geometry type
    if ( layer->geometryType() == QGis::Line )
    {
      double dist = calc.measure( f_it->geometry() );
      QGis::UnitType myDisplayUnits;
      convertMeasurement( calc, dist, myDisplayUnits, false );
      QString str = calc.textUnit( dist, 3, myDisplayUnits, false );  // dist and myDisplayUnits are out params
      derivedAttributes.insert( tr( "Length" ), str );
      if ( f_it->geometry()->wkbType() == QGis::WKBLineString ||
           f_it->geometry()->wkbType() == QGis::WKBLineString25D )
      {
        // Add the start and end points in as derived attributes
        QgsPoint pnt = mCanvas->mapRenderer()->layerToMapCoordinates( layer, f_it->geometry()->asPolyline().first() );
        str = QLocale::system().toString( pnt.x(), 'g', 10 );
        derivedAttributes.insert( tr( "firstX", "attributes get sorted; translation for lastX should be lexically larger than this one" ), str );
        str = QLocale::system().toString( pnt.y(), 'g', 10 );
        derivedAttributes.insert( tr( "firstY" ), str );
        pnt = mCanvas->mapRenderer()->layerToMapCoordinates( layer, f_it->geometry()->asPolyline().last() );
        str = QLocale::system().toString( pnt.x(), 'g', 10 );
        derivedAttributes.insert( tr( "lastX", "attributes get sorted; translation for firstX should be lexically smaller than this one" ), str );
        str = QLocale::system().toString( pnt.y(), 'g', 10 );
        derivedAttributes.insert( tr( "lastY" ), str );
      }
    }
    else if ( layer->geometryType() == QGis::Polygon )
    {
      double area = calc.measure( f_it->geometry() );
      double perimeter = calc.measurePerimeter( f_it->geometry() );
      QGis::UnitType myDisplayUnits;
      convertMeasurement( calc, area, myDisplayUnits, true );  // area and myDisplayUnits are out params
      QString str = calc.textUnit( area, 3, myDisplayUnits, true );
      derivedAttributes.insert( tr( "Area" ), str );
      convertMeasurement( calc, perimeter, myDisplayUnits, false );  // perimeter and myDisplayUnits are out params
      str = calc.textUnit( perimeter, 3, myDisplayUnits, false );
      derivedAttributes.insert( tr( "Perimeter" ), str );
    }
    else if ( layer->geometryType() == QGis::Point &&
              ( f_it->geometry()->wkbType() == QGis::WKBPoint ||
                f_it->geometry()->wkbType() == QGis::WKBPoint25D ) )
    {
      // Include the x and y coordinates of the point as a derived attribute
      QgsPoint pnt = mCanvas->mapRenderer()->layerToMapCoordinates( layer, f_it->geometry()->asPoint() );
      QString str = QLocale::system().toString( pnt.x(), 'g', 10 );
      derivedAttributes.insert( "X", str );
      str = QLocale::system().toString( pnt.y(), 'g', 10 );
      derivedAttributes.insert( "Y", str );
    }

    derivedAttributes.insert( tr( "feature id" ), fid < 0 ? tr( "new feature" ) : FID_TO_STRING( fid ) );

    results()->addFeature( layer, *f_it, derivedAttributes );
  }

  if ( renderer && renderer->capabilities() & QgsFeatureRendererV2::ScaleDependent )
  {
    renderer->stopRender( *( mCanvas->mapRenderer()->rendererContext() ) );
  }

  QgsDebugMsg( "Feature count on identify: " + QString::number( featureCount ) );

  return featureCount > 0;
}

bool QgsMapToolIdentify::identifyRasterLayer( QgsRasterLayer *layer, QgsPoint point, QgsRectangle viewExtent, double mapUnitsPerPixel )
{
  bool res = true;

  if ( !layer ) return false;

  QgsRasterDataProvider *dprovider = layer->dataProvider();
  int capabilities = dprovider->capabilities();
  if ( !dprovider || !( capabilities & QgsRasterDataProvider::Identify ) )
  {
    return false;
  }

  try
  {
    point = toLayerCoordinates( layer, point );
  }
  catch ( QgsCsException &cse )
  {
    Q_UNUSED( cse );
    QgsDebugMsg( QString( "coordinate not reprojectable: %1" ).arg( cse.what() ) );
    return false;
  }
  QgsDebugMsg( QString( "point = %1 %2" ).arg( point.x() ).arg( point.y() ) );

  if ( !layer->extent().contains( point ) ) return false;

  QMap< QString, QString > attributes, derivedAttributes;

  QMap<int, QVariant> values;

  QgsRasterDataProvider::IdentifyFormat format = QgsRasterDataProvider::identifyFormatFromName( layer->customProperty( "identify/format" ).toString() );

  // check if the format is really supported otherwise use first supported format
  if ( !( QgsRasterDataProvider::identifyFormatToCapability( format ) & capabilities ) )
  {
    if ( capabilities & QgsRasterInterface::IdentifyFeature ) format = QgsRasterDataProvider::IdentifyFormatFeature;
    else if ( capabilities & QgsRasterInterface::IdentifyValue ) format = QgsRasterDataProvider::IdentifyFormatValue;
    else if ( capabilities & QgsRasterInterface::IdentifyHtml ) format = QgsRasterDataProvider::IdentifyFormatHtml;
    else if ( capabilities & QgsRasterInterface::IdentifyText ) format = QgsRasterDataProvider::IdentifyFormatText;
    else return false;
  }

  // We can only use context (extent, width, heigh) if layer is not reprojected,
  // otherwise we don't know source resolution (size).
  if ( mCanvas->hasCrsTransformEnabled() && dprovider->crs() != mCanvas->mapRenderer()->destinationCrs() )
  {
    viewExtent = toLayerCoordinates( layer, viewExtent );
    values = dprovider->identify( point, format );
  }
  else
  {
    // It would be nice to use the same extent and size which was used for drawing,
    // so that WCS can use cache from last draw, unfortunately QgsRasterLayer::draw()
    // is doing some tricks with extent and size to allign raster to output which
    // would be difficult to replicate here.
    // Note: cutting the extent may result in slightly different x and y resolutions
    // and thus shifted point calculated back in QGIS WMS (using average resolution)
    //viewExtent = dprovider->extent().intersect( &viewExtent );

    // Width and height are calculated from not projected extent and we hope that
    // are similar to source width and height used to reproject layer for drawing.
    // TODO: may be very dangerous, because it may result in different resolutions
    // in source CRS, and WMS server (QGIS server) calcs wrong coor using average resolution.
    int width = qRound( viewExtent.width() / mapUnitsPerPixel );
    int height = qRound( viewExtent.height() / mapUnitsPerPixel );

    QgsDebugMsg( QString( "viewExtent.width = %1 viewExtent.height = %2" ).arg( viewExtent.width() ).arg( viewExtent.height() ) );
    QgsDebugMsg( QString( "width = %1 height = %2" ).arg( width ).arg( height ) );
    QgsDebugMsg( QString( "xRes = %1 yRes = %2 mapUnitsPerPixel = %3" ).arg( viewExtent.width() / width ).arg( viewExtent.height() / height ).arg( mapUnitsPerPixel ) );

    values = dprovider->identify( point, format, viewExtent, width, height );
  }

  if ( format == QgsRasterDataProvider::IdentifyFormatValue )
  {
    foreach ( int bandNo, values.keys() )
    {
      double value = values.value( bandNo ).toDouble();
      QString valueString;
      if ( dprovider->isNoDataValue( bandNo, value ) )
      {
        valueString = tr( "no data" );
      }
      else
      {
        valueString = QgsRasterBlock::printValue( value );
      }
      attributes.insert( dprovider->generateBandName( bandNo ), valueString );
    }
  }
  else if ( format == QgsRasterDataProvider::IdentifyFormatFeature )
  {
    foreach ( int i, values.keys() )
    {
      attributes.insert( values.value( i ).toString(), "" );
    }
  }
  else // text or html
  {
    foreach ( int bandNo, values.keys() )
    {
      QString value = values.value( bandNo ).toString();
      // TODO: better 'attribute' name, in theory it may be something else than WMS
      // feature info
      if ( format == QgsRasterDataProvider::IdentifyFormatText )
      {
        value = "<pre>" + value + "</pre>";
      }
      attributes.insert( tr( "Feature info" ), value );
    }
  }

  QString type;
  type = tr( "Raster" );

  if ( attributes.size() > 0 )
  {
    derivedAttributes.insert( tr( "(clicked coordinate)" ), point.toString() );
    results()->addFeature( layer, type, attributes, derivedAttributes );
  }

  return res;
}


void QgsMapToolIdentify::convertMeasurement( QgsDistanceArea &calc, double &measure, QGis::UnitType &u, bool isArea )
{
  // Helper for converting between meters and feet
  // The parameter &u is out only...

  // Get the canvas units
  QGis::UnitType myUnits = mCanvas->mapUnits();

  // Get the units for display
  QSettings settings;
  QGis::UnitType displayUnits = QGis::fromLiteral( settings.value( "/qgis/measure/displayunits", QGis::toLiteral( QGis::Meters ) ).toString() );

  calc.convertMeasurement( measure, myUnits, displayUnits, isArea );
  u = myUnits;
}

void QgsMapToolIdentify::formatChanged( QgsRasterLayer *layer )
{
  // TODO?: this is not perfect, it calls identify with last point, but other
  // things could also change
  QgsDebugMsg( "Entered" );
  //identify( mLastPoint );
  identifyRasterLayer( layer, mLastPoint, mLastExtent, mLastMapUnitsPerPixel );
}

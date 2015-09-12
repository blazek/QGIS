/***************************************************************************
    qgsgrasseditrenderer.cpp
                             -------------------
    begin                : February, 2015
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

#include "qgsgrasseditrenderer.h"
#include "qgsgrassprovider.h"

#include "qgssymbolv2.h"
#include "qgssymbollayerv2utils.h"

#include "qgslogger.h"
#include "qgsfeature.h"
//#include "qgsvectorlayer.h"
#include "qgssymbollayerv2.h"
//#include "qgsogcutils.h"
#include "qgscategorizedsymbolrendererv2.h"
#include "symbology-ng/qgscategorizedsymbolrendererv2widget.h"

//#include "qgspointdisplacementrenderer.h"
//#include "qgsinvertedpolygonrenderer.h"

//#include <QDomDocument>
//#include <QDomElement>
#include <QHash>
#include  <QVBoxLayout>

QgsGrassEditRenderer::QgsGrassEditRenderer()
    : QgsFeatureRendererV2( "grassEdit" )
{
//  Q_ASSERT( symbol );
  //QgsSymbolV2 * lineSymbol = QgsSymbolV2::defaultSymbol( QGis::Line );
  //mLineRenderer = new QgsSingleSymbolRendererV2( lineSymbol );

  QHash<int, QColor> colors;
  colors.insert( QgsGrassProvider::TopoUndefined, QColor( 125, 125, 125 ) );
  colors.insert( QgsGrassProvider::TopoLine, QColor( Qt::black ) );
  colors.insert( QgsGrassProvider::TopoBoundary0, QColor( Qt::red ) );
  colors.insert( QgsGrassProvider::TopoBoundary1, QColor( 255, 125, 0 ) );
  colors.insert( QgsGrassProvider::TopoBoundary2, QColor( Qt::green ) );

  QHash<int, QString> labels;
  labels.insert( QgsGrassProvider::TopoUndefined, "Unknown type" );
  labels.insert( QgsGrassProvider::TopoLine, "Line" );
  labels.insert( QgsGrassProvider::TopoBoundary0, "Boundary (isolated)" );
  labels.insert( QgsGrassProvider::TopoBoundary1, "Boundary (area on one side)" );
  labels.insert( QgsGrassProvider::TopoBoundary2, "Boundary (areas on both sides)" );

  QgsCategoryList categoryList;

  foreach ( int value, colors.keys() )
  {
    QgsSymbolV2 * symbol = QgsSymbolV2::defaultSymbol( QGis::Line );
    symbol->setColor( colors.value( value ) );
    categoryList << QgsRendererCategoryV2( QVariant( value ), symbol, labels.value( value ) );
  }

  //categoryList << QgsRendererCategoryV2( QVariant( 0 ), symbol, "TopoLine" );
  mLineRenderer = new QgsCategorizedSymbolRendererV2( "topo_symbol", categoryList );

  colors.clear();
  labels.clear();

  colors.insert( QgsGrassProvider::TopoPoint, QColor( 0, 0, 0 ) );
  colors.insert( QgsGrassProvider::TopoCentroidIn, QColor( 0, 255, 0 ) );
  colors.insert( QgsGrassProvider::TopoCentroidOut, QColor( 255, 0, 0 ) );
  colors.insert( QgsGrassProvider::TopoCentroidDupl, QColor( 255, 0, 255 ) );

  labels.insert( QgsGrassProvider::TopoPoint, "Point" );
  labels.insert( QgsGrassProvider::TopoCentroidIn, "Centroid in area" );
  labels.insert( QgsGrassProvider::TopoCentroidOut, "Centroid outside area" );
  labels.insert( QgsGrassProvider::TopoCentroidDupl, "Duplicate centroid" );

  categoryList.clear();

  foreach ( int value, colors.keys() )
  {
    QgsSymbolV2 * symbol = QgsSymbolV2::defaultSymbol( QGis::Point );
    symbol->setColor( colors.value( value ) );
    categoryList << QgsRendererCategoryV2( QVariant( value ), symbol, labels.value( value ) );
  }

  //categoryList << QgsRendererCategoryV2( QVariant( 0 ), symbol, "TopoLine" );
  mPointRenderer = new QgsCategorizedSymbolRendererV2( "topo_symbol", categoryList );
}

QgsGrassEditRenderer::~QgsGrassEditRenderer()
{
}

QgsSymbolV2* QgsGrassEditRenderer::symbolForFeature( QgsFeature& feature, QgsRenderContext& context )
{
  //QgsDebugMsg( QString("fid = %1 topo_symbol = %2").arg(feature.id()).arg( feature.attribute(0).toInt() ) );

  //foreach( QgsRendererCategoryV2 category, mLineRenderer->categories() )
  //{
  //  QgsDebugMsg( "category: " + category.dump() );
  //}

  QgsSymbolV2* symbol = mLineRenderer->symbolForFeature( feature, context );

  if ( !symbol )
  {
    symbol = mPointRenderer->symbolForFeature( feature, context );
  }
  /*
    if ( symbol )
    {
      QgsDebugMsg( "color = " + symbol->color().name() );
    }
    else
    {
      QgsDebugMsg( "no symbol");
    }
  */
  return symbol;
}

void QgsGrassEditRenderer::startRender( QgsRenderContext& context, const QgsFields& fields )
{
  Q_UNUSED( fields );
  // TODO better
  QgsFields topoFields;
  topoFields.append( QgsField( "topo_symbol", QVariant::Int, "int" ) );
  mLineRenderer->startRender( context, topoFields );
  mPointRenderer->startRender( context, topoFields );
}

void QgsGrassEditRenderer::stopRender( QgsRenderContext& context )
{
  mLineRenderer->stopRender( context );
  mPointRenderer->stopRender( context );
}

QList<QString> QgsGrassEditRenderer::usedAttributes()
{
  return mLineRenderer->usedAttributes();
}


/*
QgsSymbolV2* QgsGrassEditRenderer::symbol() const
{
  return mLineRenderer->symbol();
}

void QgsGrassEditRenderer::setSymbol( QgsSymbolV2* s )
{
  Q_UNUSED( s );
}
*/

QgsFeatureRendererV2* QgsGrassEditRenderer::clone() const
{
  QgsGrassEditRenderer* r = new QgsGrassEditRenderer();
  r->mLineRenderer = dynamic_cast<QgsCategorizedSymbolRendererV2*>( mLineRenderer->clone() );
  r->mPointRenderer = dynamic_cast<QgsCategorizedSymbolRendererV2*>( mPointRenderer->clone() );
  return r;
}

QgsSymbolV2List QgsGrassEditRenderer::symbols( QgsRenderContext& context )
{
  return mLineRenderer->symbols( context );
}

QString QgsGrassEditRenderer::dump() const
{
  return "GRASS edit renderer";
}

//------------------------------------------------------------------------------------------------

QgsRendererV2Widget* QgsGrassEditRendererWidget::create( QgsVectorLayer* layer, QgsStyleV2* style, QgsFeatureRendererV2* renderer )
{
  return new QgsGrassEditRendererWidget( layer, style, renderer );
}

QgsGrassEditRendererWidget::QgsGrassEditRendererWidget( QgsVectorLayer* layer, QgsStyleV2* style, QgsFeatureRendererV2* renderer )
    : QgsRendererV2Widget( layer, style )
    , mRenderer( 0 )
{
  // try to recognize the previous renderer
  // (null renderer means "no previous renderer")
  //mRenderer = new QgsCategorizedSymbolRendererV2( "", QgsCategoryList() );
  mRenderer = dynamic_cast<QgsGrassEditRenderer*>( renderer->clone() );
  //setupUi( this );
  if ( !mRenderer )
    return;

  QVBoxLayout* layout = new QVBoxLayout( this );

  mLineRendererWidget = QgsCategorizedSymbolRendererV2Widget::create( layer, style, mRenderer->lineRenderer() );
  layout->addWidget( mLineRendererWidget );

  mPointRendererWidget = QgsCategorizedSymbolRendererV2Widget::create( layer, style, mRenderer->pointRenderer() );
  layout->addWidget( mPointRendererWidget );
}
QgsGrassEditRendererWidget::~QgsGrassEditRendererWidget()
{
  delete mRenderer;
}
QgsFeatureRendererV2* QgsGrassEditRendererWidget::renderer()
{
  mRenderer->setLineRenderer( dynamic_cast<QgsCategorizedSymbolRendererV2*>( mLineRendererWidget->renderer() ) );
  mRenderer->setPointRenderer( dynamic_cast<QgsCategorizedSymbolRendererV2*>( mPointRendererWidget->renderer() ) );
  return mRenderer;
}




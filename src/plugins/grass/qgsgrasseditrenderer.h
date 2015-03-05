/***************************************************************************
    qgsgrasseditrenderer.h  
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
#ifndef QGSGRASSEDITRENDERER_H
#define QGSGRASSEDITRENDERER_H

#include "qgis.h"
#include "qgsrendererv2.h"
//#include "qgssinglesymbolrendererv2.h"
#include "qgscategorizedsymbolrendererv2.h"
#include "qgssymbolv2.h"
//class QgsSingleSymbolRendererV2;

#include "qgscategorizedsymbolrendererv2.h"
#include "symbology-ng/qgsrendererv2widget.h"

class QgsGrassEditRenderer : public QgsFeatureRendererV2
{
  public:
    enum TopoSymbol
    {
      TopoPoint,
      TopoLine,
      TopoBoundary0,
      TopoBoundary1,
      TopoBoundary2,
      TopoCentroidIn,
      TopoCentroidOut,
      TopoCentroidDupl,
      TopoNode0,
      TopoNode1,
      TopoNode2
    };

    QgsGrassEditRenderer( );

    virtual ~QgsGrassEditRenderer();

    virtual QgsSymbolV2* symbolForFeature( QgsFeature& feature ) override;

    //virtual QgsSymbolV2* originalSymbolForFeature( QgsFeature& feature ) override;

    virtual void startRender( QgsRenderContext& context, const QgsFields& fields ) override;

    virtual void stopRender( QgsRenderContext& context ) override;

    virtual QList<QString> usedAttributes() override;

    virtual QgsFeatureRendererV2* clone() const override;

    //virtual int capabilities() override { return SymbolLevels | RotationField; }

    virtual QgsSymbolV2List symbols() override;

    virtual QString dump() const override;

    QgsCategorizedSymbolRendererV2 *lineRenderer() const { return mLineRenderer; }
    QgsCategorizedSymbolRendererV2 *pointRenderer() const { return mPointRenderer; }

    void setLineRenderer( QgsCategorizedSymbolRendererV2 *renderer ) { mLineRenderer = renderer; }
    void setPointRenderer( QgsCategorizedSymbolRendererV2 *renderer ) { mPointRenderer = renderer; }

  protected:
    //QScopedPointer<QgsSymbolV2> mSymboLine;
    
    //QgsSingleSymbolRendererV2 *mLineRenderer;
    QgsCategorizedSymbolRendererV2 *mLineRenderer;
    QgsCategorizedSymbolRendererV2 *mPointRenderer;
};

class GUI_EXPORT QgsGrassEditRendererWidget : public QgsRendererV2Widget
{
    Q_OBJECT
  public:
    static QgsRendererV2Widget* create( QgsVectorLayer* layer, QgsStyleV2* style, QgsFeatureRendererV2* renderer );

    QgsGrassEditRendererWidget ( QgsVectorLayer* layer, QgsStyleV2* style, QgsFeatureRendererV2* renderer );
    ~QgsGrassEditRendererWidget();

    virtual QgsFeatureRendererV2* renderer() override;

  protected:
    QgsGrassEditRenderer* mRenderer;

    QgsRendererV2Widget* mLineRendererWidget;
    QgsRendererV2Widget* mPointRendererWidget;
};

#endif // QGSGRASSEDITRENDERER_H

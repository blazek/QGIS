/***************************************************************************
    qgsrasterkernel.h - Raster kernel
     --------------------------------------
    Date                 : Jun 2012
    Copyright            : (C) 2012 by Radim Blazek
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
#ifndef QGSRASTERKERNEL_H
#define QGSRASTERKERNEL_H

#include <QVector>
#include <QList>

#include "qgsrectangle.h"
#include "qgscoordinatereferencesystem.h"
#include "qgscoordinatetransform.h"
#include "qgsrasterface.h"

#include <cmath>

class QgsRasterKernel : public QgsRasterFace
{
  public:
    QgsRasterKernel ( QgsRasterFace* input );

    /** \brief The destructor */
    ~QgsRasterKernel();

    void * readBlock( int bandNo, QgsRectangle  const & extent, int width, int height );

  private:
    /** Window size */
    int mWinSize;
};

#endif


/***************************************************************************
    qgsrasterkernel.cpp - Raster kernel
     --------------------------------------
    Date                 : Jan 16, 2011
    Copyright            : (C) 2005 by Radim Blazek
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

#include "qgslogger.h"
#include "qgsrasterkernel.h"
#include "qgscoordinatetransform.h"

QgsRasterKernel::QgsRasterKernel( QgsRasterFace* input ):
  QgsRasterFace( input ), mWinSize (5)
{
  QgsDebugMsg( "Entered" );
}

QgsRasterKernel::~QgsRasterKernel()
{
}

void * QgsRasterKernel::readBlock( int bandNo, QgsRectangle  const & extent, int width, int height )
{
  QgsDebugMsg( QString( "bandNo = %1 mWinSize = %2" ).arg(bandNo).arg( mWinSize ) );
  if ( !mInput ) return 0;

  // TODO: expand extent by half win size 
  void * inputData = mInput->readBlock( bandNo, extent, width, height );
  if ( !inputData ) return 0;

  int typeSize = mInput->dataTypeSize( bandNo );
  void * outputData = malloc( width * height * typeSize );

  QgsRasterFace::DataType rasterType = ( QgsRasterFace::DataType )mInput->dataType( bandNo );
  QgsDebugMsg( QString( "rasterType = %1" ).arg( rasterType ) );
  
  int half = floor ( mWinSize / 2 );
          
  double pi = 3.141592653589793;
  int dimension = 2;
  double bandwidth = 1.* mWinSize / half / 2; // TODO
  double term =  1. / (pow(bandwidth, dimension) * pow((2. * pi), dimension / 2.));

  for ( int row = half; row < height - half; row++ )
  {
    for ( int col = half; col < width - half; col++ )
    {
      int index = row * width + col;

      double val;
    
      double count = 0;
      double kernel = 0;
      // TODO everything
      for ( int r = row-half; r < row+half; r++ )
      {
        for ( int c = col-half; c < col+half; c++ )
        {
          int i = r * width + c;

          val = readValue( inputData, rasterType, i );
          //QgsDebugMsg( QString( "row = %1 col = %2 val = %3").arg(row).arg(col).arg(val) );
          double x = sqrt ( pow(row-r,2) + pow(col-c,2) );

          double k;

          // TODO Gaussian 
          //x /= bandwidth;
          //double k = (term * exp(-(x * x) / 2.));
          //QgsDebugMsg( QString( "x= %1 k = %2 term = %3").arg(x).arg( k ).arg(term) );

          // triangular
          if ( x > bandwidth ) {
             k = 0;
          } else {
            k = 1/bandwidth;
            x /= bandwidth;
            k = k * ( 1 - x );
          }

          kernel += val * k;
          count += k;
        }
      }
    
      // TODO: devide by what? In theory sum of whole map should give 1
      kernel /= count;
      //QgsDebugMsg( QString( "kernel = %1").arg( kernel ) );

      writeValue( outputData, rasterType, index, kernel );
    }
  }
  free( inputData );
  return outputData;
}

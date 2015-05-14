/***************************************************************************
    qgsgrassimport.cpp  -  Import to GRASS mapset
                             -------------------
    begin                : May, 2015
    copyright            : (C) 2015 Radim Blazek
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

#include <QByteArray>
#include <QtConcurrentRun>

#include "qgsproviderregistry.h"
#include "qgsrasterdataprovider.h"
#include "qgsrasteriterator.h"

#include "qgsgrassimport.h"

extern "C"
{
#include <grass/version.h>
#include <grass/gis.h>
#include <grass/raster.h>
}

QgsGrassImport::QgsGrassImport(QgsGrassObject grassObject, const QString & providerKey, const QString & uri)
  : QObject()
  , mGrassObject(grassObject)
  , mProviderKey(providerKey)
  , mUri(uri)
{
}

void QgsGrassImport::setError(QString error)
{
  QgsDebugMsg( "error: " + error );
  mError = error;
}

QString QgsGrassImport::error()
{
  return mError;
}

//------------------------------ QgsGrassRasterImport ------------------------------------
//QgsGrassRasterImport::QgsGrassRasterImport(QgsGrassObject grassObject, QgsRasterLayer* layer)
QgsGrassRasterImport::QgsGrassRasterImport(const QgsGrassObject& grassObject, const QString & providerKey, const QString & uri )
  : QgsGrassImport(grassObject, providerKey, uri)
  , mFutureWatcher(0)
{
}

QgsGrassRasterImport::~QgsGrassRasterImport()
{
  //delete mLayer;
  if ( mFutureWatcher && !mFutureWatcher->isFinished() )
  {
    QgsDebugMsg( "mFutureWatcher not finished -> waitForFinished()" );
    mFutureWatcher->waitForFinished();
  }
}

void QgsGrassRasterImport::start()
{
  QgsDebugMsg( "entered" );
  mFutureWatcher = new QFutureWatcher<bool>( this );
  connect( mFutureWatcher, SIGNAL( finished() ), SLOT( onFinished() ) );
  mFutureWatcher->setFuture( QtConcurrent::run( run, this ) );
}

bool QgsGrassRasterImport::run(QgsGrassRasterImport *imp)
{
  QgsDebugMsg( "entered" );
  imp->import();
  return true;
}

bool QgsGrassRasterImport::import()
{
  QgsDebugMsg( "entered" );
  QgsRasterDataProvider* provider = qobject_cast<QgsRasterDataProvider*>( QgsProviderRegistry::instance()->provider( mProviderKey, mUri ) );
  if ( !provider )
  {
    setError( "cannot create provider" );
    return false;
  }
  if ( !provider->isValid() )
  {
    setError( "provider is not valid" );
    delete provider;
    return false;
  }

  // TODO: size / extent dialog
  if ( !(provider->capabilities() & QgsRasterInterface::Size) || provider->xSize() == 0 || provider->ySize() == 0 ) {
    setError( "unknown data source size" );
    delete provider;
    return false;
  }

  QgsDebugMsg( "extent = " + provider->extent().toString() );

  for ( int band = 1; band <= provider->bandCount(); band++ )
  {
    QgsDebugMsg( QString("band = %1").arg(band));
    QGis::DataType qgis_out_type = QGis::UnknownDataType;
    RASTER_MAP_TYPE data_type = -1;
    switch ( provider->dataType(band) )
    {
      case QGis::Byte:
      case QGis::UInt16:
      case QGis::Int16:
      case QGis::UInt32:
      case QGis::Int32:
        qgis_out_type = QGis::Int32;
        break;
      case QGis::Float32:
        qgis_out_type = QGis::Float32;
        break;
      case QGis::Float64:
        qgis_out_type = QGis::Float64;
        break;
      case QGis::ARGB32:
      case QGis::ARGB32_Premultiplied:
        qgis_out_type = QGis::Int32;  // split to multiple bands?
        break;
      case QGis::CInt16:
      case QGis::CInt32:
      case QGis::CFloat32:
      case QGis::CFloat64:
      case QGis::UnknownDataType:
        setError( tr("Data type %1 not supported").arg(provider->dataType(band)) );
        delete provider;
        return false;
    }
    //qgis_out_type = QGis::Int32; // debug

    QgsDebugMsg( QString("data_type = %1").arg(data_type));

    QString module = QgsGrass::qgisGrassModulePath() + "/qgis.r.in";
    QStringList arguments;
    QString name = mGrassObject.name();
    if (  provider->bandCount() > 1 )
    {
      name += QString("_%1").arg( band );
    }
    arguments.append( "output=" + name );
    QTemporaryFile gisrcFile;
    QProcess* process = 0;
    try
    {
      process = QgsGrass::startModule( mGrassObject.gisdbase(), mGrassObject.location(), mGrassObject.mapset(), module, arguments, gisrcFile );
    }
    catch ( QgsGrass::Exception &e )
    {
      setError( e.what() );
      delete provider;
      return false;
    }

    QDataStream outStream(process);

    outStream << provider->extent() << (qint32)provider->xSize() << (qint32)provider->ySize();
    outStream << (qint32)qgis_out_type;

    // calculate reasonable block size (5MB)
    int maximumTileHeight = 5000000/provider->xSize();
    maximumTileHeight = std::max(1,maximumTileHeight);

    QgsRasterIterator iter( provider );
    iter.setMaximumTileWidth( provider->xSize() );
    iter.setMaximumTileHeight( maximumTileHeight );

    iter.startRasterRead( band, provider->xSize(), provider->ySize(), provider->extent() );

    int iterLeft = 0;
    int iterTop = 0;
    int iterCols = 0;
    int iterRows = 0;
    QgsRasterBlock* block = 0;
    while ( iter.readNextRasterPart( band, iterCols, iterRows, &block, iterLeft, iterTop ) )
    {
      for ( int row = 0; row < iterRows; row++ )
      {
        if ( !block->convert(qgis_out_type) ) {
          setError( "cannot vonvert data type" );
          delete block;
          delete provider;
          return false;
        }
        char * data = block->bits( row, 0 );
        int size = iterCols * block->dataTypeSize();
        QByteArray byteArray = QByteArray::fromRawData(data, size); // does not copy data and does not take ownership
        outStream << byteArray;
      }
      delete block;
    }

    process->closeWriteChannel();
    process->waitForFinished(5000);

    //process->waitForReadyRead();
    //QString str = process->readLine().trimmed();
    //QgsDebugMsg( "read from stdout : " + str );

    QString stdoutString = process->readAllStandardOutput().data();
    QString stderrString = process->readAllStandardError().data();

    QString processResult = QString( "exitStatus=%1, exitCode=%2, errorCode=%3, error=%4 stdout=%5, stderr=%6" )
                            .arg( process->exitStatus() ).arg( process->exitCode() )
                            .arg( process->error() ).arg( process->errorString() )
                            .arg( stdoutString ).arg( stderrString );
    QgsDebugMsg( "processResult: " + processResult );

    if ( process->exitStatus() != QProcess::NormalExit )
    {
      setError( process->errorString() );
      delete provider;
      delete process;
      return false;
    }

    if ( process->exitCode() != 0 )
    {
      setError( stderrString );
      delete provider;
      delete process;
      return false;
    }

    delete process;
  }
  delete provider;
  return true;
}

void QgsGrassRasterImport::onFinished()
{
  QgsDebugMsg( "entered" );
  emit finished(this);
}

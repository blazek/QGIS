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

#include <QtConcurrentRun>

#include "qgsproviderregistry.h"
#include "qgsrasterdataprovider.h"

#include "qgsgrassimport.h"

extern "C"
{
#include <grass/version.h>
#include <grass/gis.h>
#include <grass/raster.h>
}

QgsGrassImport::QgsGrassImport(QgsGrassObject grassObject)
  : QObject()
  , mGrassObject(grassObject)
{
}

//------------------------------ QgsGrassRasterImport ------------------------------------
//QgsGrassRasterImport::QgsGrassRasterImport(QgsGrassObject grassObject, QgsRasterLayer* layer)
QgsGrassRasterImport::QgsGrassRasterImport(const QgsGrassObject& grassObject, const QString & providerKey, const QString & uri )
  : QgsGrassImport(grassObject)
  //, mProvider(0)
  , mProviderKey(providerKey)
  , mUri(uri)
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

void QgsGrassRasterImport::setError(QString error)
{
  QgsDebugMsg( "error: " + error );
  mError = error;
}

QString QgsGrassRasterImport::error()
{
  return mError;
}

void QgsGrassRasterImport::start()
{
  QgsDebugMsg( "entered" );
  mFutureWatcher = new QFutureWatcher<bool>( this );
  connect( mFutureWatcher, SIGNAL( finished() ), SLOT( finished() ) );
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

  QgsDebugMsg( "extent = " + provider->extent().toString() );

  for ( int band = 1; band <= provider->bandCount(); band++ )
  {
    QgsDebugMsg( QString("band = %1").arg(band));
    RASTER_MAP_TYPE data_type = -1;
    switch ( provider->dataType(band) )
    {
      case QGis::Byte:
      case QGis::UInt16:
      case QGis::Int16:
      case QGis::UInt32:
      case QGis::Int32:
        data_type = CELL_TYPE;
        break;
      case QGis::Float32:
        data_type = FCELL_TYPE;
        break;
      case QGis::Float64:
        data_type = DCELL_TYPE;
        break;
      case QGis::ARGB32:
      case QGis::ARGB32_Premultiplied:
        data_type = CELL_TYPE; // split to multiple bands?
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
    outStream << (qint32)data_type;

    //process->waitForReadyRead();
    //QString str = process->readLine().trimmed();
    //QgsDebugMsg( "read from stdout : " + str );

    process->closeWriteChannel();
    process->waitForFinished(5000);

    if ( process->exitCode() != 0 )
    {
      setError( process->readAllStandardError().data() );
      delete provider;
      delete process;
      return false;
    }
    delete process;
  }
  delete provider;
  return true;
}

void QgsGrassRasterImport::finished()
{
  QgsDebugMsg( "entered" );
}

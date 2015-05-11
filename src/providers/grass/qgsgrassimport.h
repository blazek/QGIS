/***************************************************************************
    qgsgrassimport.h  -  Import to GRASS mapset
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
#ifndef QGSGRASSIMPORT_H
#define QGSGRASSIMPORT_H

#include <QFutureWatcher>
#include <QObject>

#include "qgslogger.h"
//#include "qgsrasterlayer.h"

#include "qgsgrass.h"

class QgsGrassImport : public QObject
{
    Q_OBJECT
  public:
    QgsGrassImport(QgsGrassObject grassObject);
    virtual ~QgsGrassImport() {}
    QgsGrassObject grassObject() { return mGrassObject; }
  protected:
    QgsGrassObject mGrassObject;
};

class QgsGrassRasterImport : public QgsGrassImport
{
    Q_OBJECT
  public:
    // QgsGrassRasterImport takes layer ownership
    //QgsGrassRasterImport(QgsGrassObject grassObject, QgsRasterLayer* layer);
    QgsGrassRasterImport(const QgsGrassObject& grassObject, const QString & providerKey, const QString & uri );
    ~QgsGrassRasterImport();
    bool import();
    // start import in thread
    void start();
    // get error if import failed
    QString error();
  public slots:
    void finished();
  private:
    static bool run(QgsGrassRasterImport *imp);
    void setError(QString error);
    //QgsRasterLayer* mLayer;
    QString mProviderKey;
    QString mUri;
    //QgsRasterDataProvider* mProvider;
    QFutureWatcher<bool>* mFutureWatcher;
    QString mError;
};

#endif // QGSGRASSIMPORT_H

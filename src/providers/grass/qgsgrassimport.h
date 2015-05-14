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
    QgsGrassImport(QgsGrassObject grassObject, const QString & providerKey, const QString & uri );
    virtual ~QgsGrassImport() {}
    QgsGrassObject grassObject() const { return mGrassObject; }
    QString uri() const { return mUri; }
    // get error if import failed
    QString error();

  signals:
    // sent when process finished
    void finished(QgsGrassImport *import);

  protected:
    void setError(QString error);
    QgsGrassObject mGrassObject;
    QString mProviderKey;
    QString mUri;
    QString mError;
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
  public slots:
    void onFinished();
  //signals:
    //void finished(QgsGrassImport *import) override;
  private:
    static bool run(QgsGrassRasterImport *imp);

    //QgsRasterLayer* mLayer;
    //QgsRasterDataProvider* mProvider;
    QFutureWatcher<bool>* mFutureWatcher;

};

#endif // QGSGRASSIMPORT_H

/***************************************************************************
     qgsfeaturestore.h
     --------------------------------------
    Date                 : February 2013
    Copyright            : (C) 2013 by Radim Blazek
    Email                : radim.blazek@gmail.com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGSFEATURESTORE_H
#define QGSFEATURESTORE_H

#include "qgis.h"
#include "qgsfeature.h"
#include "qgsfield.h"
#include "qgslogger.h"
#include "qgscoordinatereferencesystem.h"
#include <QObject>
#include <QList>
#include <QMetaType>

/** \ingroup core
 * Container for features with the same fields and crs.
 */
class CORE_EXPORT QgsFeatureStore
{
  public:
    //! Constructor
    QgsFeatureStore();

    //! Constructor
    QgsFeatureStore( const QgsFeatureStore &rhs );

    //! Constructor
    QgsFeatureStore( const QgsFields& fields, const QgsCoordinateReferenceSystem& crs );

    //! Destructor
    ~QgsFeatureStore();

    /** Get fields list */
    QgsFields& fields() { return mFields; }

    /** Set fields */
    void setFields( const QgsFields & fields ) { mFields = fields; }

    /** Get crs */
    QgsCoordinateReferenceSystem crs() const { return mCrs; }

    /** Set crs */
    void setCrs( const QgsCoordinateReferenceSystem& crs ) { mCrs = crs; }

    /** Get features list reference */
    QgsFeatureList& features() { return mFeatures; } 

  private:
    QgsFields mFields;    

    QgsCoordinateReferenceSystem mCrs;
  
    QgsFeatureList mFeatures;
};

Q_DECLARE_METATYPE( QgsFeatureStore );

#endif

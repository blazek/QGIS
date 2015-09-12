/***************************************************************************
                            qgsgrassvectormap.cpp
                             -------------------
    begin                : September, 2015
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

#ifndef QGSGRASSVECTORMAP_H
#define QGSGRASSVECTORMAP_H

#include <QDateTime>

#include "qgsgrass.h"
#include "qgsgrassvectormaplayer.h"

class QgsGrassVectorMap
{
  public:
    QgsGrassVectorMap( const QgsGrassObject & grassObject );
    ~QgsGrassVectorMap();

    QgsGrassObject grassObject() const { return mGrassObject; }
    struct Map_info *map() { return mMap; }
    bool isValid() const { return mValid; }
    bool isFrozen() const { return mFrozen; }
    bool isUpdate() const { return mUpdate; }
    int version() const { return mVersion; }
    //int numLines() const { return mNumLines; }
    // number of instances using this map
    int userCount() const;
    /** Get current number of lines.
     *   @return number of lines */
    int numLines();

    QHash<int, int> oldLids() const { return mEditOldLids; }
    QHash<int, int> newLids() const { return mEditNewLids; }
    QHash<int, QgsGeometry> & oldGeometries() { return mOldGeometries; }

    /** Open GRASS map */
    bool open();

    /** Close GRASS map */
    void close();

    /** Open map.
     *  @param grassObject
     *  @return pointer to map or 0 */
    static QgsGrassVectorMap * openMap( const QgsGrassObject & grassObject );

    bool startEdit();
    bool closeEdit( bool newMap );

    /** Get layer, layer is created and loaded if not yet.
     *  @param field
     *  @return pointer to layer or 0 if layer doe not exist */
    QgsGrassVectorMapLayer * openLayer( int field );

    /** Open layer.
     *  @param grassObject
     *  @param field
     *  @return pointer to layer or 0 */
    static QgsGrassVectorMapLayer * openLayer( const QgsGrassObject & grassObject, int field );

    /** Close layer and release cached data if there are no more users and close map
     *  if there are no more map users.
     *  @param layer */
    static void closeLayer( QgsGrassVectorMapLayer * layer );

    /** Update map. Close and reopen vector and refresh layers.
     *  Instances of QgsGrassProvider are not updated and should call update() method */
    void update();

    /** The map is outdated. The map was for example rewritten by GRASS module outside QGIS.
     *  This function checks internal timestamp stored in QGIS.
     */
    bool mapOutdated();

    /** The attributes are outdated. The table was for example updated by GRASS module outside QGIS.
     *  This function checks internal timestamp stored in QGIS.
     */
    bool attributesOutdated();

  private:
    QgsGrassObject mGrassObject;
    // true if map is open, once the map is closed, valid is set to false and no more used
    bool mValid;
    // Vector temporally disabled. Necessary for GRASS Tools on Windows
    bool mFrozen;
    // true if the map is opened in update mode
    bool mUpdate;
    // number layers using this map
    //int  mUsers;
    // version, increased by each closeEdit() and updateMap()
    int mVersion;
    // last modified time of the vector directory, when the map was opened
    QDateTime mLastModified;
    // last modified time of the vector 'dbln' file, when the map was opened
    // or attributes were updated. The 'dbln' file is updated by v.to.db etc.
    QDateTime mLastAttributesModified;
    // when attributes are changed
    // map header
    struct  Map_info *mMap;
    // Vector layers
    QList<QgsGrassVectorMapLayer*> mLayers;
    // Number of lines in vector before editing started
    //int mNumLines;
    // Original line ids of rewritten GRASS lines (new lid -> old lid)
    QHash<int, int> mEditOldLids;
    // Current line ids for old line ids (old lid -> new lid)
    QHash<int, int> mEditNewLids;
    // hash of rewritten (deleted) features
    //QHash<int,QgsFeature> mChangedFeatures;
    // Hash of original lines' geometries of lines which were changed, keys are GRASS lid
    QHash<int, QgsGeometry> mOldGeometries;
    /** Open vector maps */
    static QList<QgsGrassVectorMap*> mMaps;
};

#endif // QGSGRASSVECTORMAP_H

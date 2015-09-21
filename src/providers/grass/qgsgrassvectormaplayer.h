/***************************************************************************
                          qgsgrassvectormaplayer.h
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

#ifndef QGSGRASSVECTORMAPLAYER_H
#define QGSGRASSVECTORMAPLAYER_H

#include <QDateTime>
#include <QMap>
#include <QObject>
#include <QPair>

#include "qgsfield.h"

class QgsGrassVectorMap;

class GRASS_LIB_EXPORT QgsGrassVectorMapLayer : public QObject
{
    Q_OBJECT
  public:
    QgsGrassVectorMapLayer( QgsGrassVectorMap *map, int field );

    int field() const { return mField; }
    bool isValid() const { return mValid; }
    QgsGrassVectorMap *map() { return mMap; }

    /** Current fields, in sync with vector layer fields.
     * When the map is in editing mode, fields() contain topo symbol column,
     * added/deleted columns in editing are reflected in fields() */
    QgsFields & fields() { return mFields; }

    static QStringList fieldNames( QgsFields & fields );

    QMap<int, QList<QVariant> > & attributes() { return mAttributes; }

    /** Get attribute for index corresponding to current fields(),
     * if there is no table, returns cat */
    QVariant attribute( int cat, int index );

    bool hasTable() { return mHasTable; }
    int keyColumn() { return mKeyColumn; }
    QList< QPair<double, double> > minMax() { return mMinMax; }
    int userCount() { return mUsers; }
    void addUser();
    void removeUser();

    /** Load attributes from the map. Old sources are released. */
    void load();

    /** Clear all cached data */
    void clear();

    /** Decrease number of users and clear if no more users */
    void close();

    void startEdit();

    //------------------------------- Database utils ---------------------------------
    void setMapset();

    /** Execute SQL statement
     *   @param sql
     *   @return empty string or error message
     */
    void executeSql( const QString &sql, QString &error );

    /** Update attributes
     *   @param field
     *   @param cat
     *   @param update comma separated update string, e.g.: col1 = 5, col2 = 'Val d''Aosta'
     *   @return empty string or error messagemLayer
     */
    void updateAttributes( int cat, const QString &values, QString &error );

    /** Insert new attributes to the table (it does not check if attributes already exists)
     *   @param cat
     *   @return empty string or error message
     */
    void insertAttributes( int cat, QString &error );

    /** Delete attributes from the table
     *   @param cat
     *   @return empty string or error message
     */
    void deleteAttribute( int cat, QString &error );

    /** Check if a database row exists and it is orphan (no more lines with
     *  that category)
     *   @param cat
     *   @param orphan set to true if a record exits and it is orphan
     *   @return empty string or error message
     */
    void isOrphan( int cat, int &orphan, QString &error );

    /** Create table and link vector to this table
     *   @param columns SQL definition for columns, e.g. cat integer, label varchar(10)
     *   @return empty string or error message
     */
    void createTable( const QString &key, const QString &columns, QString &error );

    /** Add column to table
     *   @param field
     */
    void addColumn( const QgsField &field, QString &error );

    void deleteColumn( const QgsField &field, QString &error );

  private:
    // update current fields
    void updateFields();

    int mField;
    bool mValid;
    QgsGrassVectorMap *mMap;
    struct field_info *mFieldInfo;
    bool mHasTable;
    // index of key column
    int mKeyColumn;

    // table fields, updated if a field is added/deleted, if there is no table, it contains
    // cat field
    QgsFields mTableFields;

    // current fields
    QgsFields mFields;

    // list of fields in mAttributes, these fields may only grow when a field is added,
    // but do not shrink until editing is closed
    QgsFields mAttributeFields;

    // Map of attributes with cat as key
    QMap<int, QList<QVariant> > mAttributes;

    // Map of current fields() indexes to mAttributes
    QMap<int, int> mAttributeIndexes;

    // minimum and maximum values of attributes
    QList<QPair<double, double> > mMinMax;
    // timestamp when attributes were loaded
    QDateTime mLastLoaded;
    // number of instances using this layer
    int mUsers;
};

#endif // QGSGRASSVECTORMAPLAYER_H

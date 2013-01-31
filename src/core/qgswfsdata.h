/***************************************************************************
     qgswfsdata.h
     --------------------------------------
    Date                 : Sun Sep 16 12:19:55 AKDT 2007
    Copyright            : (C) 2007 by Gary E. Sherman
    Email                : sherman at mrcc dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#ifndef QGSWFSDATA_H
#define QGSWFSDATA_H

#include <expat.h>
#include "qgis.h"
#include "qgsapplication.h"
#include "qgsdataprovider.h"
#include "qgsfeature.h"
#include "qgsfield.h"
#include "qgslogger.h"
#include "qgspoint.h"
#include <list>
#include <set>
#include <stack>
#include <QPair>
#include <QByteArray>
#include <QDomElement>
#include <QStringList>
#include <QStack>
class QgsRectangle;
class QgsCoordinateReferenceSystem;

/* Description of feature class in GML */
class CORE_EXPORT QgsGmlFeatureClass
{
  public:
    QgsGmlFeatureClass( );
    QgsGmlFeatureClass( QString name, QString path );

    ~QgsGmlFeatureClass();

    QList<QgsField> & fields() { return  mFields; }

    int fieldIndex( const QString & name );

    QString path() const { return mPath; }

    QStringList & geometryAttributes() { return mGeometryAttributes; }

  private:
    /* Feature class name:
     *  - element name without NS or known prefix/suffix (_feature)
     *  - typeName attribute name */
    QString mName;

    //QString mElementName;

    /* Dot separated path to element including the name */
    QString mPath;

    /* Fields */
    // Do not use QMap to keep original fields order. If it gets to performance,
    // add a field index map
    QList<QgsField> mFields;

    /* Geometry attribute */
    QStringList mGeometryAttributes;
};


/**This class reads data from a WFS server or alternatively from a GML file. It uses the expat XML parser and an event based model to keep performance high. The parsing starts when the first data arrives, it does not wait until the request is finished*/
class CORE_EXPORT QgsWFSData: public QObject
{
    Q_OBJECT
  public:
    QgsWFSData();

    QgsWFSData(
      const QString& uri,
      QgsRectangle* extent,
      QMap<QgsFeatureId, QgsFeature* > &features,
      QMap<QgsFeatureId, QString > &idMap,
      const QString& geometryAttribute,
      const QMap<QString, QPair<int, QgsField> >& thematicAttributes,
      QGis::WkbType* wkbType );


    ~QgsWFSData();
    /**Does the Http GET request to the wfs server
       @param query string (to define the requested typename)
       @param extent the extent of the WFS layer
       @param srs the reference system of the layer
       @param features the features of the layer
    @return 0 in case of success*/
    int getWFSData();

    /** Read from give GML. Constructor uri param is ignored */
    int getWFSData( const QByteArray &data );

    /** Get fields info from XSD */
    bool parseXSD( const QByteArray &xml );

    /** Guess GML schema from data if XSD does not exist.
      * Currently only recognizes UMN Mapserver GetFeatureInfo GML response.
      * @param data GML data
      * @return true in case of success */
    bool guessSchema( const QByteArray &data );

    /** Get list of dot separated paths to feature classes parsed from GML or XSD */
    QStringList typeNames() const;

    /** Get map of fields parsed from XSD by parseXSD */
    QMap<int, QgsField> fields();

    /** Get fields for type/class name parsed from GML or XSD */
    QList<QgsField> fields( const QString & typeName );

    /** Get list of geometry attributes for type/class name */
    QStringList geometryAttributes( const QString & typeName );

  private slots:
    void setFinished();

    /**Takes progress value and total steps and emit signals 'dataReadProgress' and 'totalStepUpdate'*/
    void handleProgressEvent( qint64 progress, qint64 totalSteps );

    /* XSD parsing support methods */

    /** Get dom elements by path */
    QList<QDomElement> domElements( const QDomElement &element, const QString & path );

    /** Get dom element by path */
    QDomElement domElement( const QDomElement &element, const QString & path );

    /** Filter list of elements by attribute value */
    QList<QDomElement> domElements( QList<QDomElement> &elements, const QString & attr, const QString & attrVal );

    /** Get dom element by path and attribute value */
    QDomElement domElement( const QDomElement &element, const QString & path, const QString & attr, const QString & attrVal );

    /** Strip namespace from element name */
    QString stripNS( const QString & name );

    /** Find GML base type for complex type of given name
     * @param name complex type name
     * @return name of GML base type without NS, e.g. AbstractFeatureType or empty string if not pased on GML type
     */
    QString xsdComplexTypeGmlBaseType( const QDomElement &element, const QString & name );

    /** Get feature class information from complex type recursively */
    bool xsdFeatureClass( const QDomElement &element, const QString & typeName, QgsGmlFeatureClass & featureClass );

  signals:
    void dataReadProgress( int progress );
    void totalStepsUpdate( int totalSteps );
    //also emit signal with progress and totalSteps together (this is better for the status message)
    void dataProgressAndSteps( int progress, int totalSteps );

  private:

    enum ParseMode
    {
      none,
      boundingBox,
      featureMember, // gml:featureMember
      feature,  // feature element containint attrs and geo (inside gml:featureMember)
      attribute,
      geometry,
      coordinate,
      point,
      line,
      polygon,
      multiPoint,
      multiLine,
      multiPolygon
    };

    /**XML handler methods*/
    void startElement( const XML_Char* el, const XML_Char** attr );
    void endElement( const XML_Char* el );
    void characters( const XML_Char* chars, int len );
    static void start( void* data, const XML_Char* el, const XML_Char** attr )
    {
      static_cast<QgsWFSData*>( data )->startElement( el, attr );
    }
    static void end( void* data, const XML_Char* el )
    {
      static_cast<QgsWFSData*>( data )->endElement( el );
    }
    static void chars( void* data, const XML_Char* chars, int len )
    {
      static_cast<QgsWFSData*>( data )->characters( chars, len );
    }

    /**XML handler methods for guessing schema from data*/
    void startElementSchema( const XML_Char* el, const XML_Char** attr );
    void endElementSchema( const XML_Char* el );
    void charactersSchema( const XML_Char* chars, int len );
    static void startSchema( void* data, const XML_Char* el, const XML_Char** attr )
    {
      static_cast<QgsWFSData*>( data )->startElementSchema( el, attr );
    }
    static void endSchema( void* data, const XML_Char* el )
    {
      static_cast<QgsWFSData*>( data )->endElementSchema( el );
    }
    static void charsSchema( void* data, const XML_Char* chars, int len )
    {
      static_cast<QgsWFSData*>( data )->charactersSchema( chars, len );
    }

    //helper routines
    /**Reads attribute srsName="EpsgCrsId:..."
       @param epsgNr result
       @param attr attribute strings
       @return 0 in case of success*/
    int readEpsgFromAttribute( int& epsgNr, const XML_Char** attr ) const;
    /**Reads attribute as string
      @return attribute value or an empty string if no such attribute*/
    QString readAttribute( const QString& attributeName, const XML_Char** attr ) const;
    /**Creates a rectangle from a coordinate string.
     @return 0 in case of success*/
    int createBBoxFromCoordinateString( QgsRectangle* bb, const QString& coordString ) const;
    /**Creates a set of points from a coordinate string.
       @param points list that will contain the created points
       @param coordString the text containing the coordinates
       @param cs coortinate separator
       @param ts tuple separator
       @return 0 in case of success*/
    int pointsFromCoordinateString( std::list<QgsPoint>& points, const QString& coordString ) const;

    int getPointWKB( unsigned char** wkb, int* size, const QgsPoint& ) const;
    int getLineWKB( unsigned char** wkb, int* size, const std::list<QgsPoint>& lineCoordinates ) const;
    int getRingWKB( unsigned char** wkb, int* size, const std::list<QgsPoint>& ringCoordinates ) const;
    /**Creates a multiline from the information in mCurrentWKBFragments and mCurrentWKBFragmentSizes. Assign the result. The multiline is in mCurrentWKB and mCurrentWKBSize. The function deletes the memory in mCurrentWKBFragments. Returns 0 in case of success.*/
    int createMultiLineFromFragments();
    int createMultiPointFromFragments();
    int createPolygonFromFragments();
    int createMultiPolygonFromFragments();
    /**Adds all the integers contained in mCurrentWKBFragmentSizes*/
    int totalWKBFragmentSize() const;

    /**Returns pointer to main window or 0 if it does not exist*/
    QWidget* findMainWindow() const;
    /**This function evaluates the layer bounding box from the features and sets it to mExtent.
    Less efficient compared to reading the bbox from the provider, so it is only done if the wfs server
    does not provider extent information.*/
    void calculateExtentFromFeatures() const;

    /** Get safely (if empty) top from mode stack */
    ParseMode modeStackTop() { return mParseModeStack.isEmpty() ? none : mParseModeStack.top(); }

    /** Safely (if empty) pop from mode stack */
    ParseMode modeStackPop() { return mParseModeStack.isEmpty() ? none : mParseModeStack.pop(); }

    QString mUri;
    //results are members such that handler routines are able to manipulate them
    /**Bounding box of the layer*/
    QgsRectangle* mExtent;
    /**The features of the layer*/
    //QMap<QgsFeatureId, QgsFeature* > &mFeatures;
    QMap<QgsFeatureId, QgsFeature* > mFeatures;
    /**Stores the relation between provider ids and WFS server ids*/
    //QMap<QgsFeatureId, QString > &mIdMap;
    QMap<QgsFeatureId, QString > mIdMap;
    /**Name of geometry attribute*/
    QString mGeometryAttribute;
    //const QMap<QString, QPair<int, QgsField> > &mThematicAttributes;
    QMap<QString, QPair<int, QgsField> > mThematicAttributes;
    QGis::WkbType* mWkbType;
    /**True if the request is finished*/
    bool mFinished;
    /**Keep track about the most important nested elements*/
    //std::stack<ParseMode> mParseModeStack;
    QStack<ParseMode> mParseModeStack;
    /**This contains the character data if an important element has been encountered*/
    QString mStringCash;
    QgsFeature* mCurrentFeature;
    QString mCurrentFeatureId;
    int mFeatureCount;
    /**The total WKB for a feature*/
    unsigned char* mCurrentWKB;
    /**The total WKB size for a feature*/
    int mCurrentWKBSize;
    /**WKB intermediate storage during parsing. For points and lines, no intermediate WKB is stored at all. For multipoins and multilines and polygons, only one nested list is used. For multipolygons, both nested lists are used*/
    std::list< std::list<unsigned char*> > mCurrentWKBFragments;
    /**Similar to mCurrentWKB, but only the size*/
    std::list< std::list<int> > mCurrentWKBFragmentSizes;
    QString mAttributeName;
    QString mTypeName;
    QgsApplication::endian_t mEndian;
    /**Coordinate separator for coordinate strings. Usually "," */
    QString mCoordinateSeparator;
    /**Tuple separator for coordinate strings. Usually " " */
    QString mTupleSeparator;

    /* Schema informations guessed/parsed from GML in getSchema() */

    /** Depth level, root element is 0 */
    int mLevel;

    /** Skip all levels under this */
    int mSkipLevel;

    /** Path to current level */
    QStringList mParsePathStack;

    QString mCurrentFeatureName;

    // List of know geometries (Point, Multipoint,...)
    QStringList mGeometryTypes;

    /* Feature classes map with element paths as keys */
    QMap<QString, QgsGmlFeatureClass> mFeatureClassMap;
};

#endif

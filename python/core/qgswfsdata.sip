typedef QMap<int, QgsField> QgsFieldMap;

class QgsWFSData: QObject 
{

%TypeHeaderCode
#include <qgswfsdata.h>
%End

  public:
    /*
    QgsWFSData(
      const QString& uri,
      QgsRectangle* extent,
      QMap<QgsFeatureId, QgsFeature* > &features,
      QMap<QgsFeatureId, QString > &idMap,
      const QString& geometryAttribute,
      const QMap<QString, QPair<int, QgsField> >& thematicAttributes,
      QGis::WkbType* wkbType );
    */
    QgsWFSData();
    ~QgsWFSData();

    //void setFeatureType ( const QString & typeName, const QString& geometryAttribute, const QMap<QString, QPair<int, QgsField> >& thematicAttributes );
    void setFeatureType ( const QString & typeName, const QString& geometryAttribute, const QgsFieldMap & thematicAttributes );


    /**Does the Http GET request to the wfs server
       @param query string (to define the requested typename)
       @param extent the extent of the WFS layer
       @param srs the reference system of the layer
       @param features the features of the layer
    @return 0 in case of success*/
    //int getWFSData();

    /** Read from give GML. Constructor uri param is ignored */
    int getWFSData( const QByteArray &data, QGis::WkbType* wkbType );

    //QMap<QgsFeatureId, QgsFeature* > featuresMap() const;
    //QList<QgsFeature*> features() const;
    QVector<QgsFeature*> featuresVector() const;

    /** Get fields info from XSD */
    bool parseXSD( const QByteArray &xml );

    /** Guess GML schema from data if XSD does not exist.
      * Currently only recognizes UMN Mapserver GetFeatureInfo GML response.
      * @param data GML data
      * @return true in case of success */
    bool guessSchema( const QByteArray &data );

    QStringList typeNames() const;

    //QMap<int, QgsField> fields();

    QList<QgsField> fields( const QString & typeName );

    QStringList geometryAttributes( const QString & typeName );
};

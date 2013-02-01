class QgsGml: QObject 
{

%TypeHeaderCode
#include <qgsgml.h>
%End

  public:
    /*
    QgsGml(
      const QString& uri,
      QgsRectangle* extent,
      QMap<QgsFeatureId, QgsFeature* > &features,
      QMap<QgsFeatureId, QString > &idMap,
      const QString& geometryAttribute,
      const QMap<QString, QPair<int, QgsField> >& thematicAttributes,
      QGis::WkbType* wkbType );
    */
    QgsGml();
    ~QgsGml();

    //void setFeatureType ( const QString & typeName, const QString& geometryAttribute, const QMap<QString, QPair<int, QgsField> >& thematicAttributes );
    void setFeatureType ( const QString & typeName, const QString& geometryAttribute, const QgsFields & fields );


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

};

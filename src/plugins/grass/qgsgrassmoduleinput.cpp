/***************************************************************************
                          qgsgrassmoduleinput.cpp
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

#include <QCompleter>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTreeView>

#include "qgis.h"
#include "qgsdatasourceuri.h"
#include "qgslogger.h"
#include "qgsmaplayer.h"
#include "qgsmaplayerregistry.h"
#include "qgsrasterlayer.h"
#include "qgsvectorlayer.h"

#include "qgsgrass.h"
#include "qgsgrassmodule.h"
#include "qgsgrassmoduleparam.h"
#include "qgsgrassplugin.h"
#include "qgsgrassprovider.h"

extern "C"
{
#if GRASS_VERSION_MAJOR < 7
#include <grass/Vect.h>
#else
#include <grass/vector.h>
#endif
}

#include "qgsgrassmoduleinput.h"

/**************************** QgsGrassModuleInputModel ****************************/
QgsGrassModuleInputModel::QgsGrassModuleInputModel( QObject *parent )
    : QStandardItemModel( parent )
{
  setColumnCount( 1 );
  reload();
}

void QgsGrassModuleInputModel::reload()
{
  clear();
  QStringList mapsets = QgsGrass::mapsets( QgsGrass::getDefaultGisdbase(), QgsGrass::getDefaultLocation() );
  // Put current mapset on top
  mapsets.removeOne( QgsGrass::getDefaultMapset() );
  mapsets.prepend( QgsGrass::getDefaultMapset() );

  foreach ( QString mapset, mapsets )
  {
    bool currentMapset = mapset == QgsGrass::getDefaultMapset();
    QStandardItem *mapsetItem = new QStandardItem( mapset );
    mapsetItem->setData( mapset, MapsetRole );
    mapsetItem->setData( mapset, Qt::EditRole );
    mapsetItem->setData( QgsGrassObject::None, TypeRole );
    mapsetItem->setSelectable( false );

    QList<QgsGrassObject::Type> types;
    types << QgsGrassObject::Raster << QgsGrassObject::Vector;
    foreach ( QgsGrassObject::Type type, types )
    {
      QStringList maps = QgsGrass::grassObjects( QgsGrass::getDefaultGisdbase() + "/" + QgsGrass::getDefaultLocation() + "/" + mapset, type );
      foreach ( QString map, maps )
      {
        if ( map.startsWith( "qgis_import_tmp_" ) )
        {
          continue;
        }
        QString mapName = map;
        // For now, for completer popup simplicity
        // TODO: implement tree view in popup
        if ( !currentMapset )
        {
          mapName += "@" + mapset;
        }
        QStandardItem *mapItem = new QStandardItem( mapName );
        mapItem->setData( mapName, Qt::EditRole );
        mapItem->setData( map, MapRole );
        mapItem->setData( mapset, MapsetRole );
        mapItem->setData( type, TypeRole );
        mapsetItem->appendRow( mapItem );
      }
    }
    appendRow( mapsetItem );
  }
}

QgsGrassModuleInputModel::~QgsGrassModuleInputModel()
{

}

QgsGrassModuleInputModel *QgsGrassModuleInputModel::instance()
{
  static QgsGrassModuleInputModel sInstance;
  return &sInstance;
}

/**************************** QgsGrassModuleInputProxy ****************************/
QgsGrassModuleInputProxy::QgsGrassModuleInputProxy( QgsGrassObject::Type type, QObject *parent )
    : QSortFilterProxyModel( parent )
    , mType( type )
{
  setDynamicSortFilter( true );
}

bool QgsGrassModuleInputProxy::filterAcceptsRow( int sourceRow, const QModelIndex &sourceParent ) const
{
  if ( !sourceModel() )
  {
    return false;
  }
  QModelIndex sourceIndex = sourceModel()->index( sourceRow, 0, sourceParent );

  QgsDebugMsg( QString( "mType = %1 item type = %2" ).arg( mType ).arg( sourceModel()->data( sourceIndex, QgsGrassModuleInputModel::TypeRole ).toInt() ) );
  //return true;
  QgsGrassObject::Type itemType = ( QgsGrassObject::Type )( sourceModel()->data( sourceIndex, QgsGrassModuleInputModel::TypeRole ).toInt() );
  // TODO: filter out mapsets without given type? May be confusing.
  return itemType == QgsGrassObject::None || mType == itemType; // None for mapsets
}

/**************************** QgsGrassModuleInputTreeView ****************************/
QgsGrassModuleInputTreeView::QgsGrassModuleInputTreeView( QWidget * parent )
    : QTreeView( parent )
{
  setHeaderHidden( true );
}

void QgsGrassModuleInputTreeView::resetState()
{
  QAbstractItemView::setState( QAbstractItemView::NoState );
}

/**************************** QgsGrassModuleInputPopup ****************************/
QgsGrassModuleInputPopup::QgsGrassModuleInputPopup( QWidget * parent )
    : QTreeView( parent )
{
  //setMinimumHeight(200);
}

void QgsGrassModuleInputPopup::setModel( QAbstractItemModel * model )
{
  QgsDebugMsg( "entered" );
  QTreeView::setModel( model );
}

/**************************** QgsGrassModuleInputCompleterProxy ****************************/
// TODO refresh data on sourceModel data change
QgsGrassModuleInputCompleterProxy::QgsGrassModuleInputCompleterProxy( QObject * parent )
    : QAbstractProxyModel( parent )
{
}

int QgsGrassModuleInputCompleterProxy::rowCount( const QModelIndex & parent ) const
{
  Q_UNUSED( parent );
  return mRows.size();
}

QModelIndex QgsGrassModuleInputCompleterProxy::index( int row, int column, const QModelIndex & parent ) const
{
  Q_UNUSED( parent );
  return createIndex( row, column );
}

QModelIndex QgsGrassModuleInputCompleterProxy::parent( const QModelIndex & index ) const
{
  Q_UNUSED( index );
  return QModelIndex();
}

void QgsGrassModuleInputCompleterProxy::setSourceModel( QAbstractItemModel * sourceModel )
{
  QAbstractProxyModel::setSourceModel( sourceModel );
  refreshMapping();
}

QModelIndex QgsGrassModuleInputCompleterProxy::mapFromSource( const QModelIndex & sourceIndex ) const
{
  if ( !mRows.contains( sourceIndex ) )
  {
    return QModelIndex();
  }
  return createIndex( mRows.value( sourceIndex ), 0 );
}

QModelIndex QgsGrassModuleInputCompleterProxy::mapToSource( const QModelIndex & proxyIndex ) const
{
  if ( !mIndexes.contains( proxyIndex.row() ) )
  {
    return QModelIndex();
  }
  return mIndexes.value( proxyIndex.row() );
}

void QgsGrassModuleInputCompleterProxy::refreshMapping()
{
  // TODO: emit data changed
  QgsDebugMsg( "entered" );
  mIndexes.clear();
  mRows.clear();
  map( QModelIndex() );
  QgsDebugMsg( QString( "mRows.size() = %1" ).arg( mRows.size() ) );
}

void QgsGrassModuleInputCompleterProxy::map( const QModelIndex & parent, int level )
{
  //QgsDebugMsg( "entered" );
  if ( !sourceModel() )
  {
    return;
  }
  //QgsDebugMsg( "parent = " + sourceModel()->data(parent).toString() );
  for ( int i = 0; i < sourceModel()->rowCount( parent ); i++ )
  {
    QModelIndex index = sourceModel()->index( i, 0, parent );
    if ( level == 0 ) // mapset
    {
      map( index, level + 1 );
    }
    else if ( level == 1 ) // map
    {
      int row = mRows.size();
      mIndexes.insert( row, index );
      mRows.insert( index, row );
    }
  }
}

/**************************** QgsGrassModuleInputCompleter ****************************/
QgsGrassModuleInputCompleter::QgsGrassModuleInputCompleter( QWidget * parent )
    : QCompleter( parent )
    , mSeparator( ":" )
{
}

QgsGrassModuleInputCompleter::QgsGrassModuleInputCompleter( QAbstractItemModel * model, QWidget * parent )
    : QCompleter( model, parent )
    , mSeparator( ":" )
{
}

QString QgsGrassModuleInputCompleter::pathFromIndex( const QModelIndex& index ) const
{
  return QCompleter::pathFromIndex( index );
}

QStringList QgsGrassModuleInputCompleter::splitPath( const QString& path ) const
{
  return QCompleter::splitPath( path );
}

/**************************** QgsGrassModuleInputComboBox ****************************/
// Ideas from http://qt.shoutwiki.com/wiki/Implementing_QTreeView_in_QComboBox_using_Qt-_Part_2
// and bug work around https://bugreports.qt.io/browse/QTBUG-11913
QgsGrassModuleInputComboBox::QgsGrassModuleInputComboBox( QgsGrassObject::Type type, QWidget * parent )
    : QComboBox( parent )
    , mType( type )
    , mModel( 0 )
    , mTreeView( 0 )
    , mSkipHide( false )
{
  setEditable( true );

  mModel = QgsGrassModuleInputModel::instance();

  QgsGrassModuleInputProxy *proxy = new QgsGrassModuleInputProxy( mType, this );
  proxy->setSourceModel( mModel );
  //setModel ( mModel );
  setModel( proxy );

  mTreeView = new QgsGrassModuleInputTreeView( this );
  mTreeView->setSelectionMode( QAbstractItemView::SingleSelection );
  //view->setSelectionMode(QAbstractItemView::MultiSelection);
  mTreeView->viewport()->installEventFilter( this );
  setView( mTreeView );
  mTreeView->expandAll();

  QgsGrassModuleInputCompleterProxy *completerProxy = new QgsGrassModuleInputCompleterProxy( this );
  completerProxy->setSourceModel( proxy );

  QCompleter *completer = new QgsGrassModuleInputCompleter( completerProxy );
  completer->setCompletionRole( Qt::DisplayRole );
  completer->setCaseSensitivity( Qt::CaseInsensitive );
  completer->setCompletionMode( QCompleter::PopupCompletion );
  //completer->setCompletionMode( QCompleter::UnfilteredPopupCompletion );
  completer->setMaxVisibleItems( 20 );

  // TODO: set custom treeview for popup to show items in tree structure, if possible
  //QgsGrassModuleInputPopup *popupView = new QgsGrassModuleInputPopup();
  //completer->setPopup( popupView );
  //popupView->setModel( mModel );

  setCompleter( completer );
  setCurrentIndex( -1 );
}

bool QgsGrassModuleInputComboBox::eventFilter( QObject * watched, QEvent * event )
{
  QgsDebugMsg( QString( "event type = %1" ).arg( event->type() ) );
  if ( event->type() == QEvent::MouseButtonPress && watched == view()->viewport() )
  {
    QMouseEvent* mouseEvent = static_cast<QMouseEvent*>( event );
    QModelIndex index = view()->indexAt( mouseEvent->pos() );
    if ( !view()->visualRect( index ).contains( mouseEvent->pos() ) )
    {
      mSkipHide = true;
    }
  }
  return false;
}

void QgsGrassModuleInputComboBox::showPopup()
{
  setRootModelIndex( QModelIndex() );
  QComboBox::showPopup();
}

void QgsGrassModuleInputComboBox::hidePopup()
{
  QgsDebugMsg( "entered" );
  setRootModelIndex( view()->currentIndex().parent() );
  setCurrentIndex( view()->currentIndex().row() );
  if ( mSkipHide )
  {
    mSkipHide = false;
  }
  else
  {
    QComboBox::hidePopup();
  }

  // reset state to fix the bug after drag
  mTreeView->resetState();
}

QgsGrassModuleInputComboBox::~QgsGrassModuleInputComboBox()
{

}

/**************************** QgsGrassModuleInput ****************************/
QgsGrassModuleInput::QgsGrassModuleInput( QgsGrassModule *module,
    QgsGrassModuleStandardOptions *options, QString key,
    QDomElement &qdesc, QDomElement &gdesc, QDomNode &gnode,
    bool direct, QWidget * parent )
    : QgsGrassModuleGroupBoxItem( module, key, qdesc, gdesc, gnode, direct, parent )
    , mType( QgsGrassObject::Vector )
    , mModuleStandardOptions( options )
    , mGeometryTypeOption( "" )
    , mVectorLayerOption( "" )
    , mModel( 0 )
    , mComboBox( 0 )
    , mRegionButton( 0 )
    , mUpdate( false )
    , mUsesRegion( false )
    , mRequired( false )
{
  QgsDebugMsg( "called." );
  mGeometryTypeMask = GV_POINT | GV_LINE | GV_AREA;

  if ( mTitle.isEmpty() )
  {
    mTitle = tr( "Input" );
  }
  adjustTitle();

  // Check if this parameter is required
  mRequired = gnode.toElement().attribute( "required" ) == "yes";

  QDomNode promptNode = gnode.namedItem( "gisprompt" );
  QDomElement promptElem = promptNode.toElement();
  QString element = promptElem.attribute( "element" );

  if ( element == "vector" )
  {
    mType = QgsGrassObject::Vector;

    // Read type mask if "typeoption" is defined
    QString opt = qdesc.attribute( "typeoption" );
    if ( ! opt.isNull() )
    {

      QDomNode optNode = nodeByKey( gdesc, opt );

      if ( optNode.isNull() )
      {
        mErrors << tr( "Cannot find typeoption %1" ).arg( opt );
      }
      else
      {
        mGeometryTypeOption = opt;

        QDomNode valuesNode = optNode.namedItem( "values" );
        if ( valuesNode.isNull() )
        {
          mErrors << tr( "Cannot find values for typeoption %1" ).arg( opt );
        }
        else
        {
          mGeometryTypeMask = 0; //GV_POINT | GV_LINE | GV_AREA;

          QDomElement valuesElem = valuesNode.toElement();
          QDomNode valueNode = valuesElem.firstChild();

          while ( !valueNode.isNull() )
          {
            QDomElement valueElem = valueNode.toElement();

            if ( !valueElem.isNull() && valueElem.tagName() == "value" )
            {
              QDomNode n = valueNode.namedItem( "name" );
              if ( !n.isNull() )
              {
                QDomElement e = n.toElement();
                QString val = e.text().trimmed();

                if ( val == "point" )
                {
                  mGeometryTypeMask |= GV_POINT;
                }
                else if ( val == "line" )
                {
                  mGeometryTypeMask |= GV_LINE;
                }
                else if ( val == "area" )
                {
                  mGeometryTypeMask |= GV_AREA;
                }
              }
            }

            valueNode = valueNode.nextSibling();
          }
        }
      }
    }

    // Read type mask defined in configuration
    opt = qdesc.attribute( "typemask" );
    if ( ! opt.isNull() )
    {
      int mask = 0;

      if ( opt.indexOf( "point" ) >= 0 )
      {
        mask |= GV_POINT;
      }
      if ( opt.indexOf( "line" ) >= 0 )
      {
        mask |= GV_LINE;
      }
      if ( opt.indexOf( "area" ) >= 0 )
      {
        mask |= GV_AREA;
      }

      mGeometryTypeMask &= mask;
    }

    // Read "layeroption" if defined
    opt = qdesc.attribute( "layeroption" );
    if ( ! opt.isNull() )
    {

      QDomNode optNode = nodeByKey( gdesc, opt );

      if ( optNode.isNull() )
      {
        mErrors << tr( "Cannot find layeroption %1" ).arg( opt );
      }
      else
      {
        mVectorLayerOption = opt;
      }
    }

    // Read "mapid"
    mMapId = qdesc.attribute( "mapid" );
  }
  else if ( element == "cell" )
  {
    mType = QgsGrassObject::Raster;
  }
  else
  {
    mErrors << tr( "GRASS element %1 not supported" ).arg( element );
  }

  if ( qdesc.attribute( "update" ) == "yes" )
  {
    mUpdate = true;
  }

  //mModel = new QgsGrassModuleInputModel(this);
  QHBoxLayout *l = new QHBoxLayout( this );

  mComboBox = new QgsGrassModuleInputComboBox( mType, this );

  mComboBox->setSizePolicy( QSizePolicy::Expanding, QSizePolicy:: Preferred );
  l->addWidget( mComboBox );

  QString region = qdesc.attribute( "region" );
  if ( mType == QgsGrassObject::Raster
       && QgsGrass::versionMajor() >= 6 && QgsGrass::versionMinor() >= 1
       && region != "no"
     )
  {

    mRegionButton = new QPushButton(
      QgsGrassPlugin::getThemeIcon( "grass_set_region.png" ), "" );

    mRegionButton->setToolTip( tr( "Use region of this map" ) );
    mRegionButton->setCheckable( true );
    mRegionButton->setSizePolicy( QSizePolicy::Minimum,
                                  QSizePolicy:: Preferred );

    if ( !mDirect )
    {
      l->addWidget( mRegionButton );
    }
  }

  connect( QgsMapLayerRegistry::instance(), SIGNAL( layersAdded( QList<QgsMapLayer *> ) ),
           this, SLOT( updateQgisLayers() ) );
  connect( QgsMapLayerRegistry::instance(), SIGNAL( layersRemoved( QStringList ) ),
           this, SLOT( updateQgisLayers() ) );

  connect( mComboBox, SIGNAL( activated( int ) ), this, SLOT( changed( int ) ) );

  if ( !mMapId.isEmpty() )
  {
    QgsGrassModuleParam *item = mModuleStandardOptions->item( mMapId );
    if ( item )
    {
      QgsGrassModuleInput *mapInput = dynamic_cast<QgsGrassModuleInput *>( item );

      connect( mapInput, SIGNAL( valueChanged() ), this, SLOT( updateQgisLayers() ) );
    }
  }

  mUsesRegion = false;
  if ( region.length() > 0 )
  {
    if ( region == "yes" )
      mUsesRegion = true;
  }
  else
  {
    if ( type() == QgsGrassObject::Raster )
      mUsesRegion = true;
  }

  // Fill in QGIS layers
  updateQgisLayers();
}

QgsGrassModuleInput::~QgsGrassModuleInput()
{
}

bool QgsGrassModuleInput::useRegion()
{
  QgsDebugMsg( "called." );

  return mUsesRegion && mType == QgsGrassObject::Raster && mRegionButton && mRegionButton->isChecked();
}

void QgsGrassModuleInput::updateQgisLayers()
{
  QgsDebugMsg( "called." );

  QString current = mComboBox->currentText();
  //mComboBox->clear();
  mMaps.clear();
  mGeometryTypes.clear();
  mVectorLayerNames.clear();
  mMapLayers.clear();
  mBands.clear();
  mVectorFields.clear();

  // If not required, add an empty item to combobox and a padding item into
  // layer containers.
  if ( !mRequired )
  {
    mMaps.push_back( QString( "" ) );
    mVectorLayerNames.push_back( QString( "" ) );
    mMapLayers.push_back( NULL );
    mBands.append( 0 );
    //mComboBox->addItem( tr( "Select a layer" ), QVariant() );
  }

  // Find map option
  QString sourceMap;
  if ( !mMapId.isEmpty() )
  {
    QgsGrassModuleParam *item = mModuleStandardOptions->item( mMapId );
    if ( item )
    {
      QgsGrassModuleInput *mapInput = dynamic_cast<QgsGrassModuleInput *>( item );
      if ( mapInput )
        sourceMap = mapInput->currentMap();
    }
  }

  // Note: QDir::cleanPath is using '/' also on Windows
  //QChar sep = QDir::separator();
  QChar sep = '/';

  //QgsMapCanvas *canvas = mModule->qgisIface()->mapCanvas();
  //int nlayers = canvas->layerCount();
  foreach ( QString layerId, QgsMapLayerRegistry::instance()->mapLayers().keys() )
  {
    //QgsMapLayer *layer = canvas->layer( i );
    QgsMapLayer *layer =  QgsMapLayerRegistry::instance()->mapLayers().value( layerId );

    QgsDebugMsg( "layer->type() = " + QString::number( layer->type() ) );

    if ( mType == QgsGrassObject::Vector && layer->type() == QgsMapLayer::VectorLayer )
    {
      QgsVectorLayer *vector = ( QgsVectorLayer* )layer;
      QgsDebugMsg( "vector->providerType() = " + vector->providerType() );
      if ( vector->providerType() != "grass" )
        continue;

      //TODO dynamic_cast ?
      QgsGrassProvider *provider = ( QgsGrassProvider * ) vector->dataProvider();

      // Check type mask
      int geomType = provider->geometryType();

      if (( geomType == QGis::WKBPoint && !( mGeometryTypeMask & GV_POINT ) ) ||
          ( geomType == QGis::WKBLineString && !( mGeometryTypeMask & GV_LINE ) ) ||
          ( geomType == QGis::WKBPolygon && !( mGeometryTypeMask & GV_AREA ) )
         )
      {
        continue;
      }

      // TODO add map() mapset() location() gisbase() to grass provider
      QString source = QDir::cleanPath( provider->dataSourceUri() );

      QgsDebugMsg( "source = " + source );

      // Check GISDBASE and LOCATION
      QStringList split = source.split( sep, QString::SkipEmptyParts );

      if ( split.size() < 4 )
        continue;
      split.pop_back(); // layer

      QString map = split.last();
      split.pop_back(); // map

      QString mapset = split.last();
      split.pop_back(); // mapset

      //QDir locDir ( sep + split.join ( QString(sep) ) );
      //QString loc = locDir.canonicalPath();
      QString loc =  source.remove( QRegExp( "/[^/]+/[^/]+/[^/]+$" ) );
      loc = QDir( loc ).canonicalPath();

      QDir curlocDir( QgsGrass::getDefaultGisdbase() + sep + QgsGrass::getDefaultLocation() );
      QString curloc = curlocDir.canonicalPath();

      QgsDebugMsg( "loc = " + loc );
      QgsDebugMsg( "curloc = " + curloc );
      QgsDebugMsg( "mapset = " + mapset );
      QgsDebugMsg( "QgsGrass::getDefaultMapset() = " + QgsGrass::getDefaultMapset() );

      if ( loc != curloc )
        continue;

      if ( mUpdate && mapset != QgsGrass::getDefaultMapset() )
        continue;

      // Check if it comes from source map if necessary
      if ( !mMapId.isEmpty() )
      {
        QString cm = map + "@" + mapset;
        if ( sourceMap != cm )
          continue;
      }

      mMaps.push_back( map + "@" + mapset );

      QString type;
      if ( geomType == QGis::WKBPoint )
      {
        type = "point";
      }
      else if ( geomType == QGis::WKBLineString )
      {
        type = "line";
      }
      else if ( geomType == QGis::WKBPolygon )
      {
        type = "area";
      }
      else
      {
        type = "unknown";
      }

      mGeometryTypes.push_back( type );

      QString grassLayer = QString::number( provider->grassLayer() );

      QString label = layer->name() + " ( " + map + "@" + mapset
                      + " " + grassLayer + " " + type + " )";

      //mComboBox->addItem( label );
      //if ( label == current )
      //  mComboBox->setCurrentIndex( mComboBox->count() - 1 );

      mMapLayers.push_back( vector );
      mVectorLayerNames.push_back( grassLayer );

      // convert from QgsFields to std::vector<QgsField>
      mVectorFields.push_back( vector->dataProvider()->fields() );
    }
    else if ( mType == QgsGrassObject::Raster && layer->type() == QgsMapLayer::RasterLayer )
    {
      if ( mDirect )
      {
        // Add item for each numeric band
        QgsRasterLayer* rasterLayer = qobject_cast<QgsRasterLayer *>( layer );
        if ( rasterLayer && rasterLayer->dataProvider() )
        {
          QString providerKey = rasterLayer->dataProvider()->name();
          // TODO: GRASS itself is not supported for now because module is run
          // with fake GRASS gis lib and the provider needs true gis lib
          if ( providerKey == "grassraster" ) continue;
          // Cannot use WCS until the problem with missing QThread is solved
          if ( providerKey == "wcs" ) continue;
          for ( int i = 1; i <= rasterLayer->dataProvider()->bandCount(); i++ )
          {
            if ( QgsRasterBlock::typeIsNumeric( rasterLayer->dataProvider()->dataType( i ) ) )
            {
              QString uri = rasterLayer->dataProvider()->dataSourceUri();
              mMaps.push_back( uri );

              QString label = tr( "%1 (band %2)" ).arg( rasterLayer->name() ).arg( i );
              //mComboBox->addItem( label );
              mMapLayers.push_back( layer );
              mBands.append( i );

              //if ( label == current )
              //  mComboBox->setCurrentIndex( mComboBox->count() - 1 );
            }
          }
        }
      }
      else
      {
        // Check if it is GRASS raster
        QString source = QDir::cleanPath( layer->source() );

        if ( source.contains( "cellhd" ) == 0 )
          continue;

        // Most probably GRASS layer, check GISDBASE and LOCATION
        QStringList split = source.split( sep, QString::SkipEmptyParts );

        if ( split.size() < 4 )
          continue;

        QString map = split.last();
        split.pop_back(); // map
        if ( split.last() != "cellhd" )
          continue;
        split.pop_back(); // cellhd

        QString mapset = split.last();
        split.pop_back(); // mapset

        //QDir locDir ( sep + split.join ( QString(sep) ) );
        //QString loc = locDir.canonicalPath();
        QString loc =  source.remove( QRegExp( "/[^/]+/[^/]+/[^/]+$" ) );
        loc = QDir( loc ).canonicalPath();

        QDir curlocDir( QgsGrass::getDefaultGisdbase() + sep + QgsGrass::getDefaultLocation() );
        QString curloc = curlocDir.canonicalPath();

        if ( loc != curloc )
          continue;

        if ( mUpdate && mapset != QgsGrass::getDefaultMapset() )
          continue;

        mMaps.push_back( map + "@" + mapset );
        mMapLayers.push_back( layer );

        QString label = layer->name() + " ( " + map + "@" + mapset + " )";

        //mComboBox->addItem( label );
        //if ( label == current )
        //  mComboBox->setCurrentIndex( mComboBox->count() - 1 );
      }
    }
  }
}

QStringList QgsGrassModuleInput::options()
{
  QStringList list;
  QString opt;

  int current = mComboBox->currentIndex();
  if ( current < 0 ) // not found
    return list;

  if ( mDirect )
  {
    QgsMapLayer *layer = mMapLayers[current];

    if ( layer->type() == QgsMapLayer::RasterLayer )
    {
      QgsRasterLayer* rasterLayer = qobject_cast<QgsRasterLayer *>( layer );
      if ( !rasterLayer || !rasterLayer->dataProvider() )
      {
        QMessageBox::warning( 0, tr( "Warning" ), tr( "Cannot get provider" ) );
        return list;
      }
      QString grassUri;
      QString providerUri = rasterLayer->dataProvider()->dataSourceUri();
      QString providerKey = rasterLayer->dataProvider()->name();
      int band = mBands.value( current );
      if ( providerKey == "gdal" && band == 1 )
      {
        // GDAL provider and band 1 are defaults, thus we can use simply GDAL path
        grassUri = providerUri;
      }
      else
      {
        // Need to encode more info into uri
        QgsDataSourceURI uri;
        if ( providerKey == "gdal" )
        {
          // providerUri is simple file path
          // encoded uri is not currently supported by GDAL provider, it is only used here and decoded in fake gis lib
          uri.setParam( "path", providerUri );
        }
        else // WCS
        {
          // providerUri is encoded QgsDataSourceURI
          uri.setEncodedUri( providerUri );
        }
        uri.setParam( "provider", providerKey );
        uri.setParam( "band", QString::number( band ) );
        grassUri = uri.encodedUri();
      }
      opt = mKey + "=" + grassUri;
      list.push_back( opt );
    }
    else if ( layer->type() == QgsMapLayer::VectorLayer )
    {
      QgsVectorLayer* vectorLayer = qobject_cast<QgsVectorLayer *>( layer );
      if ( !vectorLayer || !vectorLayer->dataProvider() )
      {
        QMessageBox::warning( 0, tr( "Warning" ), tr( "Cannot get provider" ) );
        return list;
      }
      opt = mKey + "=" + vectorLayer->dataProvider()->dataSourceUri();
      list.push_back( opt );
    }
  }
  else
  {
    // TODO: this is hack for network nodes, do it somehow better
    if ( mMapId.isEmpty() )
    {
      if ( current <  mMaps.size() )
      {
        if ( ! mMaps[current].isEmpty() )
        {
          list.push_back( mKey + "=" + mMaps[current] );
        }
      }
    }

    if ( !mGeometryTypeOption.isEmpty() && current < mGeometryTypes.size() )
    {
      opt = mGeometryTypeOption + "=" + mGeometryTypes[current];
      list.push_back( opt );
    }

    if ( !mVectorLayerOption.isEmpty() && current < mVectorLayerNames.size() )
    {
      opt = mVectorLayerOption + "=" + mVectorLayerNames[current];
      list.push_back( opt );
    }
  }

  return list;
}

QgsFields QgsGrassModuleInput::currentFields()
{
  QgsDebugMsg( "called." );

  int limit = 0;
  if ( !mRequired )
    limit = 1;

  QgsFields fields;

  int current = mComboBox->currentIndex();
  if ( current < limit )
    return fields;

  if ( current >= limit && current <  mVectorFields.size() )
  {
    fields = mVectorFields[current];
  }

  return fields;
}

QgsMapLayer * QgsGrassModuleInput::currentLayer()
{
  QgsDebugMsg( "called." );

  int limit = 0;
  if ( !mRequired )
    limit = 1;

  int current = mComboBox->currentIndex();
  if ( current < limit )
    return 0;

  if ( current >= limit && current <  mMapLayers.size() )
  {
    return mMapLayers[current];
  }

  return 0;
}

QString QgsGrassModuleInput::currentMap()
{
  QgsDebugMsg( "called." );

  int limit = 0;
  if ( !mRequired )
    limit = 1;

  int current = mComboBox->currentIndex();
  if ( current < limit )
    return QString();

  if ( current >= limit && current < mMaps.size() )
  {
    return mMaps[current];
  }

  return QString();
}

void QgsGrassModuleInput::changed( int i )
{
  Q_UNUSED( i );
  emit valueChanged();
}

QString QgsGrassModuleInput::ready()
{
  QgsDebugMsg( "called." );

  QString error;

  QgsDebugMsg( QString( "count = %1" ).arg( mComboBox->count() ) );
  if ( mComboBox->count() == 0 )
  {
    error.append( tr( "%1:&nbsp;no input" ).arg( title() ) );
  }
  return error;
}



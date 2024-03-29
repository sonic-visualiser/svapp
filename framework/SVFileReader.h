/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef SV_FILE_READER_H
#define SV_FILE_READER_H

#include "layer/LayerFactory.h"
#include "transform/Transform.h"

#include <map>

class QXmlStreamReader;
class QXmlStreamAttributes;

namespace sv {

class Pane;
class Model;
class Path;
class Document;
class PlayParameters;

class SVFileReaderPaneCallback
{
public:
    virtual ~SVFileReaderPaneCallback();
    virtual Pane *addPane() = 0;
    virtual void setWindowSize(int width, int height) = 0;
    virtual void addSelection(sv_frame_t start, sv_frame_t end) = 0;
};

/**
    SVFileReader loads Sonic Visualiser XML files.  (The SV file
    format is bzipped XML.)

    Some notes about the SV XML format follow.  We're very lazy with
    our XML: there's no schema or DTD, and we depend heavily on
    elements being in a particular order.
 
\verbatim

    <sv>

    <data>

      <!-- The data section contains definitions of both models and
           visual layers.  Layers are considered data in the document;
           the structure of views that displays the layers is not. -->

      <!-- id numbers are unique within the data type (i.e. no two
           models can have the same id, but a model can have the same
           id as a layer, etc).  SV generates its id numbers just for
           the purpose of cross-referencing within the current file;
           they don't necessarily have any meaning once the file has
           been loaded. -->

      <model id="0" name="..." type="..." ... />
      <model id="1" name="..." type="..." ... />

      <!-- Models that have data associated with them store it
           in a neighbouring dataset element.  The dataset must follow
           the model and precede any derivation or layer elements that
           refer to the model. -->

      <model id="2" name="..." type="..." dataset="0" ... />

      <dataset id="0" type="..."> 
        <point frame="..." value="..." ... />
      </dataset>

      <!-- Where one model is derived from another via a transform,
           it has an associated derivation element.  This must follow
           both the source and target model elements.  The source and
           model attributes give the source model id and target model
           id respectively.  A model can have both dataset and
           derivation elements; if it does, dataset must appear first. 
           If the model's data are not stored, but instead the model
           is to be regenerated completely from the transform when 
           the session is reloaded, then the model should have _only_
           a derivation element, and no model element should appear
           for it at all. -->

      <derivation type="transform" source="0" model="2" channel="-1">
        <transform id="vamp:soname:pluginid:output" ... />
      </derivation>

      <!-- Note that the derivation element just described replaces
           this earlier formulation, which had more attributes in the
           derivation element and a plugin element describing plugin
           parameters and properties.  What we actually read and
           write these days is a horrid composite of the two formats,
           for backward compatibility reasons. -->

      <derivation source="0" model="2" transform="vamp:soname:pluginid:output" ...>
        <plugin id="pluginid" ... />
      </derivation>

      <!-- The playparameters element lists playback settings for
           a model. -->

      <playparameters mute="false" pan="0" gain="1" model="1" ... />

      <!-- Layer elements.  The models must have already been defined.
           The same model may appear in more than one layer (of more
           than one type). -->

      <layer id="1" type="..." name="..." model="0" ... />
      <layer id="2" type="..." name="..." model="1" ... />

    </data>


    <display>

      <!-- The display element contains visual structure for the
           layers.  It's simpler than the data section. -->

      <!-- Overall preferred window size for this session. (Now
           deprecated, it wasn't a good idea to try to persist this) -->

      <window width="..." height="..."/>

      <!-- List of view elements to stack up.  Each one contains
           a list of layers in stacking order, back to front. -->

      <view type="pane" ...>
        <layer id="1"/>
        <layer id="2"/>
      </view>

      <!-- The layer elements just refer to layers defined in the
           data section, so they don't have to have any attributes
           other than the id.  For sort-of-historical reasons SV
           actually does repeat the other attributes here, but
           it doesn't need to. -->

      <view type="pane" ...>
        <layer id="2"/>
      <view>

    </display>


    <!-- List of selected regions by audio frame extents. -->

    <selections>
      <selection start="..." end="..."/>
    </selections>


    </sv>
 
\endverbatim
 */


class SVFileReader : public QObject
{
    Q_OBJECT

public:
    SVFileReader(Document *document,
                 SVFileReaderPaneCallback &callback,
                 QString location = ""); // for audio file locate mechanism
    virtual ~SVFileReader();

    void parseXml(QString xmlData);
    void parseFile(QString filename);
    void parseFile(QIODevice *file);

    bool isOK();
    QString getErrorString() const { return m_errorString; }

    // For loading a single layer onto an existing pane
    void setCurrentPane(Pane *pane) { m_currentPane = pane; }
    
    enum FileType
    {
        SVSessionFile,
        SVLayerFile,
        UnknownFileType
    };

    static FileType identifyXmlFile(QString path);

signals:
    void modelRegenerationFailed(QString layerName, QString transformName,
                                 QString message);
    void modelRegenerationWarning(QString layerName, QString transformName,
                                  QString message);

protected:
    void parseWith(QXmlStreamReader &);
    bool startElement(const QString &localName,
                      const QXmlStreamAttributes& atts);
    bool characters(const QString &);
    bool endElement(const QString &localName);

    
    bool readWindow(const QXmlStreamAttributes &);
    bool readModel(const QXmlStreamAttributes &);
    bool readView(const QXmlStreamAttributes &);
    bool readLayer(const QXmlStreamAttributes &);
    bool readDatasetStart(const QXmlStreamAttributes &);
    bool addBinToDataset(const QXmlStreamAttributes &);
    bool addPointToDataset(const QXmlStreamAttributes &);
    bool addRowToDataset(const QXmlStreamAttributes &);
    bool readRowData(const QString &);
    bool readDerivation(const QXmlStreamAttributes &);
    bool readPlayParameters(const QXmlStreamAttributes &);
    bool readPlugin(const QXmlStreamAttributes &);
    bool readPluginForTransform(const QXmlStreamAttributes &);
    bool readPluginForPlayback(const QXmlStreamAttributes &);
    bool readTransform(const QXmlStreamAttributes &);
    bool readParameter(const QXmlStreamAttributes &);
    bool readSelection(const QXmlStreamAttributes &);
    bool readMeasurement(const QXmlStreamAttributes &);

    void makeAggregateModels();
    void addUnaddedModels();

    // We use the term "pending" of things that have been referred to
    // but not yet constructed because their definitions are
    // incomplete. They are just referred to with an ExportId.  Models
    // that have been constructed are always added straight away to
    // ById and are referred to with a ModelId (everywhere where
    // previously we would have used a Model *). m_models maps from
    // ExportId (as read from the file) to complete Models, or to a
    // ModelId of None for any model that could not be constructed for
    // some reason.

    typedef XmlExportable::ExportId ExportId;
    
    bool haveModel(ExportId id) {
        return (m_models.find(id) != m_models.end()) && !m_models[id].isNone();
    }
    
    struct PendingAggregateRec {
        QString name;
        sv_samplerate_t sampleRate;
        std::vector<ExportId> components;
    };
    
    Document *m_document;
    SVFileReaderPaneCallback &m_paneCallback;
    QString m_location;
    Pane *m_currentPane;
    std::map<ExportId, Layer *> m_layers;
    std::map<ExportId, ModelId> m_models;
    std::map<ExportId, Path *> m_paths;
    std::set<ModelId> m_addedModels; // i.e. added to Document, not just ById
    std::map<ExportId, PendingAggregateRec> m_pendingAggregates;

    // A model element often contains a dataset id, and the dataset
    // then follows it. When the model is read, an entry in this map
    // is added, mapping from the dataset's export id (the actual
    // dataset has not been read yet) back to the export id of the
    // object that needs it. We map to export id rather than model id,
    // because the object might be a path rather than a model.
    std::map<ExportId, ExportId> m_awaitingDatasets;

    // And then this is the model or path that a dataset element is
    // currently being read into, i.e. the value looked up from
    // m_awaitingDatasets at the point where the dataset is found.
    ExportId m_currentDataset;

    Layer *m_currentLayer;
    ModelId m_currentDerivedModel;
    ExportId m_pendingDerivedModel;
    std::shared_ptr<PlayParameters> m_currentPlayParameters;
    Transform m_currentTransform;
    ModelId m_currentTransformSource;
    int m_currentTransformChannel;
    bool m_currentTransformIsNewStyle;
    QString m_datasetSeparator;
    bool m_inRow;
    bool m_inLayer;
    bool m_inView;
    bool m_inData;
    bool m_inSelections;
    int m_rowNumber;
    QString m_errorString;
    bool m_ok;
};

} // end namespace sv

#endif

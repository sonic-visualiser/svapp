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

#ifndef SV_DOCUMENT_H
#define SV_DOCUMENT_H

#include "layer/LayerFactory.h"
#include "transform/Transform.h"
#include "transform/ModelTransformer.h"
#include "transform/FeatureExtractionModelTransformer.h"
#include "base/Command.h"

#include <map>
#include <set>

class Model;
class Layer;
class View;
class WaveFileModel;
class AggregateWaveModel;

class AdditionalModelConverter;

class Align;

/**
 * A Sonic Visualiser document consists of a set of data models, and
 * also the visualisation layers used to display them.  Changes to the
 * layers and their layout need to be stored and managed in much the
 * same way as changes to the underlying data.
 * 
 * The document manages:
 * 
 *  - A main data Model, which provides the underlying sample rate and
 * such like.  This must be a WaveFileModel.
 * 
 *  - Any number of imported Model objects, which contain data without any
 * requirement to remember where the data came from or how to
 * regenerate it.
 * 
 *  - Any number of Model objects that were generated by a Transformer
 * such as FeatureExtractionModelTransformer.  For these, we also
 * record the source model and the name of the transform used to
 * generate the model so that we can regenerate it (potentially
 * from a different source) on demand.
 *
 *  - A flat list of Layer objects.  Elsewhere, the GUI may distribute these
 * across any number of View widgets.  A layer may be viewable on more
 * than one view at once, in principle.  A layer refers to one model,
 * but the same model can be in use in more than one layer.
 *
 * The document does *not* manage the existence or structure of Pane
 * and other view widgets.  However, it does provide convenience
 * methods for reference-counted command-based management of the
 * association between layers and views (addLayerToView,
 * removeLayerFromView).
 */

class Document : public QObject,
                 public XmlExportable
{
    Q_OBJECT

public:
    Document();
    virtual ~Document();

    /**
     * Create and return a new layer of the given type, associated
     * with no model.  The caller may set any model on this layer, but
     * the model must also be registered with the document via the
     * add-model methods below.
     */
    Layer *createLayer(LayerFactory::LayerType);

    /**
     * Create and return a new layer of the given type, associated
     * with the current main model (if appropriate to the layer type).
     */
    Layer *createMainModelLayer(LayerFactory::LayerType);

    /**
     * Create and return a new layer associated with the given model,
     * and register the model as an imported model.
     */
    Layer *createImportedLayer(ModelId);

    /**
     * Create and return a new layer of the given type, with an
     * appropriate empty model.  If the given type is not one for
     * which an empty model can meaningfully be created, return 0.
     */
    Layer *createEmptyLayer(LayerFactory::LayerType);

    /**
     * Create and return a new layer of the given type, associated
     * with the given transform name.  This method does not run the
     * transform itself, nor create a model.  The caller can safely
     * add a model to the layer later, but note that all models used
     * by a transform layer _must_ be registered with the document
     * using addDerivedModel below.
     */
    Layer *createDerivedLayer(LayerFactory::LayerType, TransformId);

    /**
     * Create and return a suitable layer for the given transform,
     * running the transform and associating the resulting model with
     * the new layer.
     */
    Layer *createDerivedLayer(const Transform &,
                              const ModelTransformer::Input &);

    /**
     * Create and return suitable layers for the given transforms,
     * which must be identical apart from the output (i.e. must use
     * the same plugin and configuration). The layers are returned in
     * the same order as the transforms are supplied.
     */
    std::vector<Layer *> createDerivedLayers(const Transforms &,
                                             const ModelTransformer::Input &);

    typedef void *LayerCreationAsyncHandle;

    class LayerCreationHandler {
    public:
        virtual ~LayerCreationHandler() { }

        /**
         * The primary layers are those corresponding 1-1 to the input
         * models, listed in the same order as the input models. The
         * additional layers vector contains any layers (from all
         * models) that were returned separately at the end of
         * processing. The handle indicates which async process this
         * callback was initiated by. It must not be used again after
         * this function returns.
         */
        virtual void layersCreated(LayerCreationAsyncHandle handle,
                                   std::vector<Layer *> primary,
                                   std::vector<Layer *> additional) = 0;
    };

    /**
     * Create suitable layers for the given transforms, which must be
     * identical apart from the output (i.e. must use the same plugin
     * and configuration). This method returns after initialising the
     * transformer process, and the layers are returned through a
     * subsequent call to the provided handler (which must be
     * non-null). The handle returned will be passed through to the
     * handler callback.
     */
    LayerCreationAsyncHandle createDerivedLayersAsync(const Transforms &,
                                                      const ModelTransformer::Input &,
                                                      LayerCreationHandler *handler);

    /**
     * Indicate that the async layer creation task associated with the
     * given handle should be cancelled. There is no guarantee about
     * what this will mean, and the handler callback may still be
     * called.
     */
    void cancelAsyncLayerCreation(LayerCreationAsyncHandle handle);

    /**
     * Delete the given layer, and also its associated model if no
     * longer used by any other layer.  In general, this should be the
     * only method used to delete layers -- doing so directly is a bit
     * of a social gaffe.
     */
    void deleteLayer(Layer *, bool force = false);

    /**
     * Set the main model (the source for playback sample rate, etc)
     * to the given wave file model.  This will regenerate any derived
     * models that were based on the previous main model. The model
     * must have been added to ModelById already, and Document will
     * take responsibility for releasing it later.
     */
    void setMainModel(ModelId); // a WaveFileModel

    /**
     * Get the main model (the source for playback sample rate, etc).
     */
    ModelId getMainModel() { return m_mainModel; }
    
    std::vector<ModelId> getTransformInputModels();

    /**
     * Return true if the model id is known to be the main model or
     * one of the other existing models that can be shown in a new
     * layer.
     */
    bool isKnownModel(ModelId) const;

    /**
     * Add a derived model associated with the given transform,
     * running the transform and returning the resulting model.
     * The model is added to ModelById before returning.
     */
    ModelId addDerivedModel(const Transform &transform,
                            const ModelTransformer::Input &input,
                            QString &returnedMessage);

    /**
     * Add derived models associated with the given set of related
     * transforms, running the transforms and returning the resulting
     * models.  The models are added to ModelById before returning.
     */
    friend class AdditionalModelConverter;
    std::vector<ModelId> addDerivedModels(const Transforms &transforms,
                                          const ModelTransformer::Input &input,
                                          QString &returnedMessage,
                                          AdditionalModelConverter *);

    /**
     * Add a derived model associated with the given transform.  This
     * is necessary to register any derived model that was not created
     * by the document using createDerivedModel or
     * createDerivedLayer. Document will take responsibility for
     * releasing the model later.
     */
    void addAlreadyDerivedModel(const Transform &transform,
                                const ModelTransformer::Input &input,
                                ModelId outputModelToAdd);

    /**
     * Add an imported model, i.e. any model (other than the main
     * model) that has been created by any means other than as the
     * output of a transform.  This is necessary to register any
     * imported model that is to be associated with a layer, and also
     * to make sure that the model is released by the Document
     * later. Aggregate models, alignment models, and miscellaneous
     * temporary models should also be added in this way, unless the
     * temporary models are large enough to need managing in a way
     * that guarantees the shortest possible lifespan.
     */
    void addNonDerivedModel(ModelId);

    /**
     * Associate the given model with the given layer.  The model must
     * have already been registered using one of the addXXModel
     * methods above.
     */
    void setModel(Layer *, ModelId);

    /**
     * Set the given layer to use the given channel of its model (-1
     * means all available channels).
     */
    void setChannel(Layer *, int);

    /**
     * Add the given layer to the given view.  If the layer is
     * intended to show a particular model, the model should normally
     * be set using setModel before this method is called.
     */
    void addLayerToView(View *, Layer *);

    /**
     * Remove the given layer from the given view. Ownership of the
     * layer is transferred to a command object on the undo stack, and
     * the layer will be deleted when the undo stack is pruned.
     */
    void removeLayerFromView(View *, Layer *);

    /**
     * Return true if alignment is supported (i.e. if the necessary
     * plugin is found).
     */
    static bool canAlign();

    /**
     * Specify whether models added via addImportedModel should be
     * automatically aligned against the main model if appropriate.
     */
    void setAutoAlignment(bool on) { m_autoAlignment = on; }

    /**
     * Generate alignments for all appropriate models against the main
     * model.  Existing alignments will not be re-calculated unless
     * the main model has changed since they were calculated.
     */
    void alignModels();

    /**
     * Re-generate alignments for all appropriate models against the
     * main model.  Existing alignments will be re-calculated.
     */
    void realignModels();

    /**
     * Return true if any external files (most obviously audio) failed
     * to be found on load, so that the document is incomplete
     * compared to its saved description.
     */
    bool isIncomplete() const { return m_isIncomplete; }

    void setIncomplete(bool i) { m_isIncomplete = i; }

    void toXml(QTextStream &, QString indent, QString extraAttributes) const override;
    void toXmlAsTemplate(QTextStream &, QString indent, QString extraAttributes) const;

signals:
    void layerAdded(Layer *);
    void layerRemoved(Layer *);
    void layerAboutToBeDeleted(Layer *);

    // Emitted when a layer is first added to a view, or when it is
    // last removed from a view
    void layerInAView(Layer *, bool);

    void modelAdded(ModelId);
    void mainModelChanged(ModelId); // a WaveFileModel; emitted after modelAdded

    void modelGenerationFailed(QString transformName, QString message);
    void modelGenerationWarning(QString transformName, QString message);
    void modelRegenerationFailed(QString layerName, QString transformName,
                                 QString message);
    void modelRegenerationWarning(QString layerName, QString transformName,
                                  QString message);

    void alignmentComplete(ModelId); // an AlignmentModel
    void alignmentFailed(ModelId, QString message); // an AlignmentModel

    void activity(QString);

protected slots:
    void performDeferredAlignment(ModelId);
    
protected:
    void releaseModel(ModelId model);

    /**
     * If model is suitable for alignment, align it against the main
     * model and store the alignment in the model. If the model has an
     * alignment already for the current main model, leave it
     * unchanged unless forceRecalculate is true.
     */
    void alignModel(ModelId, bool forceRecalculate = false);

    /*
     * Every model that is in use by a layer in the document must be
     * found in either m_mainModel or m_models.  We own and control
     * the lifespan of all of these models.
     */

    /**
     * The model that provides the underlying sample rate, etc.  This
     * model is not reference counted for layers, and is not freed
     * unless it is replaced or the document is deleted.
     */
    ModelId m_mainModel; // a WaveFileModel

    struct ModelRecord
    {
        // Information associated with a non-main model.  If this
        // model is derived from another, then source will be
        // something other than None and the transform name will be
        // set appropriately.  If the transform is set but source is
        // None, then there was a transform involved but the (target)
        // model has been modified since being generated from it.
        
        // This does not use ModelTransformer::Input, because it would
        // be confusing to have Input objects hanging around with None
        // models in them.

        ModelId source; // may be None
        int channel;
        Transform transform;
        bool additional;
    };

    // These must be stored in increasing order of id (as in the
    // ordered std::map), to ensure repeatability for automated tests
    std::map<ModelId, ModelRecord> m_models;

    std::set<ModelId> m_aggregateModels;
    std::set<ModelId> m_alignmentModels;
    
    /**
     * Add an extra derived model (returned at the end of processing a
     * transform).
     */
    void addAdditionalModel(ModelId);

    class AddLayerCommand : public Command
    {
    public:
        AddLayerCommand(Document *d, View *view, Layer *layer);
        virtual ~AddLayerCommand();
        
        void execute() override;
        void unexecute() override;
        QString getName() const override;

    protected:
        Document *m_d;
        View *m_view; // I don't own this
        Layer *m_layer; // Document owns this, but I determine its lifespan
        QString m_name;
        bool m_added;
    };

    class RemoveLayerCommand : public Command
    {
    public:
        RemoveLayerCommand(Document *d, View *view, Layer *layer);
        virtual ~RemoveLayerCommand();
        
        void execute() override;
        void unexecute() override;
        QString getName() const override;

    protected:
        Document *m_d;
        View *m_view; // I don't own this
        Layer *m_layer; // Document owns this, but I determine its lifespan
        bool m_wasDormant;
        QString m_name;
        bool m_added;
    };

    typedef std::map<Layer *, std::set<View *> > LayerViewMap;
    LayerViewMap m_layerViewMap;

    void addToLayerViewMap(Layer *, View *);
    void removeFromLayerViewMap(Layer *, View *);

    QString getUniqueLayerName(QString candidate);
    void writeBackwardCompatibleDerivation(QTextStream &, QString, ModelId,
                                           const ModelRecord &) const;

    void toXml(QTextStream &, QString, QString, bool asTemplate) const;
    void writePlaceholderMainModel(QTextStream &, QString) const;

    std::vector<Layer *> createLayersForDerivedModels(std::vector<ModelId>,
                                                      QStringList names);

    /**
     * And these are the layers.  We also control the lifespans of
     * these (usually through the commands used to add and remove them).
     */
    typedef std::vector<Layer *> LayerList;
    LayerList m_layers;

    bool m_autoAlignment;
    Align *m_align;

    bool m_isIncomplete;
};

#endif

/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef _AUDIO_GENERATOR_H_
#define _AUDIO_GENERATOR_H_

class Model;
class NoteModel;
class FlexiNoteModel;
class DenseTimeValueModel;
class SparseOneDimensionalModel;
class Playable;
class ClipMixer;
class ContinuousSynth;

#include <QObject>
#include <QMutex>

#include <set>
#include <map>
#include <vector>

class AudioGenerator : public QObject
{
    Q_OBJECT

public:
    AudioGenerator();
    virtual ~AudioGenerator();

    /**
     * Add a data model to be played from and initialise any necessary
     * audio generation code.  Returns true if the model will be
     * played.  The model will be added regardless of the return
     * value.
     */
    virtual bool addModel(Model *model);

    /**
     * Remove a model.
     */
    virtual void removeModel(Model *model);

    /**
     * Remove all models.
     */
    virtual void clearModels();

    /**
     * Reset playback, clearing buffers and the like.
     */
    virtual void reset();

    /**
     * Set the target channel count.  The buffer parameter to mixModel
     * must always point to at least this number of arrays.
     */
    virtual void setTargetChannelCount(size_t channelCount);

    /**
     * Return the internal processing block size.  The frameCount
     * argument to all mixModel calls must be a multiple of this
     * value.
     */
    virtual size_t getBlockSize() const;

    /**
     * Mix a single model into an output buffer.
     */
    virtual size_t mixModel(Model *model, size_t startFrame, size_t frameCount,
			    float **buffer, size_t fadeIn = 0, size_t fadeOut = 0);

    /**
     * Specify that only the given set of models should be played.
     */
    virtual void setSoloModelSet(std::set<Model *>s);

    /**
     * Specify that all models should be played as normal (if not
     * muted).
     */
    virtual void clearSoloModelSet();

protected slots:
    void playClipIdChanged(const Playable *, QString);

protected:
    size_t m_sourceSampleRate;
    size_t m_targetChannelCount;

    bool m_soloing;
    std::set<Model *> m_soloModelSet;

    struct NoteOff {

        NoteOff(float _freq, size_t _frame) : frequency(_freq), frame(_frame) { }

        float frequency;
	size_t frame;

	struct Comparator {
	    bool operator()(const NoteOff &n1, const NoteOff &n2) const {
		return n1.frame < n2.frame;
	    }
	};
    };


    typedef std::map<const Model *, ClipMixer *> ClipMixerMap;

    typedef std::multiset<NoteOff, NoteOff::Comparator> NoteOffSet;
    typedef std::map<const Model *, NoteOffSet> NoteOffMap;

    typedef std::map<const Model *, ContinuousSynth *> ContinuousSynthMap;

    QMutex m_mutex;

    ClipMixerMap m_clipMixerMap;
    NoteOffMap m_noteOffs;
    static QString m_sampleDir;

    ContinuousSynthMap m_continuousSynthMap;

    bool usesClipMixer(const Model *);
    bool usesContinuousSynth(const Model *);

    ClipMixer *makeClipMixerFor(const Model *model);
    ContinuousSynth *makeSynthFor(const Model *model);

    static void initialiseSampleDir();

    virtual size_t mixDenseTimeValueModel
    (DenseTimeValueModel *model, size_t startFrame, size_t frameCount,
     float **buffer, float gain, float pan, size_t fadeIn, size_t fadeOut);

    virtual size_t mixClipModel
    (Model *model, size_t startFrame, size_t frameCount,
     float **buffer, float gain, float pan);

    virtual size_t mixContinuousSynthModel
    (Model *model, size_t startFrame, size_t frameCount,
     float **buffer, float gain, float pan);
    
    static const size_t m_processingBlockSize;
};

#endif


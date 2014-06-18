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
    virtual void setTargetChannelCount(int channelCount);

    /**
     * Return the internal processing block size.  The frameCount
     * argument to all mixModel calls must be a multiple of this
     * value.
     */
    virtual int getBlockSize() const;

    /**
     * Mix a single model into an output buffer.
     */
    virtual int mixModel(Model *model, int startFrame, int frameCount,
			    float **buffer, int fadeIn = 0, int fadeOut = 0);

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
    int m_sourceSampleRate;
    int m_targetChannelCount;
    int m_waveType;

    bool m_soloing;
    std::set<Model *> m_soloModelSet;

    struct NoteOff {

        NoteOff(float _freq, int _frame) : frequency(_freq), frame(_frame) { }

        float frequency;
	int frame;

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
    bool wantsQuieterClips(const Model *);
    bool usesContinuousSynth(const Model *);

    ClipMixer *makeClipMixerFor(const Model *model);
    ContinuousSynth *makeSynthFor(const Model *model);

    static void initialiseSampleDir();

    virtual int mixDenseTimeValueModel
    (DenseTimeValueModel *model, int startFrame, int frameCount,
     float **buffer, float gain, float pan, int fadeIn, int fadeOut);

    virtual int mixClipModel
    (Model *model, int startFrame, int frameCount,
     float **buffer, float gain, float pan);

    virtual int mixContinuousSynthModel
    (Model *model, int startFrame, int frameCount,
     float **buffer, float gain, float pan);
    
    static const int m_processingBlockSize;
};

#endif


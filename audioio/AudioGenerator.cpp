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

#include "AudioGenerator.h"

#include "base/TempDirectory.h"
#include "base/PlayParameters.h"
#include "base/PlayParameterRepository.h"
#include "base/Pitch.h"
#include "base/Exceptions.h"

#include "data/model/NoteModel.h"
#include "data/model/FlexiNoteModel.h"
#include "data/model/DenseTimeValueModel.h"
#include "data/model/SparseOneDimensionalModel.h"
#include "data/model/NoteData.h"

#include <iostream>
#include <cmath>

#include <QDir>
#include <QFile>

const size_t
AudioGenerator::m_processingBlockSize = 2048;

QString
AudioGenerator::m_sampleDir = "";

//#define DEBUG_AUDIO_GENERATOR 1

AudioGenerator::AudioGenerator() :
    m_sourceSampleRate(0),
    m_targetChannelCount(1),
    m_soloing(false)
{
    initialiseSampleDir();

    connect(PlayParameterRepository::getInstance(),
            SIGNAL(playSampleIdChanged(const Playable *, QString)),
            this,
            SLOT(playSampleIdChanged(const Playable *, QString)));
}

AudioGenerator::~AudioGenerator()
{
#ifdef DEBUG_AUDIO_GENERATOR
    SVDEBUG << "AudioGenerator::~AudioGenerator" << endl;
#endif
}

void
AudioGenerator::initialiseSampleDir()
{
    if (m_sampleDir != "") return;

    try {
        m_sampleDir = TempDirectory::getInstance()->getSubDirectoryPath("samples");
    } catch (DirectoryCreationFailed f) {
        cerr << "WARNING: AudioGenerator::initialiseSampleDir:"
                  << " Failed to create temporary sample directory"
                  << endl;
        m_sampleDir = "";
        return;
    }

    QDir sampleResourceDir(":/samples", "*.wav");

    for (unsigned int i = 0; i < sampleResourceDir.count(); ++i) {

        QString fileName(sampleResourceDir[i]);
        QFile file(sampleResourceDir.filePath(fileName));
        QString target = QDir(m_sampleDir).filePath(fileName);

        if (!file.copy(target)) {
            cerr << "WARNING: AudioGenerator::getSampleDir: "
                      << "Unable to copy " << fileName
                      << " into temporary directory \""
                      << m_sampleDir << "\"" << endl;
        } else {
            QFile tf(target);
            tf.setPermissions(tf.permissions() |
                              QFile::WriteOwner |
                              QFile::WriteUser);
        }
    }
}

bool
AudioGenerator::addModel(Model *model)
{
    if (m_sourceSampleRate == 0) {

	m_sourceSampleRate = model->getSampleRate();

    } else {

	DenseTimeValueModel *dtvm =
	    dynamic_cast<DenseTimeValueModel *>(model);

	if (dtvm) {
	    m_sourceSampleRate = model->getSampleRate();
	    return true;
	}
    }
/*!!!
    RealTimePluginInstance *plugin = loadPluginFor(model);
    if (plugin) {
        QMutexLocker locker(&m_mutex);
        m_synthMap[model] = plugin;
        return true;
    }
*/
    return false;
}

void
AudioGenerator::playSampleIdChanged(const Playable *playable, QString)
{
    const Model *model = dynamic_cast<const Model *>(playable);
    if (!model) {
        cerr << "WARNING: AudioGenerator::playSampleIdChanged: playable "
                  << playable << " is not a supported model type"
                  << endl;
        return;
    }

    if (m_synthMap.find(model) == m_synthMap.end()) return;
/*!!!    
    RealTimePluginInstance *plugin = loadPluginFor(model);
    if (plugin) {
        QMutexLocker locker(&m_mutex);
        delete m_synthMap[model];
        m_synthMap[model] = plugin;
    }
*/
}
/*!!!
void
AudioGenerator::playPluginConfigurationChanged(const Playable *playable,
                                               QString configurationXml)
{
//    SVDEBUG << "AudioGenerator::playPluginConfigurationChanged" << endl;

    const Model *model = dynamic_cast<const Model *>(playable);
    if (!model) {
        cerr << "WARNING: AudioGenerator::playSampleIdChanged: playable "
                  << playable << " is not a supported model type"
                  << endl;
        return;
    }

    if (m_synthMap.find(model) == m_synthMap.end()) {
        SVDEBUG << "AudioGenerator::playPluginConfigurationChanged: We don't know about this plugin" << endl;
        return;
    }

    RealTimePluginInstance *plugin = m_synthMap[model];
    if (plugin) {
        PluginXml(plugin).setParametersFromXml(configurationXml);
    }
}

void
AudioGenerator::setSampleDir(RealTimePluginInstance *plugin)
{
    if (m_sampleDir != "") {
        plugin->configure("sampledir", m_sampleDir.toStdString());
    }
} 

RealTimePluginInstance *
AudioGenerator::loadPluginFor(const Model *model)
{
    QString pluginId, configurationXml;

    const Playable *playable = model;
    if (!playable || !playable->canPlay()) return 0;

    PlayParameters *parameters =
	PlayParameterRepository::getInstance()->getPlayParameters(playable);
    if (parameters) {
        pluginId = parameters->getPlaySampleId();
        configurationXml = parameters->getPlayPluginConfiguration();
    }

    std::cerr << "AudioGenerator::loadPluginFor(" << model << "): id = " << pluginId << std::endl;

    if (pluginId == "") {
        SVDEBUG << "AudioGenerator::loadPluginFor(" << model << "): parameters contain empty plugin ID, skipping" << endl;
        return 0;
    }

    RealTimePluginInstance *plugin = loadPlugin(pluginId, "");
    if (!plugin) return 0;

    std::cerr << "AudioGenerator::loadPluginFor(" << model << "): loaded plugin "
              << plugin << std::endl;

    if (configurationXml != "") {
        PluginXml(plugin).setParametersFromXml(configurationXml);
        setSampleDir(plugin);
    }

    configurationXml = PluginXml(plugin).toXmlString();

    if (parameters) {
        parameters->setPlaySampleId(pluginId);
        parameters->setPlayPluginConfiguration(configurationXml);
    }

    return plugin;
}

RealTimePluginInstance *
AudioGenerator::loadPlugin(QString pluginId, QString program)
{
    RealTimePluginFactory *factory =
	RealTimePluginFactory::instanceFor(pluginId);
    
    if (!factory) {
	cerr << "Failed to get plugin factory" << endl;
	return 0;
    }
	
    RealTimePluginInstance *instance =
	factory->instantiatePlugin
	(pluginId, 0, 0, m_sourceSampleRate, m_processingBlockSize, m_targetChannelCount);

    if (!instance) {
	cerr << "Failed to instantiate plugin " << pluginId << endl;
        return 0;
    }

    setSampleDir(instance);

    for (unsigned int i = 0; i < instance->getParameterCount(); ++i) {
        instance->setParameterValue(i, instance->getParameterDefault(i));
    }
    std::string defaultProgram = instance->getProgram(0, 0);
    if (defaultProgram != "") {
        cerr << "first selecting default program " << defaultProgram << endl;
        instance->selectProgram(defaultProgram);
    }
    if (program != "") {
        cerr << "now selecting desired program " << program << endl;
        instance->selectProgram(program.toStdString());
    }
    instance->setIdealChannelCount(m_targetChannelCount); // reset!

    return instance;
}
*/
void
AudioGenerator::removeModel(Model *model)
{
    SparseOneDimensionalModel *sodm =
	dynamic_cast<SparseOneDimensionalModel *>(model);
    if (!sodm) return; // nothing to do

    QMutexLocker locker(&m_mutex);

    if (m_synthMap.find(sodm) == m_synthMap.end()) return;

//!!!    RealTimePluginInstance *instance = m_synthMap[sodm];
//    m_synthMap.erase(sodm);
    delete instance;
}

void
AudioGenerator::clearModels()
{
    QMutexLocker locker(&m_mutex);
/*!!!
    while (!m_synthMap.empty()) {
	RealTimePluginInstance *instance = m_synthMap.begin()->second;
	m_synthMap.erase(m_synthMap.begin());
	delete instance;
    }
*/
}    

void
AudioGenerator::reset()
{
    QMutexLocker locker(&m_mutex);
/*!!!
    for (PluginMap::iterator i = m_synthMap.begin(); i != m_synthMap.end(); ++i) {
	if (i->second) {
	    i->second->silence();
	    i->second->discardEvents();
	}
    }
*/

    m_noteOffs.clear();
}

void
AudioGenerator::setTargetChannelCount(size_t targetChannelCount)
{
    if (m_targetChannelCount == targetChannelCount) return;

//    SVDEBUG << "AudioGenerator::setTargetChannelCount(" << targetChannelCount << ")" << endl;

    QMutexLocker locker(&m_mutex);
    m_targetChannelCount = targetChannelCount;

/*!!!
    for (PluginMap::iterator i = m_synthMap.begin(); i != m_synthMap.end(); ++i) {
	if (i->second) i->second->setIdealChannelCount(targetChannelCount);
    }
*/
}

size_t
AudioGenerator::getBlockSize() const
{
    return m_processingBlockSize;
}

void
AudioGenerator::setSoloModelSet(std::set<Model *> s)
{
    QMutexLocker locker(&m_mutex);

    m_soloModelSet = s;
    m_soloing = true;
}

void
AudioGenerator::clearSoloModelSet()
{
    QMutexLocker locker(&m_mutex);

    m_soloModelSet.clear();
    m_soloing = false;
}

size_t
AudioGenerator::mixModel(Model *model, size_t startFrame, size_t frameCount,
			 float **buffer, size_t fadeIn, size_t fadeOut)
{
    if (m_sourceSampleRate == 0) {
	cerr << "WARNING: AudioGenerator::mixModel: No base source sample rate available" << endl;
	return frameCount;
    }

    QMutexLocker locker(&m_mutex);

    Playable *playable = model;
    if (!playable || !playable->canPlay()) return frameCount;

    PlayParameters *parameters =
	PlayParameterRepository::getInstance()->getPlayParameters(playable);
    if (!parameters) return frameCount;

    bool playing = !parameters->isPlayMuted();
    if (!playing) {
#ifdef DEBUG_AUDIO_GENERATOR
        cout << "AudioGenerator::mixModel(" << model << "): muted" << endl;
#endif
        return frameCount;
    }

    if (m_soloing) {
        if (m_soloModelSet.find(model) == m_soloModelSet.end()) {
#ifdef DEBUG_AUDIO_GENERATOR
            cout << "AudioGenerator::mixModel(" << model << "): not one of the solo'd models" << endl;
#endif
            return frameCount;
        }
    }

    float gain = parameters->getPlayGain();
    float pan = parameters->getPlayPan();

    DenseTimeValueModel *dtvm = dynamic_cast<DenseTimeValueModel *>(model);
    if (dtvm) {
	return mixDenseTimeValueModel(dtvm, startFrame, frameCount,
				      buffer, gain, pan, fadeIn, fadeOut);
    }

    bool synthetic = 
        (qobject_cast<SparseOneDimensionalModel *>(model) ||
         qobject_cast<NoteModel *>(model) ||
         qobject_cast<FlexiNoteModel *>(model));

    if (synthetic) {
        return mixSyntheticNoteModel(model, startFrame, frameCount,
                                     buffer, gain, pan, fadeIn, fadeOut);
    }

    std::cerr << "AudioGenerator::mixModel: WARNING: Model " << model << " of type " << model->getTypeName() << " is marked as playable, but I have no mechanism to play it" << std::endl;

    return frameCount;
}

size_t
AudioGenerator::mixDenseTimeValueModel(DenseTimeValueModel *dtvm,
				       size_t startFrame, size_t frames,
				       float **buffer, float gain, float pan,
				       size_t fadeIn, size_t fadeOut)
{
    static float **channelBuffer = 0;
    static size_t  channelBufSiz = 0;
    static size_t  channelBufCount = 0;

    size_t totalFrames = frames + fadeIn/2 + fadeOut/2;

    size_t modelChannels = dtvm->getChannelCount();

    if (channelBufSiz < totalFrames || channelBufCount < modelChannels) {

        for (size_t c = 0; c < channelBufCount; ++c) {
            delete[] channelBuffer[c];
        }

	delete[] channelBuffer;
        channelBuffer = new float *[modelChannels];

        for (size_t c = 0; c < modelChannels; ++c) {
            channelBuffer[c] = new float[totalFrames];
        }

        channelBufCount = modelChannels;
	channelBufSiz = totalFrames;
    }

    size_t got = 0;

    if (startFrame >= fadeIn/2) {
        got = dtvm->getData(0, modelChannels - 1,
                            startFrame - fadeIn/2,
                            frames + fadeOut/2 + fadeIn/2,
                            channelBuffer);
    } else {
        size_t missing = fadeIn/2 - startFrame;

        for (size_t c = 0; c < modelChannels; ++c) {
            channelBuffer[c] += missing;
        }

        got = dtvm->getData(0, modelChannels - 1,
                            startFrame,
                            frames + fadeOut/2,
                            channelBuffer);

        for (size_t c = 0; c < modelChannels; ++c) {
            channelBuffer[c] -= missing;
        }

        got += missing;
    }	    

    for (size_t c = 0; c < m_targetChannelCount; ++c) {

	size_t sourceChannel = (c % modelChannels);

//	SVDEBUG << "mixing channel " << c << " from source channel " << sourceChannel << endl;

	float channelGain = gain;
	if (pan != 0.0) {
	    if (c == 0) {
		if (pan > 0.0) channelGain *= 1.0 - pan;
	    } else {
		if (pan < 0.0) channelGain *= pan + 1.0;
	    }
	}

	for (size_t i = 0; i < fadeIn/2; ++i) {
	    float *back = buffer[c];
	    back -= fadeIn/2;
	    back[i] += (channelGain * channelBuffer[sourceChannel][i] * i) / fadeIn;
	}

	for (size_t i = 0; i < frames + fadeOut/2; ++i) {
	    float mult = channelGain;
	    if (i < fadeIn/2) {
		mult = (mult * i) / fadeIn;
	    }
	    if (i > frames - fadeOut/2) {
		mult = (mult * ((frames + fadeOut/2) - i)) / fadeOut;
	    }
            float val = channelBuffer[sourceChannel][i];
            if (i >= got) val = 0.f;
	    buffer[c][i] += mult * val;
	}
    }

    return got;
}
  
size_t
AudioGenerator::mixSyntheticNoteModel(Model *model,
                                      size_t startFrame, size_t frames,
                                      float **buffer, float gain, float pan,
                                      size_t /* fadeIn */,
                                      size_t /* fadeOut */)
{
    RealTimePluginInstance *plugin = m_synthMap[model];
    if (!plugin) return 0;

    size_t latency = plugin->getLatency();
    size_t blocks = frames / m_processingBlockSize;
    
    //!!! hang on -- the fact that the audio callback play source's
    //buffer is a multiple of the plugin's buffer size doesn't mean
    //that we always get called for a multiple of it here (because it
    //also depends on the JACK block size).  how should we ensure that
    //all models write the same amount in to the mix, and that we
    //always have a multiple of the plugin buffer size?  I guess this
    //class has to be queryable for the plugin buffer size & the
    //callback play source has to use that as a multiple for all the
    //calls to mixModel

    size_t got = blocks * m_processingBlockSize;

#ifdef DEBUG_AUDIO_GENERATOR
    cout << "mixModel [synthetic note]: frames " << frames
	      << ", blocks " << blocks << endl;
#endif

    snd_seq_event_t onEv;
    onEv.type = SND_SEQ_EVENT_NOTEON;
    onEv.data.note.channel = 0;

    snd_seq_event_t offEv;
    offEv.type = SND_SEQ_EVENT_NOTEOFF;
    offEv.data.note.channel = 0;
    offEv.data.note.velocity = 0;
    
    NoteOffSet &noteOffs = m_noteOffs[model];

    for (size_t i = 0; i < blocks; ++i) {

	size_t reqStart = startFrame + i * m_processingBlockSize;

        NoteList notes;
        NoteExportable *exportable = dynamic_cast<NoteExportable *>(model);
        if (exportable) {
            notes = exportable->getNotes(reqStart + latency,
                                         reqStart + latency + m_processingBlockSize);
        }

        Vamp::RealTime blockTime = Vamp::RealTime::frame2RealTime
	    (startFrame + i * m_processingBlockSize, m_sourceSampleRate);

	for (NoteList::const_iterator ni = notes.begin();
             ni != notes.end(); ++ni) {

	    size_t noteFrame = ni->start;

	    if (noteFrame >= latency) noteFrame -= latency;

	    if (noteFrame < reqStart ||
		noteFrame >= reqStart + m_processingBlockSize) continue;

	    while (noteOffs.begin() != noteOffs.end() &&
		   noteOffs.begin()->frame <= noteFrame) {

                Vamp::RealTime eventTime = Vamp::RealTime::frame2RealTime
		    (noteOffs.begin()->frame, m_sourceSampleRate);

		offEv.data.note.note = noteOffs.begin()->pitch;

#ifdef DEBUG_AUDIO_GENERATOR
		cerr << "mixModel [synthetic]: sending note-off event at time " << eventTime << " frame " << noteOffs.begin()->frame << " pitch " << noteOffs.begin()->pitch << endl;
#endif

		plugin->sendEvent(eventTime, &offEv);
		noteOffs.erase(noteOffs.begin());
	    }

            Vamp::RealTime eventTime = Vamp::RealTime::frame2RealTime
		(noteFrame, m_sourceSampleRate);
	    
            if (ni->isMidiPitchQuantized) {
                onEv.data.note.note = ni->midiPitch;
            } else {
#ifdef DEBUG_AUDIO_GENERATOR
                cerr << "mixModel [synthetic]: non-pitch-quantized notes are not supported [yet], quantizing" << endl;
#endif
                onEv.data.note.note = Pitch::getPitchForFrequency(ni->frequency);
            }

            onEv.data.note.velocity = ni->velocity;

	    plugin->sendEvent(eventTime, &onEv);

#ifdef DEBUG_AUDIO_GENERATOR
	    cout << "mixModel [synthetic]: note at frame " << noteFrame << ", block start " << (startFrame + i * m_processingBlockSize) << ", resulting time " << eventTime << endl;
#endif
	    
	    noteOffs.insert
                (NoteOff(onEv.data.note.note, noteFrame + ni->duration));
	}

	while (noteOffs.begin() != noteOffs.end() &&
	       noteOffs.begin()->frame <=
	       startFrame + i * m_processingBlockSize + m_processingBlockSize) {

            Vamp::RealTime eventTime = Vamp::RealTime::frame2RealTime
		(noteOffs.begin()->frame, m_sourceSampleRate);

	    offEv.data.note.note = noteOffs.begin()->pitch;

#ifdef DEBUG_AUDIO_GENERATOR
            cerr << "mixModel [synthetic]: sending leftover note-off event at time " << eventTime << " frame " << noteOffs.begin()->frame << " pitch " << noteOffs.begin()->pitch << endl;
#endif

	    plugin->sendEvent(eventTime, &offEv);
	    noteOffs.erase(noteOffs.begin());
	}
	
	plugin->run(blockTime);
	float **outs = plugin->getAudioOutputBuffers();

	for (size_t c = 0; c < m_targetChannelCount; ++c) {
#ifdef DEBUG_AUDIO_GENERATOR
	    cout << "mixModel [synthetic]: adding " << m_processingBlockSize << " samples from plugin output " << c << endl;
#endif

	    size_t sourceChannel = (c % plugin->getAudioOutputCount());

	    float channelGain = gain;
	    if (pan != 0.0) {
		if (c == 0) {
		    if (pan > 0.0) channelGain *= 1.0 - pan;
		} else {
		    if (pan < 0.0) channelGain *= pan + 1.0;
		}
	    }

	    for (size_t j = 0; j < m_processingBlockSize; ++j) {
		buffer[c][i * m_processingBlockSize + j] +=
		    channelGain * outs[sourceChannel][j];
	    }
	}
    }

    return got;
}

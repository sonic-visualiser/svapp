/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005
    
    This is experimental software.  Not for distribution.
*/

#include "AudioGenerator.h"

#include "base/ViewManager.h"
#include "base/PlayParameters.h"

#include "model/DenseTimeValueModel.h"
#include "model/SparseOneDimensionalModel.h"

#include "plugin/RealTimePluginFactory.h"
#include "plugin/RealTimePluginInstance.h"
#include "plugin/PluginIdentifier.h"
#include "plugin/api/alsa/seq_event.h"

#include <iostream>

const size_t
AudioGenerator::m_pluginBlockSize = 2048;

// #define DEBUG_AUDIO_GENERATOR 1

AudioGenerator::AudioGenerator(ViewManager *manager) :
    m_viewManager(manager),
    m_sourceSampleRate(0),
    m_targetChannelCount(1)
{
}

AudioGenerator::~AudioGenerator()
{
}

void
AudioGenerator::addModel(Model *model)
{
    if (m_sourceSampleRate == 0) {

	m_sourceSampleRate = model->getSampleRate();

    } else {

	DenseTimeValueModel *dtvm =
	    dynamic_cast<DenseTimeValueModel *>(model);

	if (dtvm) {
	    m_sourceSampleRate = model->getSampleRate();
	}
    }

    SparseOneDimensionalModel *sodm =
	dynamic_cast<SparseOneDimensionalModel *>(model);
    if (!sodm) return; // nothing else to initialise

//	QString pluginId = "dssi:/usr/lib/dssi/dssi-vst.so:FEARkILLERrev1.dll";
//	QString pluginId = "dssi:/usr/lib/dssi/hexter.so:hexter";
//	QString pluginId = "dssi:/usr/lib/dssi/sineshaper.so:sineshaper";
//	QString pluginId = "dssi:/usr/local/lib/dssi/xsynth-dssi.so:Xsynth";
//	QString pluginId = "dssi:/usr/local/lib/dssi/trivial_synth.so:TS";
    QString pluginId = QString("dssi:%1:sample_player").
	arg(PluginIdentifier::BUILTIN_PLUGIN_SONAME);
    RealTimePluginFactory *factory =
	RealTimePluginFactory::instanceFor(pluginId);
    
    if (!factory) {
	std::cerr << "Failed to get plugin factory" << std::endl;
	return;
    }
	
    RealTimePluginInstance *instance =
	factory->instantiatePlugin
	(pluginId, 0, 0, m_sourceSampleRate, m_pluginBlockSize, m_targetChannelCount);

    if (instance) {
	m_synthMap[sodm] = instance;
	for (unsigned int i = 0; i < instance->getParameterCount(); ++i) {
	    instance->setParameterValue(i, instance->getParameterDefault(i));
	}
	QString program = instance->getProgram(0, 0);
	if (program != "") {
	    std::cerr << "selecting program " << program.toLocal8Bit().data() << std::endl;
	    instance->selectProgram(program);
	}
	instance->selectProgram("cowbell"); //!!!
	instance->setIdealChannelCount(m_targetChannelCount); // reset!
    } else {
	std::cerr << "Failed to instantiate plugin" << std::endl;
    }
}

void
AudioGenerator::removeModel(Model *model)
{
    SparseOneDimensionalModel *sodm =
	dynamic_cast<SparseOneDimensionalModel *>(model);
    if (!sodm) return; // nothing to do

    if (m_synthMap.find(sodm) == m_synthMap.end()) return;

    RealTimePluginInstance *instance = m_synthMap[sodm];
    m_synthMap.erase(sodm);
    delete instance;
}

void
AudioGenerator::clearModels()
{
    while (!m_synthMap.empty()) {
	RealTimePluginInstance *instance = m_synthMap.begin()->second;
	m_synthMap.erase(m_synthMap.begin());
	delete instance;
    }
}    

void
AudioGenerator::reset()
{
    for (PluginMap::iterator i = m_synthMap.begin(); i != m_synthMap.end(); ++i) {
	if (i->second) {
	    i->second->silence();
	    i->second->discardEvents();
	}
    }

    m_noteOffs.clear();
}

void
AudioGenerator::setTargetChannelCount(size_t targetChannelCount)
{
    m_targetChannelCount = targetChannelCount;

    for (PluginMap::iterator i = m_synthMap.begin(); i != m_synthMap.end(); ++i) {
	if (i->second) i->second->setIdealChannelCount(targetChannelCount);
    }
}

size_t
AudioGenerator::getBlockSize() const
{
    return m_pluginBlockSize;
}

size_t
AudioGenerator::mixModel(Model *model, size_t startFrame, size_t frameCount,
			 float **buffer)
{
    if (m_sourceSampleRate == 0) {
	std::cerr << "WARNING: AudioGenerator::mixModel: No base source sample rate available" << std::endl;
	return frameCount;
    }

    PlayParameters *parameters = m_viewManager->getPlayParameters(model);
    if (!parameters) return frameCount;

    bool playing = !parameters->isPlayMuted();
    if (!playing) return frameCount;

    float gain = parameters->getPlayGain();
    float pan = parameters->getPlayPan();

    DenseTimeValueModel *dtvm = dynamic_cast<DenseTimeValueModel *>(model);
    if (dtvm) {
	return mixDenseTimeValueModel(dtvm, startFrame, frameCount,
				      buffer, gain, pan);
    }

    SparseOneDimensionalModel *sodm = dynamic_cast<SparseOneDimensionalModel *>
	(model);
    if (sodm) {
	return mixSparseOneDimensionalModel(sodm, startFrame, frameCount,
					    buffer, gain, pan);
    }

    return frameCount;
}

size_t
AudioGenerator::mixDenseTimeValueModel(DenseTimeValueModel *dtvm,
				       size_t startFrame, size_t frames,
				       float **buffer, float gain, float pan)
{
    static float *channelBuffer = 0;
    static size_t channelBufSiz = 0;
    
    if (channelBufSiz < frames) {
	delete[] channelBuffer;
	channelBuffer = new float[frames];
	channelBufSiz = frames;
    }
    
    size_t got = 0;

    for (size_t c = 0; c < m_targetChannelCount && c < dtvm->getChannelCount(); ++c) {
	got = dtvm->getValues(c, startFrame, startFrame + frames, channelBuffer);
	for (size_t i = 0; i < frames; ++i) {
	    buffer[c][i] += gain * channelBuffer[i];
	}
    }

    return got;
}
    
size_t
AudioGenerator::mixSparseOneDimensionalModel(SparseOneDimensionalModel *sodm,
					     size_t startFrame, size_t frames,
					     float **buffer, float gain, float pan)
{
    RealTimePluginInstance *plugin = m_synthMap[sodm];
    if (!plugin) return 0;

    size_t latency = plugin->getLatency();
    size_t blocks = frames / m_pluginBlockSize;
    
    //!!! hang on -- the fact that the audio callback play source's
    //buffer is a multiple of the plugin's buffer size doesn't mean
    //that we always get called for a multiple of it here (because it
    //also depends on the JACK block size).  how should we ensure that
    //all models write the same amount in to the mix, and that we
    //always have a multiple of the plugin buffer size?  I guess this
    //class has to be queryable for the plugin buffer size & the
    //callback play source has to use that as a multiple for all the
    //calls to mixModel

    size_t got = blocks * m_pluginBlockSize;

#ifdef DEBUG_AUDIO_GENERATOR
    std::cout << "mixModel [sparse]: frames " << frames
	      << ", blocks " << blocks << std::endl;
#endif

    snd_seq_event_t onEv;
    onEv.type = SND_SEQ_EVENT_NOTEON;
    onEv.data.note.channel = 0;
    onEv.data.note.note = 64;
    onEv.data.note.velocity = 127;

    snd_seq_event_t offEv;
    offEv.type = SND_SEQ_EVENT_NOTEOFF;
    offEv.data.note.channel = 0;
    offEv.data.note.velocity = 0;
    
    NoteOffSet &noteOffs = m_noteOffs[sodm];

    for (size_t i = 0; i < blocks; ++i) {

	size_t reqStart = startFrame + i * m_pluginBlockSize;

	SparseOneDimensionalModel::PointList points =
	    sodm->getPoints(reqStart > 0 ? reqStart + latency : reqStart,
			    reqStart + latency + m_pluginBlockSize);

	RealTime blockTime = RealTime::frame2RealTime
	    (startFrame + i * m_pluginBlockSize, m_sourceSampleRate);

	for (SparseOneDimensionalModel::PointList::iterator pli =
		 points.begin(); pli != points.end(); ++pli) {

	    size_t pliFrame = pli->frame;
	    if (pliFrame >= latency) pliFrame -= latency;

	    while (noteOffs.begin() != noteOffs.end() &&
		   noteOffs.begin()->frame <= pliFrame) {

		RealTime eventTime = RealTime::frame2RealTime
		    (noteOffs.begin()->frame, m_sourceSampleRate);

		offEv.data.note.note = noteOffs.begin()->pitch;
		plugin->sendEvent(eventTime, &offEv);
		noteOffs.erase(noteOffs.begin());
	    }

	    RealTime eventTime = RealTime::frame2RealTime
		(pliFrame, m_sourceSampleRate);
	    
	    plugin->sendEvent(eventTime, &onEv);

#ifdef DEBUG_AUDIO_GENERATOR
	    std::cout << "mixModel [sparse]: point at frame " << pliFrame << ", block start " << (startFrame + i * m_pluginBlockSize) << ", resulting time " << eventTime << std::endl;
#endif
	    
	    size_t duration = 7000; // frames [for now]
	    NoteOff noff;
	    noff.pitch = onEv.data.note.note;
	    noff.frame = pliFrame + duration;
	    noteOffs.insert(noff);
	}

	while (noteOffs.begin() != noteOffs.end() &&
	       noteOffs.begin()->frame <=
	       startFrame + i * m_pluginBlockSize + m_pluginBlockSize) {

	    RealTime eventTime = RealTime::frame2RealTime
		(noteOffs.begin()->frame, m_sourceSampleRate);

	    offEv.data.note.note = noteOffs.begin()->pitch;
	    plugin->sendEvent(eventTime, &offEv);
	    noteOffs.erase(noteOffs.begin());
	}
	
	plugin->run(blockTime);
	float **outs = plugin->getAudioOutputBuffers();

	for (size_t c = 0; c < m_targetChannelCount && c < plugin->getAudioOutputCount(); ++c) {
#ifdef DEBUG_AUDIO_GENERATOR
	    std::cout << "mixModel [sparse]: adding " << m_pluginBlockSize << " samples from plugin output " << c << std::endl;
#endif

	    for (size_t j = 0; j < m_pluginBlockSize; ++j) {
		buffer[c][i * m_pluginBlockSize + j] += gain * outs[c][j];
	    }
	}
    }

    return got;
}


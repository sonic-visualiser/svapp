/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005
    
    This is experimental software.  Not for distribution.
*/

#include "AudioCallbackPlaySource.h"

#include "AudioGenerator.h"

#include "base/Model.h"
#include "base/ViewManager.h"
#include "model/DenseTimeValueModel.h"
#include "model/SparseOneDimensionalModel.h"
#include "dsp/timestretching/IntegerTimeStretcher.h"

#include <iostream>

//#define DEBUG_AUDIO_PLAY_SOURCE 1

//const size_t AudioCallbackPlaySource::m_ringBufferSize = 102400;
const size_t AudioCallbackPlaySource::m_ringBufferSize = 131071;

AudioCallbackPlaySource::AudioCallbackPlaySource(ViewManager *manager) :
    m_viewManager(manager),
    m_audioGenerator(new AudioGenerator(manager)),
    m_bufferCount(0),
    m_blockSize(1024),
    m_sourceSampleRate(0),
    m_targetSampleRate(0),
    m_playLatency(0),
    m_playing(false),
    m_exiting(false),
    m_bufferedToFrame(0),
    m_outputLeft(0.0),
    m_outputRight(0.0),
    m_slowdownCounter(0),
    m_timeStretcher(0),
    m_fillThread(0),
    m_converter(0)
{
    // preallocate some slots, to avoid reallocation in an
    // un-thread-safe manner later
    while (m_buffers.size() < 20) m_buffers.push_back(0);

    m_viewManager->setAudioPlaySource(this);
}

AudioCallbackPlaySource::~AudioCallbackPlaySource()
{
    m_exiting = true;

    if (m_fillThread) {
	m_condition.wakeAll();
	m_fillThread->wait();
	delete m_fillThread;
    }

    clearModels();
}

void
AudioCallbackPlaySource::addModel(Model *model)
{
    m_mutex.lock();

    m_models.insert(model);

    bool buffersChanged = false, srChanged = false;

    if (m_sourceSampleRate == 0) {

	m_sourceSampleRate = model->getSampleRate();
	srChanged = true;

    } else if (model->getSampleRate() != m_sourceSampleRate) {
	std::cerr << "AudioCallbackPlaySource::addModel: ERROR: "
		  << "New model sample rate does not match" << std::endl
		  << "existing model(s) (new " << model->getSampleRate()
		  << " vs " << m_sourceSampleRate
		  << "), playback will be wrong"
		  << std::endl;
    }

    size_t sz = m_ringBufferSize;
    if (m_bufferCount > 0) {
	sz = m_buffers[0]->getSize();
    }

    size_t modelChannels = 1;
    DenseTimeValueModel *dtvm = dynamic_cast<DenseTimeValueModel *>(model);
    if (dtvm) modelChannels = dtvm->getChannelCount();

    while (m_bufferCount < modelChannels) {

	if (m_buffers.size() < modelChannels) {
	    // This is a hideously chancy operation -- the RT thread
	    // could be using this vector.  We allocated several slots
	    // in the ctor to avoid exactly this, but if we ever end
	    // up with more channels than that (!) then we're just
	    // going to have to risk it
	    m_buffers.push_back(new RingBuffer<float>(sz));

	} else {
	    // The usual case
	    m_buffers[m_bufferCount] = new RingBuffer<float>(sz);
	}

	++m_bufferCount;
	buffersChanged = true;
    }

    if (buffersChanged) {
	m_audioGenerator->setTargetChannelCount(m_bufferCount);
    }

    if (buffersChanged || srChanged) {

	if (m_converter) {
	    src_delete(m_converter);
	    m_converter = 0;
	}

	if (getSourceSampleRate() != getTargetSampleRate()) {

	    int err = 0;
	    m_converter = src_new(SRC_SINC_FASTEST, m_bufferCount, &err);
	    if (!m_converter) {
		std::cerr
		    << "AudioCallbackPlaySource::setModel: ERROR in creating samplerate converter: "
		    << src_strerror(err) << std::endl;
	    }
	}
    }

    m_audioGenerator->addModel(model);

    m_mutex.unlock();

    if (!m_fillThread) {
	m_fillThread = new AudioCallbackPlaySourceFillThread(*this);
	m_fillThread->start();
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    std::cerr << "AudioCallbackPlaySource::addModel: emitting modelReplaced" << std::endl;
#endif
    emit modelReplaced();

    if (srChanged && (getSourceSampleRate() != getTargetSampleRate())) {
	emit sampleRateMismatch(getSourceSampleRate(), getTargetSampleRate());
    }
}

void
AudioCallbackPlaySource::removeModel(Model *model)
{
    m_mutex.lock();

    m_models.erase(model);

    if (m_models.empty()) {
	if (m_converter) {
	    src_delete(m_converter);
	    m_converter = 0;
	}
	m_sourceSampleRate = 0;
    }

    m_audioGenerator->removeModel(model);

    m_mutex.unlock();
}

void
AudioCallbackPlaySource::clearModels()
{
    m_mutex.lock();

    m_models.clear();

    if (m_converter) {
	src_delete(m_converter);
	m_converter = 0;
    }

    m_audioGenerator->clearModels();

    m_sourceSampleRate = 0;

    m_mutex.unlock();
}    

void
AudioCallbackPlaySource::play(size_t startFrame)
{
    // The fill thread will automatically empty its buffers before
    // starting again if we have not so far been playing, but not if
    // we're just re-seeking.

    if (m_playing) {
	m_mutex.lock();
	m_bufferedToFrame = startFrame;
	for (size_t c = 0; c < m_bufferCount; ++c) {
	    getRingBuffer(c).reset();
	    if (m_converter) src_reset(m_converter);
	}
	m_mutex.unlock();
    } else {
	m_bufferedToFrame = startFrame;
    }

    m_audioGenerator->reset();

    m_playing = true;
    m_condition.wakeAll();
}

void
AudioCallbackPlaySource::stop()
{
    m_playing = false;
    m_condition.wakeAll();
}

void
AudioCallbackPlaySource::setTargetBlockSize(size_t size)
{
    std::cerr << "AudioCallbackPlaySource::setTargetBlockSize() -> " << size << std::endl;
    m_blockSize = size;
    for (size_t i = 0; i < m_bufferCount; ++i) {
	getRingBuffer(i).resize(m_ringBufferSize);
    }
}

size_t
AudioCallbackPlaySource::getTargetBlockSize() const
{
    std::cerr << "AudioCallbackPlaySource::getTargetBlockSize() -> " << m_blockSize << std::endl;
    return m_blockSize;
}

void
AudioCallbackPlaySource::setTargetPlayLatency(size_t latency)
{
    m_playLatency = latency;
}

size_t
AudioCallbackPlaySource::getTargetPlayLatency() const
{
    return m_playLatency;
}

size_t
AudioCallbackPlaySource::getCurrentPlayingFrame()
{
    bool resample = false;
    double ratio = 1.0;

    if (getSourceSampleRate() != getTargetSampleRate()) {
	resample = true;
	ratio = double(getSourceSampleRate()) / double(getTargetSampleRate());
    }

    size_t readSpace = 0;
    for (size_t c = 0; c < getSourceChannelCount(); ++c) {
	size_t spaceHere = getRingBuffer(c).getReadSpace();
	if (c == 0 || spaceHere < readSpace) readSpace = spaceHere;
    }

    if (resample) {
	readSpace = size_t(readSpace * ratio + 0.1);
    }

    size_t lastRequestedFrame = 0;
    if (m_bufferedToFrame > readSpace) {
	lastRequestedFrame = m_bufferedToFrame - readSpace;
    }

    size_t framePlaying = lastRequestedFrame;

    size_t latency = m_playLatency;
    if (resample) latency = size_t(m_playLatency * ratio + 0.1);
    
    TimeStretcherData *timeStretcher = m_timeStretcher;
    if (timeStretcher) {
	latency += timeStretcher->getStretcher(0)->getProcessingLatency();
    }

    if (framePlaying > latency) {
	framePlaying = framePlaying - latency;
    } else {
	framePlaying = 0;
    }

#ifdef DEBUG_AUDIO_PLAY_SOURCE
    std::cout << "getCurrentPlayingFrame: readSpace " << readSpace << ", lastRequestedFrame " << lastRequestedFrame << ", framePlaying " << framePlaying << ", latency " << latency << std::endl;
#endif

    return framePlaying;
}

void
AudioCallbackPlaySource::setOutputLevels(float left, float right)
{
    m_outputLeft = left;
    m_outputRight = right;
}

bool
AudioCallbackPlaySource::getOutputLevels(float &left, float &right)
{
    left = m_outputLeft;
    right = m_outputRight;
    return true;
}

void
AudioCallbackPlaySource::setTargetSampleRate(size_t sr)
{
    m_targetSampleRate = sr;
}

size_t
AudioCallbackPlaySource::getTargetSampleRate() const
{
    if (m_targetSampleRate) return m_targetSampleRate;
    else return getSourceSampleRate();
}

size_t
AudioCallbackPlaySource::getSourceChannelCount() const
{
    return m_bufferCount;
}

size_t
AudioCallbackPlaySource::getSourceSampleRate() const
{
    return m_sourceSampleRate;
}

AudioCallbackPlaySource::TimeStretcherData::TimeStretcherData(size_t channels,
							      size_t factor,
							      size_t blockSize) :
    m_factor(factor),
    m_blockSize(blockSize)
{
    std::cerr << "TimeStretcherData::TimeStretcherData(" << channels << ", " << factor << ", " << blockSize << ")" << std::endl;

    for (size_t ch = 0; ch < channels; ++ch) {
	m_stretcher[ch] = StretcherBuffer
	    //!!! We really need to measure performance and work out
	    //what sort of quality level to use -- or at least to
	    //allow the user to configure it
	    (new IntegerTimeStretcher(factor, blockSize, 128),
	     new double[blockSize * factor]);
    }
    m_stretchInputBuffer = new double[blockSize];
}

AudioCallbackPlaySource::TimeStretcherData::~TimeStretcherData()
{
    std::cerr << "IntegerTimeStretcher::~IntegerTimeStretcher" << std::endl;

    while (!m_stretcher.empty()) {
	delete m_stretcher.begin()->second.first;
	delete[] m_stretcher.begin()->second.second;
	m_stretcher.erase(m_stretcher.begin());
    }
    delete m_stretchInputBuffer;
}

IntegerTimeStretcher *
AudioCallbackPlaySource::TimeStretcherData::getStretcher(size_t channel)
{
    return m_stretcher[channel].first;
}

double *
AudioCallbackPlaySource::TimeStretcherData::getOutputBuffer(size_t channel)
{
    return m_stretcher[channel].second;
}

double *
AudioCallbackPlaySource::TimeStretcherData::getInputBuffer()
{
    return m_stretchInputBuffer;
}

void
AudioCallbackPlaySource::TimeStretcherData::run(size_t channel)
{
    getStretcher(channel)->process(getInputBuffer(),
				   getOutputBuffer(channel),
				   m_blockSize);
}

void
AudioCallbackPlaySource::setSlowdownFactor(size_t factor)
{
    // Avoid locks -- create, assign, mark old one for scavenging
    // later (as a call to getSourceSamples may still be using it)

    TimeStretcherData *existingStretcher = m_timeStretcher;

    if (existingStretcher && existingStretcher->getFactor() == factor) {
	return;
    }

    if (factor > 1) {
	TimeStretcherData *newStretcher = new TimeStretcherData
	    (getSourceChannelCount(), factor, getTargetBlockSize());
	m_slowdownCounter = 0;
	m_timeStretcher = newStretcher;
    } else {
	m_timeStretcher = 0;
    }

    if (existingStretcher) {
	m_timeStretcherScavenger.claim(existingStretcher);
    }
}
	    
size_t
AudioCallbackPlaySource::getSourceSamples(size_t count, float **buffer)
{
    if (!m_playing) {
	for (size_t ch = 0; ch < getSourceChannelCount(); ++ch) {
	    for (size_t i = 0; i < count; ++i) {
		buffer[ch][i] = 0.0;
	    }
	}
	return 0;
    }

    TimeStretcherData *timeStretcher = m_timeStretcher;

    if (!timeStretcher || timeStretcher->getFactor() == 1) {

	size_t got = 0;

	for (size_t ch = 0; ch < getSourceChannelCount(); ++ch) {

	    RingBuffer<float> &rb = *m_buffers[ch];

	    // this is marginally more likely to leave our channels in
	    // sync after a processing failure than just passing "count":
	    size_t request = count;
	    if (ch > 0) request = got;

	    got = rb.read(buffer[ch], request);
	    
#ifdef DEBUG_AUDIO_PLAY_SOURCE
	    std::cout << "AudioCallbackPlaySource::getSamples: got " << got << " samples on channel " << ch << ", signalling for more (possibly)" << std::endl;
#endif
	}

	for (size_t ch = 0; ch < getSourceChannelCount(); ++ch) {
	    for (size_t i = got; i < count; ++i) {
		buffer[ch][i] = 0.0;
	    }
	}

        m_condition.wakeAll();
	return got;
    }

    if (m_slowdownCounter == 0) {

	size_t got = 0;
	double *ib = timeStretcher->getInputBuffer();

	for (size_t ch = 0; ch < getSourceChannelCount(); ++ch) {

	    RingBuffer<float> &rb = *m_buffers[ch];
	    size_t request = count;
	    if (ch > 0) request = got; // see above
	    got = rb.read(buffer[ch], request);

#ifdef DEBUG_AUDIO_PLAY_SOURCE
	    std::cout << "AudioCallbackPlaySource::getSamples: got " << got << " samples on channel " << ch << ", running time stretcher" << std::endl;
#endif

	    for (size_t i = 0; i < count; ++i) {
		ib[i] = buffer[ch][i];
	    }
	    
	    timeStretcher->run(ch);
	}

    } else if (m_slowdownCounter >= timeStretcher->getFactor()) {
	// reset this in case the factor has changed leaving the
	// counter out of range
	m_slowdownCounter = 0;
    }

    for (size_t ch = 0; ch < getSourceChannelCount(); ++ch) {

	double *ob = timeStretcher->getOutputBuffer(ch);

#ifdef DEBUG_AUDIO_PLAY_SOURCE
	std::cerr << "AudioCallbackPlaySource::getSamples: Copying from (" << (m_slowdownCounter * count) << "," << count << ") to buffer" << std::endl;
#endif

	for (size_t i = 0; i < count; ++i) {
	    buffer[ch][i] = ob[m_slowdownCounter * count + i];
	}
    }

    if (m_slowdownCounter == 0) m_condition.wakeAll();
    m_slowdownCounter = (m_slowdownCounter + 1) % timeStretcher->getFactor();
    return count;
}

void
AudioCallbackPlaySource::fillBuffers()
{
    static float *tmp = 0;
    static size_t tmpSize = 0;

    size_t space = 0;
    for (size_t c = 0; c < m_bufferCount; ++c) {
	size_t spaceHere = getRingBuffer(c).getWriteSpace();
	if (c == 0 || spaceHere < space) space = spaceHere;
    }
    
    if (space == 0) return;
    
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    std::cout << "AudioCallbackPlaySourceFillThread: filling " << space << " frames" << std::endl;
#endif

    size_t f = m_bufferedToFrame;
	
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    std::cout << "buffered to " << f << " already" << std::endl;
#endif

    bool resample = (getSourceSampleRate() != getTargetSampleRate());
    size_t channels = getSourceChannelCount();
    size_t orig = space;
    size_t got = 0;

    static float **bufferPtrs = 0;
    static size_t bufferPtrCount = 0;

    if (bufferPtrCount < channels) {
	if (bufferPtrs) delete[] bufferPtrs;
	bufferPtrs = new float *[channels];
	bufferPtrCount = channels;
    }

    size_t generatorBlockSize = m_audioGenerator->getBlockSize();

    if (resample && m_converter) {

	double ratio =
	    double(getTargetSampleRate()) / double(getSourceSampleRate());
	orig = size_t(orig / ratio + 0.1);

	// orig must be a multiple of generatorBlockSize
	orig = (orig / generatorBlockSize) * generatorBlockSize;
	if (orig == 0) return;

	size_t work = std::max(orig, space);

	// We only allocate one buffer, but we use it in two halves.
	// We place the non-interleaved values in the second half of
	// the buffer (orig samples for channel 0, orig samples for
	// channel 1 etc), and then interleave them into the first
	// half of the buffer.  Then we resample back into the second
	// half (interleaved) and de-interleave the results back to
	// the start of the buffer for insertion into the ringbuffers.
	// What a faff -- especially as we've already de-interleaved
	// the audio data from the source file elsewhere before we
	// even reach this point.
	
	if (tmpSize < channels * work * 2) {
	    delete[] tmp;
	    tmp = new float[channels * work * 2];
	    tmpSize = channels * work * 2;
	}

	float *nonintlv = tmp + channels * work;
	float *intlv = tmp;
	float *srcout = tmp + channels * work;
	
	for (size_t c = 0; c < channels; ++c) {
	    for (size_t i = 0; i < orig; ++i) {
		nonintlv[channels * i + c] = 0.0f;
	    }
	}

	for (std::set<Model *>::iterator mi = m_models.begin();
	     mi != m_models.end(); ++mi) {

	    for (size_t c = 0; c < channels; ++c) {
		bufferPtrs[c] = nonintlv + c * orig;
	    }
	    
	    size_t gotHere = m_audioGenerator->mixModel
		(*mi, f, orig, bufferPtrs);

	    got = std::max(got, gotHere);
	}

	// and interleave into first half
	for (size_t c = 0; c < channels; ++c) {
	    for (size_t i = 0; i < orig; ++i) {
		float sample = 0;
		if (i < got) {
		    sample = nonintlv[c * orig + i];
		}
		intlv[channels * i + c] = sample;
	    }
	}
		
	SRC_DATA data;
	data.data_in = intlv;
	data.data_out = srcout;
	data.input_frames = orig;
	data.output_frames = work;
	data.src_ratio = ratio;
	data.end_of_input = 0;
	
	int err = src_process(m_converter, &data);
	size_t toCopy = size_t(work * ratio + 0.1);
	
	if (err) {
	    std::cerr
		<< "AudioCallbackPlaySourceFillThread: ERROR in samplerate conversion: "
		<< src_strerror(err) << std::endl;
	    //!!! Then what?
	} else {
	    got = data.input_frames_used;
	    toCopy = data.output_frames_gen;
#ifdef DEBUG_AUDIO_PLAY_SOURCE
	    std::cerr << "Resampled " << got << " frames to " << toCopy << " frames" << std::endl;
#endif
	}
	
	for (size_t c = 0; c < channels; ++c) {
	    for (size_t i = 0; i < toCopy; ++i) {
		tmp[i] = srcout[channels * i + c];
	    }
	    getRingBuffer(c).write(tmp, toCopy);
	}
	
    } else {

	// space must be a multiple of generatorBlockSize
	space = (space / generatorBlockSize) * generatorBlockSize;
	if (space == 0) return;

	if (tmpSize < channels * space) {
	    delete[] tmp;
	    tmp = new float[channels * space];
	    tmpSize = channels * space;
	}

	for (size_t c = 0; c < channels; ++c) {

	    bufferPtrs[c] = tmp + c * space;

	    for (size_t i = 0; i < space; ++i) {
		tmp[c * space + i] = 0.0f;
	    }
	}

	for (std::set<Model *>::iterator mi = m_models.begin();
	     mi != m_models.end(); ++mi) {

	    got = m_audioGenerator->mixModel
		(*mi, f, space, bufferPtrs);
	}

	for (size_t c = 0; c < channels; ++c) {

	    got = getRingBuffer(c).write(bufferPtrs[c], space);

#ifdef DEBUG_AUDIO_PLAY_SOURCE
	    std::cerr << "Wrote " << got << " frames for ch " << c << ", now "
		      << getRingBuffer(c).getReadSpace() << " to read" 
		      << std::endl;
#endif
	}
    }
    
    m_bufferedToFrame = f + got;
}    

void
AudioCallbackPlaySource::AudioCallbackPlaySourceFillThread::run()
{
    AudioCallbackPlaySource &s(m_source);
    
#ifdef DEBUG_AUDIO_PLAY_SOURCE
    std::cerr << "AudioCallbackPlaySourceFillThread starting" << std::endl;
#endif

    s.m_mutex.lock();

    bool previouslyPlaying = s.m_playing;

    while (!s.m_exiting) {

	s.m_timeStretcherScavenger.scavenge();

	float ms = 100;
	if (s.getSourceSampleRate() > 0) {
	    ms = float(m_ringBufferSize) / float(s.getSourceSampleRate()) * 1000.0;
	}

	if (!s.m_playing) ms *= 10;

#ifdef DEBUG_AUDIO_PLAY_SOURCE
	std::cout << "AudioCallbackPlaySourceFillThread: waiting for " << ms/4 << "ms..." << std::endl;
#endif

	s.m_condition.wait(&s.m_mutex, size_t(ms / 4));

#ifdef DEBUG_AUDIO_PLAY_SOURCE
	std::cout << "AudioCallbackPlaySourceFillThread: awoken" << std::endl;
#endif

	if (!s.getSourceSampleRate()) continue;

	bool playing = s.m_playing;

	if (playing && !previouslyPlaying) {
#ifdef DEBUG_AUDIO_PLAY_SOURCE
	    std::cout << "AudioCallbackPlaySourceFillThread: playback state changed, resetting" << std::endl;
#endif
	    for (size_t c = 0; c < s.getSourceChannelCount(); ++c) {
		s.getRingBuffer(c).reset();
	    }
	}
	previouslyPlaying = playing;

	if (!playing) continue;

	s.fillBuffers();
    }

    s.m_mutex.unlock();
}



#ifdef INCLUDE_MOCFILES
#include "AudioCallbackPlaySource.moc.cpp"
#endif


/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005
    
    This is experimental software.  Not for distribution.
*/

#include "AudioCallbackPlayTarget.h"
#include "AudioCallbackPlaySource.h"

#include <iostream>

AudioCallbackPlayTarget::AudioCallbackPlayTarget(AudioCallbackPlaySource *source) :
    m_source(source),
    m_outputGain(1.0)
{
    if (m_source) {
	connect(m_source, SIGNAL(modelReplaced()),
		this, SLOT(sourceModelReplaced()));
    }
}

AudioCallbackPlayTarget::~AudioCallbackPlayTarget()
{
}

void
AudioCallbackPlayTarget::setOutputGain(float gain)
{
    m_outputGain = gain;
}

#ifdef INCLUDE_MOCFILES
#ifdef INCLUDE_MOCFILES
#include "AudioCallbackPlayTarget.moc.cpp"
#endif
#endif


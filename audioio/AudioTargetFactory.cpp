/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005
    
    This is experimental software.  Not for distribution.
*/

#include "AudioTargetFactory.h"

#include "AudioJACKTarget.h"
#include "AudioCoreAudioTarget.h"
#include "AudioPortAudioTarget.h"

#include <iostream>

AudioCallbackPlayTarget *
AudioTargetFactory::createCallbackTarget(AudioCallbackPlaySource *source)
{
    AudioCallbackPlayTarget *target = 0;

#ifdef HAVE_JACK
    target = new AudioJACKTarget(source);
    if (target->isOK()) return target;
    else {
	std::cerr << "WARNING: AudioTargetFactory::createCallbackTarget: Failed to open JACK target" << std::endl;
	delete target;
    }
#endif

#ifdef HAVE_COREAUDIO
    target = new AudioCoreAudioTarget(source);
    if (target->isOK()) return target;
    else {
	std::cerr << "WARNING: AudioTargetFactory::createCallbackTarget: Failed to open CoreAudio target" << std::endl;
	delete target;
    }
#endif

#ifdef HAVE_DIRECTSOUND
    target = new AudioDirectSoundTarget(source);
    if (target->isOK()) return target;
    else {
	std::cerr << "WARNING: AudioTargetFactory::createCallbackTarget: Failed to open DirectSound target" << std::endl;
	delete target;
    }
#endif

#ifdef HAVE_PORTAUDIO
    target = new AudioPortAudioTarget(source);
    if (target->isOK()) return target;
    else {
	std::cerr << "WARNING: AudioTargetFactory::createCallbackTarget: Failed to open PortAudio target" << std::endl;
	delete target;
    }
#endif

    std::cerr << "WARNING: AudioTargetFactory::createCallbackTarget: No suitable targets available" << std::endl;
    return 0;
}



/* -*- c-basic-offset: 4 -*-  vi:set ts=8 sts=4 sw=4: */

/*
    A waveform viewer and audio annotation editor.
    Chris Cannam, Queen Mary University of London, 2005-2006
    
    This is experimental software.  Not for distribution.
*/

#ifndef _AUDIO_TARGET_FACTORY_H_
#define _AUDIO_TARGET_FACTORY_H_

class AudioCallbackPlaySource;
class AudioCallbackPlayTarget;

class AudioTargetFactory 
{
public:
    static AudioCallbackPlayTarget *createCallbackTarget(AudioCallbackPlaySource *);
};

#endif

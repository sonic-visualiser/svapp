
SVAPP_HEADERS += \
           align/Align.h \
           align/Aligner.h \
           align/ExternalProgramAligner.h \
           align/TransformAligner.h \
           audio/AudioCallbackPlaySource.h \
           audio/AudioCallbackRecordTarget.h \
           audio/AudioGenerator.h \
           audio/ClipMixer.h \
           audio/ContinuousSynth.h \
           audio/EffectWrapper.h \
           audio/PlaySpeedRangeMapper.h \
           audio/TimeStretchWrapper.h \
	   framework/Document.h \
           framework/MainWindowBase.h \
           framework/OSCScript.h \
           framework/SVFileReader.h \
           framework/TransformUserConfigurator.h \
           framework/VersionTester.h

SVAPP_SOURCES += \
	   align/Align.cpp \
           align/ExternalProgramAligner.cpp \
           align/TransformAligner.cpp \
           audio/AudioCallbackPlaySource.cpp \
           audio/AudioCallbackRecordTarget.cpp \
           audio/AudioGenerator.cpp \
           audio/ClipMixer.cpp \
           audio/ContinuousSynth.cpp \
           audio/EffectWrapper.cpp \
           audio/PlaySpeedRangeMapper.cpp \
           audio/TimeStretchWrapper.cpp \
	   framework/Document.cpp \
           framework/MainWindowBase.cpp \
           framework/SVFileReader.cpp \
           framework/TransformUserConfigurator.cpp \
           framework/VersionTester.cpp

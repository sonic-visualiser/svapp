TEMPLATE = lib

SV_UNIT_PACKAGES = fftw3f samplerate jack portaudio
load(../sv.prf)

CONFIG += sv staticlib qt thread warn_on stl rtti exceptions

TARGET = svaudioio

DEPENDPATH += ..
INCLUDEPATH += . ..
OBJECTS_DIR = tmp_obj
MOC_DIR = tmp_moc

HEADERS += AudioCallbackPlaySource.h \
           AudioCallbackPlayTarget.h \
           AudioCoreAudioTarget.h \
           AudioGenerator.h \
           AudioJACKTarget.h \
           AudioPortAudioTarget.h \
           AudioTargetFactory.h \
           PhaseVocoderTimeStretcher.h \
           PlaySpeedRangeMapper.h
SOURCES += AudioCallbackPlaySource.cpp \
           AudioCallbackPlayTarget.cpp \
           AudioCoreAudioTarget.cpp \
           AudioGenerator.cpp \
           AudioJACKTarget.cpp \
           AudioPortAudioTarget.cpp \
           AudioTargetFactory.cpp \
           PhaseVocoderTimeStretcher.cpp \
           PlaySpeedRangeMapper.cpp
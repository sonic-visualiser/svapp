
TEMPLATE = lib

exists(config.pri) {
    include(config.pri)
}
!exists(config.pri) {

    CONFIG += release
    DEFINES += NDEBUG BUILD_RELEASE NO_TIMING

    win32-g++ {
        INCLUDEPATH += ../sv-dependency-builds/win32-mingw/include
        LIBS += -L../sv-dependency-builds/win32-mingw/lib
    }
    win32-msvc* {
        INCLUDEPATH += ../sv-dependency-builds/win32-msvc/include
        LIBS += -L../sv-dependency-builds/win32-msvc/lib
    }
    macx* {
        INCLUDEPATH += ../sv-dependency-builds/osx/include
        LIBS += -L../sv-dependency-builds/osx/lib
    }

    win* {
        DEFINES += HAVE_PORTAUDIO_2_0
    }
    macx* {
        DEFINES += HAVE_COREAUDIO HAVE_PORTAUDIO_2_0
    }
}

CONFIG += staticlib qt thread warn_on stl rtti exceptions
QT += network xml gui widgets

TARGET = svapp

DEPENDPATH += . ../svcore ../svgui
INCLUDEPATH += . ../svcore ../svgui
OBJECTS_DIR = o
MOC_DIR = o

HEADERS += audioio/AudioCallbackPlaySource.h \
           audioio/AudioCallbackPlayTarget.h \
           audioio/AudioGenerator.h \
           audioio/AudioJACKTarget.h \
           audioio/AudioPortAudioTarget.h \
           audioio/AudioPulseAudioTarget.h \
           audioio/AudioTargetFactory.h \
           audioio/ClipMixer.h \
           audioio/ContinuousSynth.h \
           audioio/PlaySpeedRangeMapper.h

SOURCES += audioio/AudioCallbackPlaySource.cpp \
           audioio/AudioCallbackPlayTarget.cpp \
           audioio/AudioGenerator.cpp \
           audioio/AudioJACKTarget.cpp \
           audioio/AudioPortAudioTarget.cpp \
           audioio/AudioPulseAudioTarget.cpp \
           audioio/AudioTargetFactory.cpp \
           audioio/ClipMixer.cpp \
           audioio/ContinuousSynth.cpp \
           audioio/PlaySpeedRangeMapper.cpp

HEADERS += framework/Align.h \
	   framework/Document.h \
           framework/MainWindowBase.h \
           framework/SVFileReader.h \
           framework/TransformUserConfigurator.h \
           framework/VersionTester.h

SOURCES += framework/Align.cpp \
	   framework/Document.cpp \
           framework/MainWindowBase.cpp \
           framework/SVFileReader.cpp \
           framework/TransformUserConfigurator.cpp \
           framework/VersionTester.cpp


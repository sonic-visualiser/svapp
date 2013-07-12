
TEMPLATE = lib

exists(config.pri) {
    include(config.pri)
}
win* {
    !exists(config.pri) {
        DEFINES += HAVE_PORTAUDIO_2_0
    }
}

CONFIG += staticlib qt thread warn_on stl rtti exceptions
QT += network xml gui widgets

TARGET = svapp

DEPENDPATH += . ../svcore ../svgui
INCLUDEPATH += . ../svcore ../svgui
OBJECTS_DIR = o
MOC_DIR = o

win32-g++ {
    INCLUDEPATH += ../sv-dependency-builds/win32-mingw/include
}
win32-msvc* {
    INCLUDEPATH += ../sv-dependency-builds/win32-msvc/include
}

HEADERS += audioio/AudioCallbackPlaySource.h \
           audioio/AudioCallbackPlayTarget.h \
           audioio/AudioCoreAudioTarget.h \
           audioio/AudioGenerator.h \
           audioio/AudioJACKTarget.h \
           audioio/AudioPortAudioTarget.h \
           audioio/AudioPulseAudioTarget.h \
           audioio/AudioTargetFactory.h \
           audioio/PlaySpeedRangeMapper.h
SOURCES += audioio/AudioCallbackPlaySource.cpp \
           audioio/AudioCallbackPlayTarget.cpp \
           audioio/AudioCoreAudioTarget.cpp \
           audioio/AudioGenerator.cpp \
           audioio/AudioJACKTarget.cpp \
           audioio/AudioPortAudioTarget.cpp \
           audioio/AudioPulseAudioTarget.cpp \
           audioio/AudioTargetFactory.cpp \
           audioio/PlaySpeedRangeMapper.cpp

HEADERS += framework/Document.h \
           framework/MainWindowBase.h \
           framework/SVFileReader.h \
           framework/TransformUserConfigurator.h \
           framework/VersionTester.h

SOURCES += framework/Document.cpp \
           framework/MainWindowBase.cpp \
           framework/SVFileReader.cpp \
           framework/TransformUserConfigurator.cpp \
           framework/VersionTester.cpp


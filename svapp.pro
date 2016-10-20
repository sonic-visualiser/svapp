
TEMPLATE = lib

INCLUDEPATH += ../vamp-plugin-sdk
DEFINES += HAVE_VAMP HAVE_VAMPHOSTSDK

exists(config.pri) {
    include(config.pri)
}
!exists(config.pri) {

    CONFIG += release
    DEFINES += NDEBUG BUILD_RELEASE
    DEFINES += NO_TIMING

    win32-g++ {
        INCLUDEPATH += ../sv-dependency-builds/win32-mingw/include
        LIBS += -L../sv-dependency-builds/win32-mingw/lib
    }
    win32-msvc* {
        # We actually expect MSVC to be used only for 64-bit builds,
        # though the qmake spec is still called win32-msvc*
        INCLUDEPATH += ../sv-dependency-builds/win64-msvc/include
        LIBS += -L../sv-dependency-builds/win64-msvc/lib
    }
    macx* {
        INCLUDEPATH += ../sv-dependency-builds/osx/include
        LIBS += -L../sv-dependency-builds/osx/lib
    }

    win* {
        DEFINES += HAVE_PORTAUDIO
    }
    macx* {
        DEFINES += HAVE_COREAUDIO HAVE_PORTAUDIO
    }
    win32-msvc* {
        DEFINES += NOMINMAX _USE_MATH_DEFINES
        DEFINES -= HAVE_LIBLO
    }
}

CONFIG += staticlib qt thread warn_on stl rtti exceptions c++11
QT += network xml gui widgets

TARGET = svapp

DEPENDPATH += . ../bqaudioio ../svcore ../svgui
INCLUDEPATH += . ../bqaudioio ../svcore ../svgui
OBJECTS_DIR = o
MOC_DIR = o

HEADERS += audio/AudioCallbackPlaySource.h \
           audio/AudioRecordTarget.h \
           audio/AudioGenerator.h \
           audio/ClipMixer.h \
           audio/ContinuousSynth.h \
           audio/PlaySpeedRangeMapper.h

SOURCES += audio/AudioCallbackPlaySource.cpp \
           audio/AudioRecordTarget.cpp \
           audio/AudioGenerator.cpp \
           audio/ClipMixer.cpp \
           audio/ContinuousSynth.cpp \
           audio/PlaySpeedRangeMapper.cpp

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


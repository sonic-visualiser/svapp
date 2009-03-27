TEMPLATE = lib

SV_UNIT_PACKAGES = vamp vamp-hostsdk # required because we use transform headers
load(../prf/sv.prf)

CONFIG += sv staticlib qt thread warn_on stl rtti exceptions
QT += xml

TARGET = svframework

DEPENDPATH += ..
INCLUDEPATH += . ..
OBJECTS_DIR = tmp_obj
MOC_DIR = tmp_moc

HEADERS += Document.h \
           MainWindowBase.h \
           SVFileReader.h

SOURCES += Document.cpp \
           MainWindowBase.cpp \
           SVFileReader.cpp


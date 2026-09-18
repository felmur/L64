QT += widgets core5compat

CONFIG += c++17

LIBS += -lSDL2 -lquazip1-qt6

INCLUDEPATH += /usr/include/QuaZip-Qt6-1.7.2/quazip

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    c64view.cpp \
    cia.cpp \
    d64image.cpp \
    drive.cpp \
    main.cpp \
    mainwindow.cpp \
    sid.cpp \
    timebase.cpp \
    cpu.cpp \
    vic.cpp \
    keyboard.cpp

HEADERS += \
    c64view.h \
    cia.h \
    d64image.h \
    drive.h \
    mainwindow.h \
    sid.h \
    timebase.h \
    cpu.h \
    common.h \
    vic.h \
    keyboard.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES = resource.qrc

# =========================================================================
# FORZATURA CONFIGURAZIONE RELEASE SENZA OTTIMIZZAZIONI DISTRUTTIVE
# =========================================================================
CONFIG(release, debug|release) {
    # Opzione 1: Disattiviamo il Link-Time Optimization (LTO) che fonde i file e rompe il clock
    QMAKE_CXXFLAGS_RELEASE = -pipe -std=gnu++1z -flto -fno-fat-lto-objects -Wall -Wextra -D_REENTRANT -fPIC $(DEFINES)
    # QMAKE_CFLAGS_RELEASE   = -pipe -O1 -Wall -Wextra -D_REENTRANT -fPIC
    # QMAKE_LFLAGS_RELEASE   = -Wl,-O1 -pipe -O1 -std=gnu++1z -fPIC
}


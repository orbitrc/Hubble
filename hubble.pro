CONFIG += link_pkgconfig

PKGCONFIG += cairo libevdev dbus-1

INCLUDEPATH += include \
    .

SOURCES += clients/desktop-shell.cpp \
    shared/cairo-util.c \
    shared/file-util.c \
    shared/xalloc.c \
    desktop-shell/shell.cpp \
    desktop-shell/exposay.cpp \
    desktop-shell/input-panel.cpp \
    compositor/main.cpp \
    libweston/base.cpp \
    libweston/launcher-util.c \
    libweston/launcher-logind.c

HEADERS += \
    shared/cairo-util.h \
    shared/file-util.h \
    shared/helpers.h \
    shared/shell-utils.h \
    shared/timespec-util.h \
    shared/xalloc.h \
    desktop-shell/shell.h \
    compositor/cms-helper.h \
    compositor/hubble.h \
    include/libweston/libweston.h

DEFINES += HAVE_DBUS

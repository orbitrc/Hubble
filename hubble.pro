CONFIG += link_pkgconfig

PKGCONFIG += cairo

INCLUDEPATH += include \
    .

SOURCES += clients/desktop-shell.cpp \
    shared/cairo-util.c \
    shared/file-util.c \
    shared/xalloc.c \
    desktop-shell/shell.cpp \
    desktop-shell/exposay.cpp \
    desktop-shell/input-panel.cpp \
    libweston/base.cpp

HEADERS += \
    shared/cairo-util.h \
    shared/file-util.h \
    shared/helpers.h \
    shared/shell-utils.h \
    shared/timespec-util.h \
    shared/xalloc.h \
    desktop-shell/shell.h \
    include/libweston/libweston.h

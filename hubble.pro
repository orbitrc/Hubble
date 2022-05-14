CONFIG += link_pkgconfig

PKGCONFIG += cairo

INCLUDEPATH += include

SOURCES += clients/desktop-shell.cpp \
    shared/cairo-util.c \
    shared/file-util.c \
    shared/xalloc.c

HEADERS += \
    shared/cairo-util.h \
    shared/file-util.h \
    shared/helpers.h \
    shared/timespec-util.h \
    shared/xalloc.h

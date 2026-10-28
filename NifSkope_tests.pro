# Build from a separate directory with qmake ../NifSkope_tests.pro.
include(NifSkope.pro)
TARGET = NifSkopeTests
CONFIG += console
SOURCES -= src/main.cpp
SOURCES += tests/document_undo.cpp
QMAKE_PRE_LINK =
QMAKE_POST_LINK =

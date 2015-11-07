TARGET = qscfb

PLUGIN_TYPE = platforms
PLUGIN_CLASS_NAME = QScFbIntegrationPlugin
!equals(TARGET, $$QT_DEFAULT_QPA_PLUGIN): PLUGIN_EXTENDS = -
load(qt_plugin)

QT += core-private gui-private platformsupport-private

SOURCES = main.cpp qscfbintegration.cpp qscfbscreen.cpp
HEADERS = qscfbintegration.h qscfbscreen.h

CONFIG += qpa/genericunixfontdatabase

OTHER_FILES += scfb.json

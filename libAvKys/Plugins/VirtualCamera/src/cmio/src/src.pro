# Webcamoid, webcam capture application.
# Copyright (C) 2011-2017  Gonzalo Exequiel Pedone
#
# Webcamoid is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Webcamoid is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
#
# Web-Site: http://webcamoid.github.io/

exists(commons.pri) {
    include(commons.pri)
} else {
    exists(../../../../../commons.pri) {
        include(../../../../../commons.pri)
    } else {
        error("commons.pri file not found.")
    }
}

include(../cmio.pri)

CONFIG += plugin

HEADERS = \
    plugin.h \
    cameraoutcmio.h \
    ../../cameraout.h


INCLUDEPATH += \
    ../VCamIPC/src \
    ../../../../../Lib/src \
    ../../

LIBS += -L$${PWD}/../../../../../Lib/ -l$${COMMONS_TARGET}

OTHER_FILES += ../pspec.json

LIBS += \
    -L$${OUT_PWD}/../../VCamUtils -lVCamUtils \
    -L$${OUT_PWD}/../VCamIPC -lVCamIPC \
    -framework CoreFoundation \
    -framework CoreMedia \
    -framework CoreMediaIO \
    -framework CoreVideo \
    -framework IOKit \
    -framework IOSurface

QT += qml xml

SOURCES = \
    plugin.cpp \
    cameraoutcmio.cpp \
    ../../cameraout.cpp

DESTDIR = $${OUT_PWD}/../../../submodules/VirtualCamera

TARGET = cmio

TEMPLATE = lib

RESOURCES = \
    ../cmio.qrc

INSTALLS += target

target.path = $${LIBDIR}/$${COMMONS_TARGET}/submodules/VirtualCamera

/* Webcamoid, webcam capture application.
 * Copyright (C) 2011-2015  Gonzalo Exequiel Pedone
 *
 * Webcamoid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Webcamoid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Webcamoid. If not, see <http://www.gnu.org/licenses/>.
 *
 * Email   : hipersayan DOT x AT gmail DOT com
 * Web-Site: http://github.com/hipersayanX/webcamoid
 */

#ifndef AUDIOOUTPUTELEMENT_H
#define AUDIOOUTPUTELEMENT_H

#include <QMutex>
#include <qbelement.h>

#ifdef Q_OS_LINUX
#include "platform/audiooutlinux.h"
#endif

#ifdef Q_OS_WIN32
#include "platform/audiooutwin.h"
#endif

class AudioOutputElement: public QbElement
{
    Q_OBJECT

    public:
        explicit AudioOutputElement();
        ~AudioOutputElement();
        Q_INVOKABLE int bufferSize() const;

    private:
        AudioOut m_audioOut;
        int m_bufferSize;
        QbElementPtr m_convert;
        QMutex m_mutex;

    protected:
        void stateChange(QbElement::ElementState from, QbElement::ElementState to);

    public slots:
        void setBufferSize(int bufferSize);
        void resetBufferSize();
        QbPacket iStream(const QbAudioPacket &packet);

    private slots:
        bool init();
        void uninit();
};

#endif // AUDIOOUTPUTELEMENT_H

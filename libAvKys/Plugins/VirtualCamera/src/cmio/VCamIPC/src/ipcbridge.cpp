/* Webcamoid, webcam capture application.
 * Copyright (C) 2011-2017  Gonzalo Exequiel Pedone
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
 * Web-Site: http://webcamoid.github.io/
 */

#include <sstream>
#include <algorithm>
#include <IOSurface/IOSurface.h>
#include <CoreMedia/CMFormatDescription.h>

#include "ipcbridge.h"
#include "../../Assistant/src/assistantglobals.h"
#include "VCamUtils/src/utils.h"
#include "VCamUtils/src/image/videoformat.h"
#include "VCamUtils/src/image/videoframe.h"

#define AkIpcBridgeLogMethod() \
    AkLoggerLog("IpcBridge::" << __FUNCTION__ << "()")

namespace AkVCam
{
    class IpcBridgePrivate;
    IpcBridgePrivate *ipcBridgePrivate();

    class IpcBridgePrivate
    {
        public:
            IpcBridge *parent;
            std::string portName;
            xpc_connection_t messagePort;
            xpc_connection_t serverMessagePort;
            std::vector<std::string> devices;
            IpcBridge::FrameReadyCallback frameReadyCallback;
            IpcBridge::DeviceChangedCallback deviceAddedCallback;
            IpcBridge::DeviceChangedCallback deviceRemovedCallback;
            IpcBridge::BroadcastingChangedCallback broadcastingChangedCallback;
            IpcBridge::MirrorChangedCallback mirrorChangedCallback;
            IpcBridge::ScalingChangedCallback scalingChangedCallback;
            IpcBridge::AspectRatioChangedCallback aspectRatioChangedCallback;
            std::map<int64_t, XpcMessage> m_messageHandlers;
            std::vector<std::string> broadcasting;

            IpcBridgePrivate(IpcBridge *parent=nullptr):
                parent(parent),
                messagePort(nullptr),
                serverMessagePort(nullptr)
            {
                this->startAssistant();

                this->m_messageHandlers = {
                    {AKVCAM_ASSISTANT_MSG_DEVICE_CREATED             , AKVCAM_BIND_FUNC(IpcBridgePrivate::deviceCreated)      },
                    {AKVCAM_ASSISTANT_MSG_DEVICE_DESTROYED           , AKVCAM_BIND_FUNC(IpcBridgePrivate::deviceDestroyed)    },
                    {AKVCAM_ASSISTANT_MSG_FRAME_READY                , AKVCAM_BIND_FUNC(IpcBridgePrivate::frameReady)         },
                    {AKVCAM_ASSISTANT_MSG_DEVICE_BROADCASTING_CHANGED, AKVCAM_BIND_FUNC(IpcBridgePrivate::broadcastingChanged)},
                    {AKVCAM_ASSISTANT_MSG_DEVICE_MIRRORING_CHANGED   , AKVCAM_BIND_FUNC(IpcBridgePrivate::mirrorChanged)      },
                    {AKVCAM_ASSISTANT_MSG_DEVICE_SCALING_CHANGED     , AKVCAM_BIND_FUNC(IpcBridgePrivate::scalingChanged)     },
                    {AKVCAM_ASSISTANT_MSG_DEVICE_ASPECTRATIO_CHANGED , AKVCAM_BIND_FUNC(IpcBridgePrivate::aspectRatioChanged) }
                };
            }

            inline bool startAssistant()
            {
                std::stringstream launchctl;

                launchctl << "launchctl list " << AKVCAM_ASSISTANT_NAME;
                int result = system(launchctl.str().c_str());

                if (result != 0) {
                    auto homePath = std::string("/Users/") + getenv("USER");

                    std::stringstream ss;
                    ss << CMIO_DAEMONS_PATH
                       << "/"
                       << AKVCAM_ASSISTANT_NAME
                       << ".plist";
                    auto daemon = ss.str();

                    if (daemon[0] == '~')
                        daemon.replace(0, 1, homePath);

                    launchctl.str("");
                    launchctl << "launchctl load -w " << daemon;
                    result = system(launchctl.str().c_str());

                    return result == 0;
                }

                return true;
            }

            inline void add(IpcBridge *bridge)
            {
                this->m_bridges.push_back(bridge);
            }

            inline void remove(IpcBridge *bridge)
            {
                for (size_t i = 0; i < this->m_bridges.size(); i++)
                    if (this->m_bridges[i] == bridge) {
                        this->m_bridges.erase(this->m_bridges.begin()
                                              + long(i));

                        break;
                    }
            }

            inline std::vector<IpcBridge *> &bridges()
            {
                return this->m_bridges;
            }

            inline void deviceCreated(xpc_connection_t client,
                                      xpc_object_t event)
            {
                UNUSED(client)

                std::string device = xpc_dictionary_get_string(event, "device");

                for (auto bridge: this->m_bridges)
                    if (bridge->d->deviceAddedCallback)
                        bridge->d->deviceAddedCallback(device);
            }

            inline void deviceDestroyed(xpc_connection_t client,
                                        xpc_object_t event)
            {
                UNUSED(client)

                std::string device = xpc_dictionary_get_string(event, "device");

                for (auto bridge: this->m_bridges)
                    if (bridge->d->deviceRemovedCallback)
                        bridge->d->deviceRemovedCallback(device);
            }

            inline void frameReady(xpc_connection_t client,
                                   xpc_object_t event)
            {
                UNUSED(client)

                std::string deviceId = xpc_dictionary_get_string(event, "device");
                auto frame = xpc_dictionary_get_value(event, "frame");
                auto surface = IOSurfaceLookupFromXPCObject(frame);

                uint32_t surfaceSeed = 0;
                IOSurfaceLock(surface, kIOSurfaceLockReadOnly, &surfaceSeed);
                FourCC fourcc = IOSurfaceGetPixelFormat(surface);
                int width = IOSurfaceGetWidth(surface);
                int height = IOSurfaceGetHeight(surface);
                size_t size = IOSurfaceGetAllocSize(surface);
                auto data = reinterpret_cast<uint8_t *>(IOSurfaceGetBaseAddress(surface));
                AkVCam::VideoFrame videoFrame({fourcc, width, height},
                                              data, size);
                IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, &surfaceSeed);
                CFRelease(surface);

                for (auto bridge: this->m_bridges)
                    if (bridge->d->frameReadyCallback)
                        bridge->d->frameReadyCallback(deviceId, videoFrame);
            }

            inline void broadcastingChanged(xpc_connection_t client,
                                            xpc_object_t event)
            {
                UNUSED(client)

                std::string deviceId = xpc_dictionary_get_string(event, "device");
                bool broadcasting = xpc_dictionary_get_bool(event, "broadcasting");

                for (auto bridge: this->m_bridges)
                    if (bridge->d->broadcastingChangedCallback)
                        bridge->d->broadcastingChangedCallback(deviceId, broadcasting);
            }

            inline void mirrorChanged(xpc_connection_t client,
                                      xpc_object_t event)
            {
                UNUSED(client)

                std::string deviceId = xpc_dictionary_get_string(event, "device");
                bool horizontalMirror = xpc_dictionary_get_bool(event, "hmirror");
                bool verticalMirror = xpc_dictionary_get_bool(event, "vmirror");

                for (auto bridge: this->m_bridges)
                    if (bridge->d->mirrorChangedCallback)
                        bridge->d->mirrorChangedCallback(deviceId,
                                                         horizontalMirror,
                                                         verticalMirror);
            }

            inline void scalingChanged(xpc_connection_t client,
                                       xpc_object_t event)
            {
                UNUSED(client)

                std::string deviceId = xpc_dictionary_get_string(event, "device");
                auto scaling = VideoFrame::Scaling(xpc_dictionary_get_int64(event, "scaling"));

                for (auto bridge: this->m_bridges)
                    if (bridge->d->scalingChangedCallback)
                        bridge->d->scalingChangedCallback(deviceId, scaling);
            }

            inline void aspectRatioChanged(xpc_connection_t client,
                                           xpc_object_t event)
            {
                UNUSED(client)

                std::string deviceId = xpc_dictionary_get_string(event, "device");
                auto aspectRatio = VideoFrame::AspectRatio(xpc_dictionary_get_int64(event, "aspect"));

                for (auto bridge: this->m_bridges)
                    if (bridge->d->aspectRatioChangedCallback)
                        bridge->d->aspectRatioChangedCallback(deviceId, aspectRatio);
            }

            inline void messageReceived(xpc_connection_t client,
                                        xpc_object_t event)
            {
                auto type = xpc_get_type(event);

                if (type == XPC_TYPE_ERROR) {
                     auto description = xpc_copy_description(event);
                     AkLoggerLog("ERROR: " << description);
                     free(description);
                } else if (type == XPC_TYPE_DICTIONARY) {
                    int64_t message = xpc_dictionary_get_int64(event, "message");

                    if (this->m_messageHandlers.count(message))
                        this->m_messageHandlers[message](client, event);
                }
            }

        private:
            std::vector<IpcBridge *> m_bridges;
    };

    inline IpcBridgePrivate *ipcBridgePrivate()
    {
        static IpcBridgePrivate ipcBridgePrivate;

        return &ipcBridgePrivate;
    }
}

AkVCam::IpcBridge::IpcBridge()
{
    AkIpcBridgeLogMethod();

    this->d = new IpcBridgePrivate(this);
    ipcBridgePrivate()->add(this);
}

AkVCam::IpcBridge::~IpcBridge()
{
    this->unregisterEndPoint();
    ipcBridgePrivate()->remove(this);
    delete this->d;
}

bool AkVCam::IpcBridge::registerEndPoint(bool asClient)
{
    AkIpcBridgeLogMethod();

    if (this->d->serverMessagePort)
        return true;

    xpc_object_t dictionary = nullptr;
    xpc_object_t reply = nullptr;
    std::string portName;
    xpc_connection_t messagePort = nullptr;
    xpc_type_t replyType;
    bool status = false;

    auto serverMessagePort =
            xpc_connection_create_mach_service(AKVCAM_ASSISTANT_NAME,
                                               NULL,
                                               0);

    if (!serverMessagePort)
        goto registerEndPoint_failed;

    xpc_connection_set_event_handler(serverMessagePort, ^(xpc_object_t) {});
    xpc_connection_resume(serverMessagePort);

    dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_REQUEST_PORT);
    xpc_dictionary_set_bool(dictionary, "client", asClient);
    reply = xpc_connection_send_message_with_reply_sync(serverMessagePort,
                                                        dictionary);
    xpc_release(dictionary);
    replyType = xpc_get_type(reply);

    if (replyType == XPC_TYPE_DICTIONARY)
        portName = xpc_dictionary_get_string(reply, "port");

    xpc_release(reply);

    if (replyType != XPC_TYPE_DICTIONARY)
        goto registerEndPoint_failed;

    messagePort = xpc_connection_create(NULL, NULL);

    if (!messagePort)
        goto registerEndPoint_failed;

    xpc_connection_set_event_handler(messagePort, ^(xpc_object_t event) {
        auto type = xpc_get_type(event);

        if (type == XPC_TYPE_ERROR)
            return;

        auto client = reinterpret_cast<xpc_connection_t>(event);

        xpc_connection_set_event_handler(client, ^(xpc_object_t event) {
            ipcBridgePrivate()->messageReceived(client, event);
        });

        xpc_connection_resume(client);
    });

    xpc_connection_resume(messagePort);

    dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_ADD_PORT);
    xpc_dictionary_set_string(dictionary, "port", portName.c_str());
    xpc_dictionary_set_connection(dictionary, "connection", messagePort);
    reply = xpc_connection_send_message_with_reply_sync(serverMessagePort,
                                                        dictionary);
    xpc_release(dictionary);
    replyType = xpc_get_type(reply);

    if (replyType == XPC_TYPE_DICTIONARY)
        status = xpc_dictionary_get_bool(reply, "status");

    xpc_release(reply);

    if (replyType != XPC_TYPE_DICTIONARY || !status)
        goto registerEndPoint_failed;

    this->d->portName = portName;
    this->d->messagePort = messagePort;
    this->d->serverMessagePort = serverMessagePort;

    AkLoggerLog("SUCCESSFUL");

    return true;

registerEndPoint_failed:
    if (messagePort)
        xpc_release(messagePort);

    if (serverMessagePort)
        xpc_release(serverMessagePort);

    AkLoggerLog("FAILED");

    return false;
}

void AkVCam::IpcBridge::unregisterEndPoint()
{
    AkIpcBridgeLogMethod();

    if (this->d->messagePort) {
        xpc_release(this->d->messagePort);
        this->d->messagePort = nullptr;
    }

    if (this->d->serverMessagePort) {
        if (!this->d->portName.empty()) {
            auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
            xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_REMOVE_PORT);
            xpc_dictionary_set_string(dictionary, "port", this->d->portName.c_str());
            xpc_connection_send_message(this->d->serverMessagePort,
                                        dictionary);
            xpc_release(dictionary);
        }

        xpc_release(this->d->serverMessagePort);
        this->d->serverMessagePort = nullptr;
    }

    this->d->portName.clear();
}

std::vector<std::string> AkVCam::IpcBridge::listDevices(bool all) const
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return {};

    if (!all)
        return this->d->devices;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICES);
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return {};
    }

    auto devicesList = xpc_dictionary_get_array(reply, "devices");
    std::vector<std::string> devices;

    for (size_t i = 0; i < xpc_array_get_count(devicesList); i++)
        devices.push_back(xpc_array_get_string(devicesList, i));

    xpc_release(reply);

    return devices;
}

std::string AkVCam::IpcBridge::description(const std::string &deviceId) const
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return {};

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DESCRIPTION);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return {};
    }

    std::string description(xpc_dictionary_get_string(reply, "description"));
    xpc_release(reply);

    return description;
}

std::vector<AkVCam::PixelFormat> AkVCam::IpcBridge::supportedOutputPixelFormats() const
{
    return {
        PixelFormatRGB32,
        PixelFormatRGB24,
        PixelFormatUYVY,
        PixelFormatYUY2
    };
}

std::vector<AkVCam::VideoFormat> AkVCam::IpcBridge::formats(const std::string &deviceId) const
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return {};

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_FORMATS);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return {};
    }

    auto formatsList = xpc_dictionary_get_array(reply, "formats");
    std::vector<VideoFormat> formats;

    for (size_t i = 0; i < xpc_array_get_count(formatsList); i++) {
        auto format = xpc_array_get_dictionary(formatsList, i);
        auto fourcc = FourCC(xpc_dictionary_get_uint64(format, "fourcc"));
        auto width = int(xpc_dictionary_get_int64(format, "width"));
        auto height = int(xpc_dictionary_get_int64(format, "height"));
        double fps = xpc_dictionary_get_double(format, "fps");

        formats.push_back(VideoFormat(fourcc, width, height, {fps}));
    }

    xpc_release(reply);

    return formats;
}

bool AkVCam::IpcBridge::broadcasting(const std::string &deviceId) const
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return false;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_BROADCASTING);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return false;
    }

    bool broadcasting = xpc_dictionary_get_bool(reply, "broadcasting");
    xpc_release(reply);

    AkLoggerLog("Device: " << deviceId);
    AkLoggerLog("Broadcasting: " << broadcasting);

    return broadcasting;
}

bool AkVCam::IpcBridge::isHorizontalMirrored(const std::string &deviceId)
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return false;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_MIRRORING);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return false;
    }

    bool horizontalMirror = xpc_dictionary_get_bool(reply, "hmirror");
    xpc_release(reply);

    return horizontalMirror;
}

bool AkVCam::IpcBridge::isVerticalMirrored(const std::string &deviceId)
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return false;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_MIRRORING);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return false;
    }

    bool verticalMirror = xpc_dictionary_get_bool(reply, "vmirror");
    xpc_release(reply);

    return verticalMirror;
}

AkVCam::VideoFrame::Scaling AkVCam::IpcBridge::scalingMode(const std::string &deviceId)
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return VideoFrame::ScalingFast;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_SCALING);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return VideoFrame::ScalingFast;
    }

    auto scaling = VideoFrame::Scaling(xpc_dictionary_get_int64(reply, "scaling"));
    xpc_release(reply);

    return scaling;
}

AkVCam::VideoFrame::AspectRatio AkVCam::IpcBridge::aspectRatioMode(const std::string &deviceId)
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return VideoFrame::AspectRatioIgnore;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_ASPECTRATIO);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return VideoFrame::AspectRatioIgnore;
    }

    auto aspectRatio = VideoFrame::AspectRatio(xpc_dictionary_get_int64(reply, "aspect"));
    xpc_release(reply);

    return aspectRatio;
}

std::string AkVCam::IpcBridge::deviceCreate(const std::string &description,
                                            const std::vector<VideoFormat> &formats)
{
    AkIpcBridgeLogMethod();

    if (!this->d->startAssistant())
        return {};

    this->registerEndPoint(false);

    if (!this->d->serverMessagePort || !this->d->messagePort)
        return {};

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_CREATE);
    xpc_dictionary_set_string(dictionary, "port", this->d->portName.c_str());
    xpc_dictionary_set_string(dictionary, "description", description.c_str());
    auto formatsList = xpc_array_create(NULL, 0);

    for (auto &format: formats) {
        auto dictFormat = xpc_dictionary_create(nullptr, nullptr, 0);
        xpc_dictionary_set_uint64(dictFormat, "fourcc", format.fourcc());
        xpc_dictionary_set_int64(dictFormat, "width", format.width());
        xpc_dictionary_set_int64(dictFormat, "height", format.height());
        xpc_dictionary_set_double(dictFormat, "fps", format.minimumFrameRate());
        xpc_array_append_value(formatsList, dictFormat);
    }

    xpc_dictionary_set_value(dictionary, "formats", formatsList);

    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return {};
    }

    std::string deviceId(xpc_dictionary_get_string(reply, "device"));

    if (!deviceId.empty())
        this->d->devices.push_back(deviceId);

    xpc_release(reply);

    return deviceId;
}

void AkVCam::IpcBridge::deviceDestroy(const std::string &deviceId)
{
    AkIpcBridgeLogMethod();

    if (!this->d->serverMessagePort)
        return;

    auto it = std::find(this->d->devices.begin(),
                        this->d->devices.end(),
                        deviceId);

    if (it == this->d->devices.end())
        return;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_DESTROY);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    xpc_connection_send_message(this->d->serverMessagePort,
                                dictionary);
    xpc_release(dictionary);
    this->d->devices.erase(it);
}

bool AkVCam::IpcBridge::deviceStart(const std::string &deviceId)
{
    AkIpcBridgeLogMethod();

    auto it = std::find(this->d->broadcasting.begin(),
                        this->d->broadcasting.end(),
                        deviceId);

    if (it != this->d->broadcasting.end())
        return false;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_SETBROADCASTING);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    xpc_dictionary_set_bool(dictionary, "broadcasting", true);
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    auto replyType = xpc_get_type(reply);

    if (replyType != XPC_TYPE_DICTIONARY) {
        xpc_release(reply);

        return {};
    }

    bool status = xpc_dictionary_get_bool(reply, "status");
    xpc_release(reply);
    this->d->broadcasting.push_back(deviceId);

    return status;
}

void AkVCam::IpcBridge::deviceStop(const std::string &deviceId)
{
    AkIpcBridgeLogMethod();

    auto it = std::find(this->d->broadcasting.begin(),
                        this->d->broadcasting.end(),
                        deviceId);

    if (it == this->d->broadcasting.end())
        return;

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_SETBROADCASTING);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    xpc_dictionary_set_bool(dictionary, "broadcasting", false);
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    xpc_release(reply);
    this->d->broadcasting.erase(it);
}

void AkVCam::IpcBridge::write(const std::string &deviceId,
                              const VideoFrame &frame)
{
    AkIpcBridgeLogMethod();

    auto it = std::find(this->d->broadcasting.begin(),
                        this->d->broadcasting.end(),
                        deviceId);

    if (it == this->d->broadcasting.end())
        return;

    std::vector<CFStringRef> keys {
        kIOSurfacePixelFormat,
        kIOSurfaceWidth,
        kIOSurfaceHeight,
        kIOSurfaceAllocSize
    };

    auto fourcc = frame.format().fourcc();
    auto width = frame.format().width();
    auto height = frame.format().height();
    auto dataSize = int64_t(frame.dataSize());

    std::vector<CFNumberRef> values {
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &fourcc),
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &width),
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &height),
        CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt64Type, &dataSize)
    };

    auto surfaceProperties =
            CFDictionaryCreate(kCFAllocatorDefault,
                               reinterpret_cast<const void **>(keys.data()),
                               reinterpret_cast<const void **>(values.data()),
                               values.size(),
                               NULL,
                               NULL);
    auto surface = IOSurfaceCreate(surfaceProperties);

    for (auto value: values)
        CFRelease(value);

    CFRelease(surfaceProperties);

    if (!surface)
        return;

    uint32_t surfaceSeed = 0;
    IOSurfaceLock(surface, 0, &surfaceSeed);
    auto data = IOSurfaceGetBaseAddress(surface);
    memcpy(data, frame.data().get(), frame.dataSize());
    IOSurfaceUnlock(surface, 0, &surfaceSeed);

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_FRAME_READY);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    xpc_dictionary_set_value(dictionary, "frame", IOSurfaceCreateXPCObject(surface));
    xpc_connection_send_message(this->d->serverMessagePort,
                                dictionary);
    xpc_release(dictionary);
    CFRelease(surface);
}

void AkVCam::IpcBridge::setMirroring(const std::string &deviceId,
                                     bool horizontalMirrored,
                                     bool verticalMirrored)
{
    AkIpcBridgeLogMethod();

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_SETMIRRORING);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    xpc_dictionary_set_bool(dictionary, "hmirror", horizontalMirrored);
    xpc_dictionary_set_bool(dictionary, "vmirror", verticalMirrored);
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    xpc_release(reply);
}

void AkVCam::IpcBridge::setScaling(const std::string &deviceId,
                                   VideoFrame::Scaling scaling)
{
    AkIpcBridgeLogMethod();

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_SETSCALING);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    xpc_dictionary_set_int64(dictionary, "scaling", scaling);
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    xpc_release(reply);
}

void AkVCam::IpcBridge::setAspectRatio(const std::string &deviceId,
                                       AkVCam::VideoFrame::AspectRatio aspectRatio)
{
    AkIpcBridgeLogMethod();

    auto dictionary = xpc_dictionary_create(NULL, NULL, 0);
    xpc_dictionary_set_int64(dictionary, "message", AKVCAM_ASSISTANT_MSG_DEVICE_SETASPECTRATIO);
    xpc_dictionary_set_string(dictionary, "device", deviceId.c_str());
    xpc_dictionary_set_int64(dictionary, "aspect", aspectRatio);
    auto reply = xpc_connection_send_message_with_reply_sync(this->d->serverMessagePort,
                                                             dictionary);
    xpc_release(dictionary);
    xpc_release(reply);
}

void AkVCam::IpcBridge::setFrameReadyCallback(FrameReadyCallback callback)
{
    this->d->frameReadyCallback = callback;
}

void AkVCam::IpcBridge::setDeviceAddedCallback(DeviceChangedCallback callback)
{
    this->d->deviceAddedCallback = callback;
}

void AkVCam::IpcBridge::setDeviceRemovedCallback(DeviceChangedCallback callback)
{
    this->d->deviceRemovedCallback = callback;
}

void AkVCam::IpcBridge::setBroadcastingChangedCallback(BroadcastingChangedCallback callback)
{
    this->d->broadcastingChangedCallback = callback;
}

void AkVCam::IpcBridge::setMirrorChangedCallback(AkVCam::IpcBridge::MirrorChangedCallback callback)
{
    this->d->mirrorChangedCallback = callback;
}

void AkVCam::IpcBridge::setScalingChangedCallback(AkVCam::IpcBridge::ScalingChangedCallback callback)
{
    this->d->scalingChangedCallback = callback;
}

void AkVCam::IpcBridge::setAspectRatioChangedCallback(AkVCam::IpcBridge::AspectRatioChangedCallback callback)
{
    this->d->aspectRatioChangedCallback = callback;
}

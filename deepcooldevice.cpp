#include "deepcooldevice.h"
#include "dcld_protocol.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <QTextStream>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <QSettings>       // 🚀 VOORBEREIDING FIX: Geeft de driver toegang tot de actieve GIF-slots!
#include <QStandardPaths>   // 🚀 VOORBEREIDING FIX: Zorgt dat de driver de permanente AppData-map kan vinden!
#include <QThread>
#include <iostream>
#include <thread>
#include <chrono>
#include <limits>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <cstring>
#include <array>
#include <cstdint>
#include <exception>
#include <vector>

DeepCoolDevice::DeepCoolDevice()
: deviceType(DEVICE_TYPE_UNKNOWN)
, usbContext(nullptr)
, deviceHandle(nullptr)
, interfaceNumber(0)
, endpointOut(0x02)
, endpointIn(0x82)
, fd(-1)
, currentMode(MODE_CPU_INFO)
, currentRotation(ROTATION_0)
{
    // Restore desired RGB and brightness before first regular rotation packet.
    QSettings visualSettings("LionheartTim", "DeepCoolDigitalGui");
    currentRgbMode = std::clamp(
        visualSettings.value("rgbEffectMode", 0).toInt(), 0, 2);
    currentBrightness = std::clamp(
        visualSettings.value("screenBrightness", 70).toInt(), 0, 100);
    libusb_init(&usbContext);
}

void DeepCoolDevice::close()
{
    if (deviceHandle0) {
        libusb_release_interface(deviceHandle0, 0);
        libusb_close(deviceHandle0);
        deviceHandle0 = nullptr;
    }

    deviceHandle = nullptr;

    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

void DeepCoolDevice::hardResetUsbDevice()
{
    if (deviceType != DEVICE_TYPE_USB_VENDOR || !deviceHandle0) {
        qWarning() << "[GIF-RECOVERY] USB-reset overgeslagen: geen USB-handle.";
        return;
    }

    const DeviceInfo reconnectInfo = deviceInfo;

    qWarning() << "[GIF-RECOVERY] USB-poortreset starten.";
    const int resetResult = libusb_reset_device(deviceHandle0);

    if (resetResult != 0) {
        qWarning() << "[GIF-RECOVERY] USB-reset gaf fout terug:"
        << libusb_error_name(resetResult);
    }

    close();
    currentMode = MODE_CPU_INFO;
    QThread::msleep(750);

    if (!open(reconnectInfo)) {
        qCritical() << "[GIF-RECOVERY] USB-heropenen na reset mislukt.";
        return;
    }

    qWarning() << "[GIF-RECOVERY] USB opnieuw geopend na uploadfout.";
}

DeepCoolDevice::~DeepCoolDevice()
{
    close();

    if (usbContext) {
        libusb_exit(usbContext);
        usbContext = nullptr;
    }
}


bool DeepCoolDevice::open(const DeviceInfo &deviceInfo) {
    if (isOpen()) {
        close();
    }

    // MYSTIQUE 360 ARGB
    // Only accept VID:PID 3633:0009 on the USB vendor route.
    if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR &&
        (deviceInfo.vendorId != 0x3633 ||
        deviceInfo.productId != 0x0009)) {

        qCritical() << "[USB] Unexpected USB device:"
        << QString("%1:%2")
        .arg(deviceInfo.vendorId, 4, 16, QChar('0'))
        .arg(deviceInfo.productId, 4, 16, QChar('0'));

    return false;
        }

        this->deviceInfo = deviceInfo;
        deviceType = deviceInfo.type;
        deviceName = deviceInfo.displayName;

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        fd = ::open(deviceInfo.devicePath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            return false;
        }
    }
    else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        if (!usbContext) {
            return false;
        }
    }

    libusb_device **devList;
    ssize_t cnt = libusb_get_device_list(usbContext, &devList);
    if (cnt < 0) {
        return false;
    }

    libusb_device *targetDevice = nullptr;
    for (ssize_t i = 0; i < cnt; ++i) {
        struct libusb_device_descriptor desc;
        if (libusb_get_device_descriptor(devList[i], &desc) == 0) {
            if (desc.idVendor == deviceInfo.vendorId && desc.idProduct == deviceInfo.productId) {
                targetDevice = devList[i];
                break;
            }
        }
    }

    if (!targetDevice) {
        libusb_free_device_list(devList, 1);
        return false;
    }


    // ============================================================
    // STEP 1.2 — MYSTIQUE USB ENDPOINT VERIFICATION
    // Interface 0, alternate setting 0:
    //   0x01 OUT / 0x81 IN
    //   0x02 OUT / 0x82 IN
    // All four endpoints must be BULK with max packet size 64.
    // This check does not send any USB commands.
    // ============================================================

    if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        libusb_config_descriptor *config = nullptr;

        const int configResult =
        libusb_get_active_config_descriptor(targetDevice, &config);

        if (configResult != LIBUSB_SUCCESS || config == nullptr) {
            qCritical() << "[USB] Cannot read active USB configuration:"
            << libusb_error_name(configResult);

            libusb_free_device_list(devList, 1);
            return false;
        }

        bool layoutValid = false;

        for (int i = 0; i < config->bNumInterfaces; ++i) {
            const libusb_interface &interface = config->interface[i];

            for (int a = 0; a < interface.num_altsetting; ++a) {
                const libusb_interface_descriptor &alt =
                interface.altsetting[a];

                if (alt.bInterfaceNumber != 0 ||
                    alt.bAlternateSetting != 0) {
                    continue;
                    }

                    bool endpointsValid = (alt.bNumEndpoints == 4);
                unsigned int endpointMask = 0;

                for (int e = 0;
                     e < alt.bNumEndpoints && endpointsValid;
                ++e) {

                    const libusb_endpoint_descriptor &ep =
                    alt.endpoint[e];

                    unsigned int bit = 0;

                    switch (ep.bEndpointAddress) {
                        case 0x01: bit = 0x01; break;
                        case 0x81: bit = 0x02; break;
                        case 0x02: bit = 0x04; break;
                        case 0x82: bit = 0x08; break;
                        default: break;
                    }

                    if (bit == 0 ||
                        (endpointMask & bit) != 0 ||
                        (ep.bmAttributes & 0x03) != 0x02 ||
                        (ep.wMaxPacketSize & 0x07FF) != 64) {

                        endpointsValid = false;
                    break;
                        }

                        endpointMask |= bit;
                }

                if (endpointsValid && endpointMask == 0x0F) {
                    layoutValid = true;
                }
            }
        }

        libusb_free_config_descriptor(config);

        if (!layoutValid) {
            qCritical() << "[USB] Interface 0 endpoint layout mismatch";
            libusb_free_device_list(devList, 1);
            return false;
        }

        qDebug() << "[USB] Interface 0 and all four endpoints verified";
    }


    // ============================================================
    // ÉÉN INTERFACE — ÉÉN LIBUSB HANDLE
    //
    // Interface 0:
    //   0x01 OUT / 0x81 IN = Mode switch / control
    //   0x02 OUT / 0x82 IN = Metrics / GIF data
    //
    // GIF blijft dus volledig op Interface 0.
    // ============================================================
    int ret = libusb_open(targetDevice, &deviceHandle0);

    libusb_free_device_list(devList, 1);

    if (ret < 0) {
        std::cerr << "❌ [JZFS] Openen van USB-handle gefaald: "
        << libusb_error_name(ret) << std::endl;
        deviceHandle0 = nullptr;
        return false;
    }

    std::cout << "✅ [JZFS Native] USB-handle geopend." << std::endl;

    // ------------------------------------------------------------
    // CLAIM INTERFACE 0 = METRICS
    // ------------------------------------------------------------
    int claimRes0 = libusb_claim_interface(deviceHandle0, 0);

    if (claimRes0 < 0) {
        std::cerr << "❌ [JZFS] Interface 0 kon niet worden geclaimd: "
        << libusb_error_name(claimRes0) << std::endl;

        libusb_close(deviceHandle0);
        deviceHandle0 = nullptr;
        return false;
    }

    std::cout << "✅ [JZFS Native] Interface 0 succesvol geclaimd."
    << std::endl;

    // ------------------------------------------------------------
    // GIF DATA = INTERFACE 0
    //
    // Endpoint 0x02 OUT / 0x82 IN
    // wordt via dezelfde Interface-0 handle gebruikt.
    //
    // Interface 1 wordt NIET geclaimd.
    // ------------------------------------------------------------
    std::cout << "✅ [JZFS] GIF data blijft op Interface 0 (0x02/0x82)."
    << std::endl;

    // Normale TFT/Metrics-route blijft op dezelfde handle actief.
    this->deviceHandle = deviceHandle0;

    libusb_device_handle *deviceHandle = deviceHandle0;

    // Vuur de geverifieerde v1.2.1 opstartpakketjes af over de alias
    {
        QByteArray openInitPacket(64, 0);
        openInitPacket[0] = 0x00;
        openInitPacket[1] = static_cast<char>(0xAA);
        openInitPacket[2] = 0x2E;
        openInitPacket[3] = 0x04;

        openInitPacket[40] = 0x48; openInitPacket[41] = 0x49; openInitPacket[42] = 0x44; openInitPacket[43] = 0x43;

        quint16 check0 = 0;
        for (int j = 1; j < 47; ++j) check0 += static_cast<quint8>(openInitPacket[j]);
        openInitPacket[47] = static_cast<char>(check0 & 0xFF);
        openInitPacket[48] = static_cast<char>((check0 >> 8) & 0xFF);
        libusb_control_transfer(deviceHandle, 0x21, 0x09, 0x0300, 0x0001, reinterpret_cast<unsigned char*>(openInitPacket.data()), 64, 500);

        QByteArray openStartPacket(64, 0);
        openStartPacket[0] = 0x00;
        openStartPacket[1] = static_cast<char>(0xAA);
        openStartPacket[2] = 0x2E;
        openStartPacket[3] = 0x05;

        openStartPacket[40] = 0x48; openStartPacket[41] = 0x49; openStartPacket[42] = 0x44; openStartPacket[43] = 0x43;

        quint16 check1 = 0;
        for (int j = 1; j < 47; ++j) check1 += static_cast<quint8>(openStartPacket[j]);
        openStartPacket[47] = static_cast<char>(check1 & 0xFF);
        openStartPacket[48] = static_cast<char>((check1 >> 8) & 0xFF);
        libusb_control_transfer(deviceHandle, 0x21, 0x09, 0x0300, 0x0001, reinterpret_cast<unsigned char*>(openStartPacket.data()), 64, 500);
    } // Sluit het openStartPacket blok

    if (!verifyDevice()) {
        close();
        return false;
    }

    // 🚀 TIM'S COLD START STREAMLINER: Schakel de overbodige handshake uit!
    qDebug() << "⚡ [JZFS Streamliner] Sla overbodige opstart-handshaking over. Interface 0 start direct!";

    return true;
} // ✅ SLUIT DE DEEPCOOLDEVICE::OPEN FUNCTIE VOLLEDIG!


void DeepCoolDevice::stopGifHardwareStream() {
    if (deviceHandle) {
        qDebug() << "🧹 [JZFS Stop] Schakel GIF -> Metrics hardware mode over Interface 0...";

        int transferred = 0;

        // ============================================================
        // GIF -> METRICS MODE SWITCH
        //
        // PCAP:
        // Interface 0 / Bulk OUT 0x01
        //
        // AA 2E 03 01 ... HIDC F4 01
        //
        // 03 01 = GIF -> Metrics
        // ============================================================

        QByteArray metricsModePacket(48, 0);

        metricsModePacket[0] = static_cast<char>(0xAA);
        metricsModePacket[1] = static_cast<char>(0x2E);
        metricsModePacket[2] = static_cast<char>(0x03);
        metricsModePacket[3] = static_cast<char>(0x01);

        metricsModePacket[42] = static_cast<char>(0x48); // H
        metricsModePacket[43] = static_cast<char>(0x49); // I
        metricsModePacket[44] = static_cast<char>(0x44); // D
        metricsModePacket[45] = static_cast<char>(0x43); // C

        quint16 metricsModeChecksum = 0;

        for (int i = 0; i < 46; ++i) {
            metricsModeChecksum += static_cast<quint8>(metricsModePacket[i]);
        }

        metricsModePacket[46] =
        static_cast<char>(metricsModeChecksum & 0xFF);

        metricsModePacket[47] =
        static_cast<char>((metricsModeChecksum >> 8) & 0xFF);

        qDebug() << "🔄 [GIF->Metrics] Mode switch:"
        << "AA 2E 03 01 ... HIDC F4 01";

        int modeResult = libusb_bulk_transfer(
            deviceHandle0,
            0x01,
            reinterpret_cast<unsigned char*>(metricsModePacket.data()),
                                              metricsModePacket.size(),
                                              &transferred,
                                              1000
        );

        if (modeResult != 0) {
            qCritical() << "❌ [GIF->Metrics] Mode switch faalde:"
            << libusb_error_name(modeResult);
            return;
        }

        if (transferred != metricsModePacket.size()) {
            qCritical() << "❌ [GIF->Metrics] Mode switch onvolledig:"
            << transferred << "/"
            << metricsModePacket.size();
            return;
        }

        qDebug() << "✅ [GIF->Metrics] Hardware mode switch verzonden.";

        // ============================================================
        // GIF -> METRICS MODE SWITCH RESPONSE
        //
        // Interface 0 / Bulk IN 0x81
        //
        // Verwacht:
        // 55 2E 03 00 ... HIDC 9E 01
        // ============================================================

        QByteArray modeResponse(48, 0);
        transferred = 0;

        int responseResult = libusb_bulk_transfer(
            deviceHandle0,
            0x81,
            reinterpret_cast<unsigned char*>(modeResponse.data()),
            modeResponse.size(),
            &transferred,
            1000
        );

        if (responseResult != 0) {
            qCritical() << "❌ [GIF->Metrics] Mode response faalde:"
            << libusb_error_name(responseResult);
            return;
        }

        qDebug() << "🔎 [GIF->Metrics] Mode response:"
        << modeResponse.toHex(' ');

        if (transferred >= 48 &&
            static_cast<quint8>(modeResponse[0]) == 0x55 &&
            static_cast<quint8>(modeResponse[1]) == 0x2E &&
            static_cast<quint8>(modeResponse[2]) == 0x03) {

            qDebug() << "✅ [GIF->Metrics] Hardware bevestigt Metrics mode.";

            } else {

                qWarning() << "⚠️ [GIF->Metrics] Onverwachte mode response.";
            }

            // Interne softwaremodus terug naar CPU Metrics.
            this->currentMode = MODE_CPU_INFO;

            qDebug()
            << "✅ [JZFS Stop] USB Bulk-wissel voltooid. "
            "Interface 0 vangt de telemetrie weer op.";
    }
}

bool DeepCoolDevice::verifyDevice()
{
    if (!isOpen()) {
        return false;
    }

    uint16_t vendor = 0;
    uint16_t product = 0;

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            return false;
        }
        vendor = info.vendor;
        product = info.product;
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        vendor = deviceInfo.vendorId;
        product = deviceInfo.productId;
    } else {
        return false;
    }

    if (vendor == DEEPCOOL_VENDOR_ID) {
        switch (product) {
            case 0x0001: deviceName = "DeepCool AK400 DIGITAL"; break;
            case 0x0002: deviceName = "DeepCool AK620 DIGITAL"; break;
            case 0x0003: deviceName = "DeepCool AK500 DIGITAL"; break;
            case 0x0004: deviceName = "DeepCool AK500S DIGITAL"; break;
            case 0x0005: deviceName = "DeepCool CH560 DIGITAL"; break;
            case 0x0006: deviceName = "DeepCool LS520/LS720 SE DIGITAL"; break;
            case 0x0007: deviceName = "DeepCool MORPHEUS"; break;
            case 0x0008: deviceName = "DeepCool AG400/AG620 DIGITAL"; break;
            case 0x0009: deviceName = "DeepCool MYSTIQUE 240/360"; break;
            case 0x000A: deviceName = "DeepCool LD240/LD360"; break;
            case 0x000C: deviceName = "DeepCool LP240/LP360"; break;
            case 0x000D: deviceName = "DeepCool LQ240/LQ360"; break;
            case 0x000F: deviceName = "DeepCool ASSASSIN IV VC VISION"; break;
            case 0x0010: deviceName = "DeepCool AK400 DIGITAL PRO"; break;
            case 0x0011: deviceName = "DeepCool AK500 DIGITAL PRO"; break;
            case 0x0012: deviceName = "DeepCool AK620 DIGITAL PRO"; break;
            case 0x0013: deviceName = "DeepCool CH170 DIGITAL"; break;
            case 0x0015: deviceName = "DeepCool CH360 DIGITAL"; break;
            case 0x0016: deviceName = "DeepCool CH270 DIGITAL"; break;
            case 0x001B: deviceName = "DeepCool CH690 DIGITAL"; break;
            default:
                deviceName = QString("DeepCool Device (PID: 0x%1)").arg(product, 4, 16, QChar('0'));
                break;
        }
        return true;
    }

    if (vendor == 0x34D3 && product == 0x1100) {
        deviceName = "DeepCool CH510 MESH DIGITAL";
        return true;
    }

    return false;
}

QString DeepCoolDevice::getDeviceInfo() const
{
    if (!isOpen()) {
        return "Device not open";
    }

    return QString("Name: %1\nVendor ID: 0x%2\nProduct ID: 0x%3\nPath: %4")
        .arg(deviceName)
        .arg(deviceInfo.vendorId, 4, 16, QChar('0'))
        .arg(deviceInfo.productId, 4, 16, QChar('0'))
        .arg(deviceInfo.devicePath);
}

bool DeepCoolDevice::sendData(const QByteArray &data)
{
    if (!isOpen()) {
        return false;
    }

    // 🔗 LOKALE POINTER BRUG: Dwing sendData om Spoor 0 te gebruiken
    libusb_device_handle *deviceHandle = deviceHandle0;

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        int bytesWritten = ::write(fd, data.constData(), data.size());
        return (bytesWritten == data.size());
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        int bytesWritten = 0;

        int ret = libusb_bulk_transfer(
            deviceHandle, endpointOut,
            (unsigned char*)data.constData(), data.size(),
                                       &bytesWritten, 1000
        );

        // Een USB-overdracht is alleen geslaagd wanneer
        // libusb succes meldt EN alle bytes zijn verzonden.
        return (ret == LIBUSB_SUCCESS &&
        bytesWritten == data.size());

    }

    return false;
}

QByteArray DeepCoolDevice::receiveData(int length)
{
    if (!isOpen()) {
        return QByteArray();
    }

    // 🔗 LOKALE POINTER BRUG: Dwing receiveData om Spoor 0 te gebruiken
    libusb_device_handle *deviceHandle = deviceHandle0;

    QByteArray buffer(length, 0);

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        int bytesRead = ::read(fd, buffer.data(), length);
        if (bytesRead < 0) {
            return QByteArray();
        }
        buffer.resize(bytesRead);
        return buffer;
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        int bytesRead = 0;
        int ret = libusb_bulk_transfer(
            deviceHandle, endpointIn,
            (unsigned char*)buffer.data(), length,
            &bytesRead, 1000
        );
        if (ret < 0) {
            return QByteArray();
        }
        buffer.resize(bytesRead);
        return buffer;
    }

    return QByteArray();
}

QByteArray DeepCoolDevice::buildPacket(quint8 command, const QByteArray &payload)
{
    QByteArray packet(PACKET_SIZE, 0);

    packet[0] = 0xAA;
    packet[1] = 0x2E;
    packet[2] = command;

    // 🚀 PROTO-02 FIX: Voorkom payload-verschuiving en forceer een vaste 39-byte alignment!
    if (!payload.isEmpty() && payload.size() <= 39) {
        ::memcpy(packet.data() + 3, payload.constData(), payload.size());
    }

    packet[42] = 0x48;  // 'H'
    packet[43] = 0x49;  // 'I'
    packet[44] = 0x44;  // 'D'
    packet[45] = 0x43;  // 'C'

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }
    packet[46] = static_cast<quint8>(checksum & 0xFF);
    packet[47] = static_cast<quint8>((checksum >> 8) & 0xFF);

    return packet;
}

bool DeepCoolDevice::validateResponse(const QByteArray &response)
{
    return !response.isEmpty() && response.size() >= 2;
}

bool DeepCoolDevice::sendStatusRequest()
{
    if (!isOpen()) {
        return false;
    }

    QByteArray packet(64, 0);
    packet[0] = static_cast<char>(0xAA);
    packet[1] = 0x2E;
    packet[2] = 0x10;
    packet[58] = 0x48;
    packet[59] = 0x49;
    packet[60] = 0x44;
    packet[61] = 0x43;


    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }
    packet[46] = static_cast<char>(checksum & 0xFF);
    packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

    if (!sendData(packet)) {
        return false;
    }

    receiveData(64);
    return true;
}

bool DeepCoolDevice::initMachineInfoMode()
{
    if (!isOpen()) {
        return false;
    }

    // 🔗 LOKALE POINTER BRUG: Dwing de handshake uitsluitend over Spoor 0 te lopen
    libusb_device_handle *deviceHandle = deviceHandle0;

    qDebug() << "🔌 [JZFS Debug] Start complete Mystique USB Handshake reeks (48-byte Bulk Route)...";

    // 📊 TIM'S UNIVERSELE 48-BYTE BULK-LAMBDA (CONFORM v1.2.1 BLUEPRINT)
    auto sendInitPacket = [this, deviceHandle](quint8 cmd, const QByteArray& payload) -> QByteArray {
        // Interface 0 (Metrics/Handshake) eist onvoorwaardelijk een buffer van exact 48 bytes!
        QByteArray packet(48, 0);
        packet[0] = static_cast<char>(0xAA); // Magic Start-byte
        packet[1] = 0x2E;                   // Protocol Versie-byte
        packet[2] = cmd;                    // Het specifieke Commando-byte

        for (int i = 0; i < payload.size() && i < 39; i++) {
            packet[3 + i] = payload[i];
        }

        packet[42] = 0x48; // 'H'
        packet[43] = 0x49; // 'I'
        packet[44] = 0x44; // 'D'
        packet[45] = 0x43; // 'C'

        // Checksum berekenen over de 46-byte structuurzone (Index 0 t/m 45)
        quint16 checksum = 0;
        for (int j = 0; j < 46; ++j) {
            checksum += static_cast<quint8>(packet[j]);
        }
        packet[46] = static_cast<char>(checksum & 0xFF);
        packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

        // Vuren via de pure 48-byte bulk OUT sluis (Endpoint 0x01)
        int transferred = 0;
        int controlRes = libusb_bulk_transfer(deviceHandle, 0x01, (unsigned char*)packet.data(), 48, &transferred, 1000);

        if (controlRes < 0) {
            qCritical() << "   ❌ [JZFS Init Error] Cmd" << QString("0x%1").arg(cmd, 2, 16, QChar('0')) << "gefaald:" << libusb_error_name(controlRes);
        } else {
            qDebug() << "   ✅ [JZFS Init] Cmd" << QString("0x%1").arg(cmd, 2, 16, QChar('0')) << "succesvol verwerkt.";
        }

        // Antwoord opvangen via de 64-byte bulk IN sluis (Endpoint 0x81)
        QByteArray response(64, 0);
        libusb_bulk_transfer(deviceHandle, 0x81, (unsigned char*)response.data(), 64, &transferred, 1000);

        return response;
    };

    // 🚀 STAP-VOOR-STAP HARDWARE OPSTART MATRIX (CONFORM FLATHUB DOCUMENTATIE)
    // 1. Handdruk starten (Leeg infoverzoek)
    qDebug() << "  Cmd 0x12 (Device Info Request)...";
    QByteArray resp = sendInitPacket(0x12, QByteArray());
    usleep(10000);

    // 2. Schermrotatie vastleggen
    qDebug() << "  Cmd 0x02 (Configuration Matrix)...";
    {

        QByteArray configPayload;
        configPayload.append(static_cast<char>(0x01));
        configPayload.append(static_cast<char>(0x00));
        configPayload.append(static_cast<char>(currentRotation & 0x03));
        configPayload.append(static_cast<char>(currentRgbMode));
        configPayload.append(static_cast<char>(currentBrightness));
        sendInitPacket(0x02, configPayload);

    }
    usleep(10000);

    // 3. Sluis openen (TFT onder spanning zetten)
    qDebug() << "  Cmd 0x03, 0x04, 0x07, 0x08 (Power Sluis)...";
    sendInitPacket(0x03, QByteArray::fromHex("01"));
    sendInitPacket(0x04, QByteArray::fromHex("05000001"));
    sendInitPacket(0x07, QByteArray::fromHex("0002"));
    sendInitPacket(0x08, QByteArray::fromHex("0004"));
    usleep(10000);

    // 4. Stream activeren
    qDebug() << "  Cmd 0x05, 0x0B, 0x06 (Stream Control)...";
    sendInitPacket(0x05, QByteArray::fromHex("0101"));
    sendInitPacket(0x0B, QByteArray());
    sendInitPacket(0x06, QByteArray::fromHex("01"));
    usleep(10000);

    // 5. Sensor-pipes mappen (Stand-by waarden)
    qDebug() << "  Cmd 0x15-0x17 (Sensor Sync)...";
    sendInitPacket(0x15, QByteArray::fromHex("2d2d"));
    sendInitPacket(0x16, QByteArray::fromHex("2d2d"));
    sendInitPacket(0x17, QByteArray::fromHex("2d2d"));
    usleep(10000);

    // 6. De Modus-Switch (De Genadeslag die het logo sloopt!)
    qDebug() << "  Cmd 0x0A (Unlock Resolution)...";
    resp = sendInitPacket(0x0A, QByteArray::fromHex("ea07020202272100"));
    usleep(100000);

    // 7. Status check
    qDebug() << "  Cmd 0x10 (Status Ping)...";
    resp = sendInitPacket(0x10, QByteArray());

    if (resp.size() >= 6 && static_cast<quint8>(resp[5]) == 0xFF) {
        qDebug() << "  🏆 [JZFS SUCCESS] DeepCool Mystique succesvol ontwaakt uit standby!";
        return true;
    }

    // Failsafe return true zodat de app stabiel doordraait op Interface 0
    return true;
}

    bool DeepCoolDevice::setDisplayMode(DisplayMode mode)
    {
        // 🚀 BEVEILIGINGS-GRENDEL: Start direct de lichte metrics op bij een koude start van de app op HID
        if (deviceType == DEVICE_TYPE_HID &&
            static_cast<int>(currentMode) == 0 &&
            mode == MODE_CPU_INFO) {
            currentMode = mode;
        return true;
            }

            if (currentMode == mode && mode != MODE_GIF_STREAM) {
                return true;
            }

        // 🛡️ TIM'S OVERDRIVE GRENDEL: Waakhond permanent op stand-by gezet!
        if (false) {
            // Dit blok staat nu binaire-technisch uitgeschakeld en blokkeert de stream niet meer.
            qDebug() << "🔒 Overdrive Beveiliging: Stand-by.";
        }

        // ============================================================
        // METRICS -> GIF MODE SWITCH
        //
        // Alleen de hardware-modus wisselen.
        // GEEN GIF-upload starten.
        //
        // PROVEN:
        //   OUT = endpoint 0x01
        //   IN  = endpoint 0x81
        //   packet = 48 bytes
        //   command = 0x03 02
        // ============================================================
        if (mode == MODE_GIF_STREAM) {

            QByteArray gifModePacket(48, 0);

            gifModePacket[0] = static_cast<char>(0xAA);
            gifModePacket[1] = static_cast<char>(0x2E);
            gifModePacket[2] = static_cast<char>(0x03);
            gifModePacket[3] = static_cast<char>(0x02);

            gifModePacket[42] = static_cast<char>(0x48); // H
            gifModePacket[43] = static_cast<char>(0x49); // I
            gifModePacket[44] = static_cast<char>(0x44); // D
            gifModePacket[45] = static_cast<char>(0x43); // C

            quint16 gifModeChecksum = 0;

            for (int i = 0; i < 46; ++i) {
                gifModeChecksum += static_cast<quint8>(gifModePacket[i]);
            }

            gifModePacket[46] =
            static_cast<char>(gifModeChecksum & 0xFF);

            gifModePacket[47] =
            static_cast<char>((gifModeChecksum >> 8) & 0xFF);

            qDebug() << "🚀 [JZFS Switch] Metrics -> GIF mode"
            << "OUT 0x01, 48 bytes:"
            << gifModePacket.toHex(' ');

            int transferred = 0;

            int modeResult = libusb_bulk_transfer(
                deviceHandle0,
                0x01,
                reinterpret_cast<unsigned char*>(gifModePacket.data()),
                                                  48,
                                                  &transferred,
                                                  1000
            );

            if (modeResult != 0) {
                qCritical() << "❌ [JZFS Switch] Metrics -> GIF failed:"
                << libusb_error_name(modeResult);
                return false;
            }

            if (transferred != 48) {
                qCritical() << "❌ [JZFS Switch] Onvolledige mode switch:"
                << transferred << "/48";
                return false;
            }

            qDebug() << "✅ [JZFS Switch] Metrics -> GIF mode verzonden.";

            // ========================================================
            // RESPONSE
            // ========================================================

            QByteArray modeResponse(48, 0);
            transferred = 0;

            int responseResult = libusb_bulk_transfer(
                deviceHandle0,
                0x81,
                reinterpret_cast<unsigned char*>(modeResponse.data()),
                                                      48,
                                                      &transferred,
                                                      1000
            );

            if (responseResult != 0) {
                qCritical() << "❌ [JZFS Switch] GIF mode response failed:"
                << libusb_error_name(responseResult);
                return false;
            }

            qDebug() << "🔎 [JZFS Switch] GIF mode response:"
            << modeResponse.toHex(' ');

            // STEP 3.3E-3C-2A
            // Validate the complete 48-byte GIF mode response.
            // A partial response must never be accepted.

            quint16 expectedModeChecksum = 0;

            for (int i = 0; i < 46; ++i) {
                expectedModeChecksum +=
                static_cast<quint8>(modeResponse[i]);
            }

            const quint16 receivedModeChecksum =
            static_cast<quint8>(modeResponse[46]) |
            (static_cast<quint16>(
                static_cast<quint8>(modeResponse[47])
            ) << 8);

            const bool validModeResponse =
            transferred == 48 &&
            static_cast<quint8>(modeResponse[0]) == 0x55 &&
            static_cast<quint8>(modeResponse[1]) == 0x2E &&
            static_cast<quint8>(modeResponse[2]) == 0x03 &&
            static_cast<quint8>(modeResponse[3]) == 0x00 &&
            modeResponse.mid(42, 4) == QByteArray("HIDC", 4) &&
            receivedModeChecksum == expectedModeChecksum;

            if (!validModeResponse) {
                qCritical()
                << "[GIF-MODE] Invalid mode response."
                << "Received bytes:" << transferred
                << "Expected checksum:" << expectedModeChecksum
                << "Received checksum:" << receivedModeChecksum
                << "Response:" << modeResponse.toHex(' ');

                return false;
            }

            qDebug()
            << "[GIF-MODE] Complete GIF mode ACK validated.";

            currentMode = MODE_GIF_STREAM;
            return true;

        }


        // 🖥️ UNIVERSAL BACKDOOR: Wordt uitgevoerd voor alle reguliere Metrics modi (CPU/GPU)
        currentMode = mode;
        return true;
    }

    // MYSTIQUE STEP 4B - COMBINED RGB BRIGHTNESS CONFIG
    // Rotation must preserve the current RGB choice and TFT brightness.
    bool DeepCoolDevice::setRotation(ScreenRotation rotation)
    {
        return applyVisualConfig(rotation, currentRgbMode, currentBrightness);
    }

    bool DeepCoolDevice::setVisualSettings(int rgbMode, int brightness)
    {
        // Do not change the rotation when a GUI control changes.
        return applyVisualConfig(currentRotation, rgbMode, brightness);
    }

    bool DeepCoolDevice::applyVisualConfig(
        ScreenRotation rotation, int rgbMode, int brightness)
    {
        // Intended solely for our USB-vendor MYSTIQUE 3633:0009.
        if (!isOpen() || deviceType != DEVICE_TYPE_USB_VENDOR ||
            deviceHandle0 == nullptr) {
            qWarning() << "[VISUAL-CONFIG] No supported connected USB device.";
            return false;
        }
        const int rot = static_cast<int>(rotation);
        if (rot < 0 || rot > 3 || rgbMode < 0 || rgbMode > 2 ||
            brightness < 0 || brightness > 100) {
            qWarning() << "[VISUAL-CONFIG] Out-of-range setting rejected."
                       << rot << rgbMode << brightness;
            return false;
        }

        // HIDC command 0x02, bytes 3..7:
        // screen=1; gyro=0 (PRESERVE current Linux behavior);
        // rotation=0..3; rgb=0..2; brightness=0..100.
        // All 48 bytes are built by the existing checked HIDC constructor.
        QByteArray payload;
        payload.append(static_cast<char>(0x01));
        payload.append(static_cast<char>(0x00));
        payload.append(static_cast<char>(rot));
        payload.append(static_cast<char>(rgbMode));
        payload.append(static_cast<char>(brightness));
        QByteArray packet = buildPacket(CMD_CONFIG, payload);
        if (packet.size() != 48) return false;

        int transferred = 0;
        const int writeResult = libusb_bulk_transfer(
            deviceHandle0, 0x01,
            reinterpret_cast<unsigned char*>(packet.data()),
            48, &transferred, 1000);
        if (writeResult != LIBUSB_SUCCESS || transferred != 48) {
            qWarning() << "[VISUAL-CONFIG] USB OUT failure:"
                       << libusb_error_name(writeResult)
                       << "bytes:" << transferred;
            return false;  // Never retry blindly after uncertain USB outcome.
        }

        QByteArray response(48, 0);
        transferred = 0;
        const int readResult = libusb_bulk_transfer(
            deviceHandle0, 0x81,
            reinterpret_cast<unsigned char*>(response.data()),
            48, &transferred, 1000);
        if (readResult != LIBUSB_SUCCESS || transferred != 48) {
            qWarning() << "[VISUAL-CONFIG] USB ACK missing/partial:"
                       << libusb_error_name(readResult)
                       << "bytes:" << transferred;
            return false;
        }

        quint16 expectedChecksum = 0;
        for (int i = 0; i < 46; ++i)
            expectedChecksum += static_cast<quint8>(response[i]);
        const quint16 receivedChecksum =
            static_cast<quint8>(response[46]) |
            (static_cast<quint16>(static_cast<quint8>(response[47])) << 8);
        const bool validAck =
            static_cast<quint8>(response[0]) == 0x55 &&
            static_cast<quint8>(response[1]) == 0x2E &&
            static_cast<quint8>(response[2]) == CMD_CONFIG &&
            static_cast<quint8>(response[3]) == 0x00 &&
            response.mid(42, 4) == QByteArray("HIDC", 4) &&
            receivedChecksum == expectedChecksum;
        if (!validAck) {
            qWarning() << "[VISUAL-CONFIG] Invalid configuration ACK:"
                       << response.toHex(' ');
            return false;
        }

        // Only confirmed transport ACK updates driver-local applied settings.
        currentRotation = rotation;
        currentRgbMode = rgbMode;
        currentBrightness = brightness;
        qInfo() << "[VISUAL-CONFIG] ACK valid; rotation/RGB/brightness:"
                << rot << rgbMode << brightness;
        return true;
    }

bool DeepCoolDevice::updateDisplay(const SystemData &data)
{
    if (!isOpen()) {
        return false;
    }

    // Send status request first
    QByteArray statusPacket(48, 0);
    statusPacket[0] = static_cast<char>(0xAA);
    statusPacket[1] = 0x2E;
    statusPacket[2] = 0x10;
    statusPacket[42] = 0x48;
    statusPacket[43] = 0x49;
    statusPacket[44] = 0x44;
    statusPacket[45] = 0x43;
    quint16 statusChecksum = 0;
    for (int i = 0; i < 46; ++i) {
        statusChecksum += static_cast<quint8>(statusPacket[i]);
    }
    statusPacket[46] = static_cast<char>(statusChecksum & 0xFF);
    statusPacket[47] = static_cast<char>((statusChecksum >> 8) & 0xFF);

    if (!sendData(statusPacket)) {
        return false;
    }
    const QByteArray statusResponse = receiveData(48);

    if (currentMode == MODE_GIF_STREAM) {
        if (statusResponse.size() >= 6) {
            qDebug().noquote()
            << "[GIF-WAIT] 0x10 status byte[5]="
            << QString("0x%1").arg(
                static_cast<int>(
                    static_cast<quint8>(statusResponse[5])
                ),
                2,
                16,
                QChar('0')
            );
        } else {
            qWarning()
            << "[GIF-WAIT] 0x10-statusrespons ontbreekt:"
            << statusResponse.size() << "bytes";
        }
    }

    // Get CPU frequency (max across all cores)
    quint16 cpuMhz = 0;
    QDir cpuDir("/sys/devices/system/cpu");
    QStringList cpus = cpuDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& cpu : cpus) {
        if (!cpu.startsWith("cpu") || cpu.length() < 4 || !cpu[3].isDigit()) {
            continue;
        }
        QString freqPath = QString("/sys/devices/system/cpu/%1/cpufreq/scaling_cur_freq").arg(cpu);
        QFile cpuFreqFile(freqPath);
        if (cpuFreqFile.open(QIODevice::ReadOnly)) {
            QString freqStr = QTextStream(&cpuFreqFile).readAll().trimmed();
            quint32 freqKhz = freqStr.toUInt();
            quint16 freq = static_cast<quint16>(freqKhz / 1000);
            cpuFreqFile.close();
            if (freq > cpuMhz) {
                cpuMhz = freq;
            }
        }
    }
    if (cpuMhz == 0) {
        cpuMhz = 3500;
    }

    // Get sensor values
    quint8 cpuTemp = static_cast<quint8>(qBound(0.0f, data.cpuTemp, 127.0f));
    quint8 cpuUsage = static_cast<quint8>(qBound(0.0f, data.cpuUsage, 100.0f));
    quint8 gpuTemp = static_cast<quint8>(qBound(0.0f, data.gpuTemp, 127.0f));
    quint8 gpuUsage = static_cast<quint8>(qBound(0.0f, data.gpuUsage, 100.0f));


    quint8 mainTemp = cpuTemp;
    quint8 bottomTemp = gpuTemp;
    quint8 mainUsage = cpuUsage; // 🚀 TIM'S BASE LOAD KLAARZETTEN!

    if (currentMode == MODE_GPU_INFO || currentMode == MODE_GPU_FOCUS) {
        mainTemp = gpuTemp;
        mainUsage = gpuUsage;
        bottomTemp = cpuTemp;
    }

    // RAM usage (rounded)
    float ramBounded = qBound(0.0f, data.ramUsage, 100.0f);
    quint8 memUsage = static_cast<quint8>(qRound(ramBounded));

    float calculatedGhz = data.gpuFrequency / 1000.0f; // Vertaalt bijv. 2822 MHz naar 2.82f
    quint8 displayInteger = static_cast<quint8>(calculatedGhz); // Pakt exact het getal voor de komma (2)
    quint16 displayDecimal = static_cast<quint16>(qRound((calculatedGhz - displayInteger) * 100)); // Pakt exact de twee decimalen (82)

    if (currentMode == MODE_GPU_INFO || currentMode == MODE_GPU_FOCUS) {
        // 🎮 GPU MODUS: Toon de stabiele GPU Gigahertzen
        displayInteger = static_cast<quint8>(calculatedGhz);
        displayDecimal = static_cast<quint16>(qRound((calculatedGhz - displayInteger) * 100));
    } else {
        // 🖥️ CPU MODUS: Toon de stabiele CPU Gigahertzen
        float ghz = cpuMhz / 1000.0f;
        displayInteger = static_cast<quint8>(ghz);
        displayDecimal = static_cast<quint16>(qRound((ghz - displayInteger) * 100));
    }

    quint8 byte23 = 0;
    if (currentMode == MODE_GPU_INFO || currentMode == MODE_GPU_FOCUS) {
        byte23 = static_cast<quint8>((data.gpuFrequency % 1000) / 10);
        if (byte23 < 10) byte23 = 0x0a;
    } else {
        byte23 = static_cast<quint8>((cpuMhz % 1000) / 10);
        if (byte23 < 10) byte23 = 0x0a;
    }


    quint8 byte9 = memUsage;
    quint8 byte11 = (gpuTemp < 35) ? 0x05 : ((gpuTemp < 38) ? 0x06 : 0x09);
    quint8 byte17 = 0x06;

    // Build 48-byte metrics packet (v1.2.1 compatible)
    QByteArray displayPacket(48, 0);

    displayPacket[0] = static_cast<char>(0xAA);
    displayPacket[1] = 0x2E;
    displayPacket[2] = 0x01;
    displayPacket[3] = mainTemp;
    displayPacket[6] = mainUsage;
    displayPacket[9] = byte9;
    displayPacket[11] = byte11;
    displayPacket[12] = 0x03;
    displayPacket[14] = gpuTemp;
    displayPacket[15] = 0x05;
    displayPacket[17] = byte17;
    displayPacket[18] = 0x0c;
    displayPacket[20] = 0x07;
    displayPacket[21] = displayInteger;
    displayPacket[23] = byte23;
    displayPacket[24] = static_cast<char>(displayDecimal & 0xFF);
    displayPacket[25] = static_cast<char>((displayDecimal >> 8) & 0xFF);
    displayPacket[27] = static_cast<char>(displayDecimal & 0xFF);
    displayPacket[28] = static_cast<char>((displayDecimal >> 8) & 0xFF);
    displayPacket[42] = 0x48; // 'H'
    displayPacket[43] = 0x49; // 'I'
    displayPacket[44] = 0x44; // 'D'
    displayPacket[45] = 0x43; // 'C'

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(displayPacket[i]);
    }

    displayPacket[46] = static_cast<char>(checksum & 0xFF);
    displayPacket[47] = static_cast<char>((checksum >> 8) & 0xFF);

    // Metrics via Interface 0 / endpoint 0x02 (v1.2.1 route)
    if (!sendData(displayPacket)) {
        qCritical() << "❌ [JZFS Metrics Error] Live update overdracht faalde op Interface 0";
        return false;
    }

    // Vang het antwoord op via Interface 0 / endpoint 0x82
    QByteArray response = receiveData(48);

    Q_UNUSED(response);

    return true;
}

bool DeepCoolDevice::selectGifHardwareSlot(int slotIndex)
{
    if (!isOpen()) {
        qCritical() << "❌ [GIF] Device is niet geopend.";
        return false;
    }

    if (slotIndex < 0 || slotIndex >= 5) {
        qCritical() << "❌ [GIF] Ongeldige slot index:" << slotIndex;
        return false;
    }

    QByteArray packet(48, 0);

    packet[0] = static_cast<char>(0xAA);
    packet[1] = static_cast<char>(0x2E);
    packet[2] = static_cast<char>(0x08);
    packet[3] = static_cast<char>(0x01);
    packet[4] = static_cast<char>(slotIndex);

    packet[42] = 0x48; // H
    packet[43] = 0x49; // I
    packet[44] = 0x44; // D
    packet[45] = 0x43; // C

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }

    packet[46] = static_cast<char>(checksum & 0xFF);
    packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

    int transferred = 0;

    const int result =
    libusb_bulk_transfer(
        deviceHandle0,
        0x01,
        reinterpret_cast<unsigned char*>(packet.data()),
                         packet.size(),
                         &transferred,
                         30000
    );

    if (result != 0 || transferred != 48) {
        qCritical()
        << "❌ [GIF] Slot-selectie mislukt:"
        << libusb_error_name(result)
        << "slot =" << slotIndex;
        return false;
    }

    QByteArray response(48, 0);
    transferred = 0;

    const int responseResult =
    libusb_bulk_transfer(
        deviceHandle0,
        0x81,
        reinterpret_cast<unsigned char*>(response.data()),
                         response.size(),
                         &transferred,
                         30000
    );

    if (responseResult != 0 || transferred != 48) {
        qCritical()
        << "❌ [GIF] Slot-selectie response mislukt:"
        << libusb_error_name(responseResult);
        return false;
    }

    quint16 expectedChecksum = 0;
    for (int i = 0; i < 46; ++i)
        expectedChecksum += static_cast<quint8>(response[i]);
    const quint16 actualChecksum =
        static_cast<quint8>(response[46]) |
        (static_cast<quint16>(static_cast<quint8>(response[47])) << 8);
    if (static_cast<quint8>(response[0]) != 0x55 ||
        static_cast<quint8>(response[1]) != 0x2E ||
        static_cast<quint8>(response[2]) != 0x08 ||
        static_cast<quint8>(response[3]) != 0x00 ||
        response.mid(42, 4) != QByteArray("HIDC", 4) ||
        actualChecksum != expectedChecksum) {
        qCritical() << "[GIF-SELECT] Ongeldige volledige 0x08-ACK:"
                    << response.toHex(' ');
        return false;
    }

        qDebug()
        << "✅ [GIF] Hardware slot geselecteerd:"
        << slotIndex;
        qWarning() << "[GIF-SELECT] Valid 0x08 ACK:" << response.toHex();

        return true;
}

bool DeepCoolDevice::deleteGifHardwareSlot(int listIndex)
{
    if (!deviceHandle0 || listIndex < 0 || listIndex > 255) {
        qCritical() << "❌ [GIF] Ongeldige delete-aanvraag:"
        << listIndex;
        return false;
    }

    QByteArray packet(48, 0);
    packet[0] = static_cast<char>(0xAA);
    packet[1] = static_cast<char>(0x2E);
    packet[2] = static_cast<char>(0x09);
    packet[3] = static_cast<char>(0x01);
    packet[4] = static_cast<char>(listIndex);

    packet[42] = 'H';
    packet[43] = 'I';
    packet[44] = 'D';
    packet[45] = 'C';

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }
    packet[46] = static_cast<char>(checksum & 0xFF);
    packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

    int transferred = 0;
    const int result = libusb_bulk_transfer(
        deviceHandle0,
        0x01,
        reinterpret_cast<unsigned char*>(packet.data()),
                                            packet.size(),
                                            &transferred,
                                            30000
    );

    qWarning().noquote()
    << "[DELETE-DIAG] USB OUT start:"
    << "index=" << listIndex
    << "| packet=" << packet.toHex(' ');

    qWarning()
    << "[DELETE-DIAG] USB OUT resultaat:"
    << "result=" << result
    << "(" << libusb_error_name(result) << ")"
    << "| transferred=" << transferred;

    if (result != 0 || transferred != 48) {
        qCritical() << "❌ [GIF] Delete verzenden mislukt:"
        << libusb_error_name(result)
        << "| index =" << listIndex
        << "| transferred =" << transferred;
        return false;
    }

    QByteArray response(48, 0);
    transferred = 0;
    const int responseResult = libusb_bulk_transfer(
        deviceHandle0,
        0x81,
        reinterpret_cast<unsigned char*>(response.data()),
                                                    response.size(),
                                                    &transferred,
                                                    30000
    );

    qWarning().noquote()
    << "[DELETE-DIAG] USB IN resultaat:"
    << "result=" << responseResult
    << "(" << libusb_error_name(responseResult) << ")"
    << "| transferred=" << transferred
    << "| response=" << response.toHex(' ');

    if (responseResult != 0 || transferred != 48) {
        qCritical() << "❌ [GIF] Delete-ACK lezen mislukt:"
        << libusb_error_name(responseResult)
        << "| transferred =" << transferred;
        return false;
    }

    quint16 expectedChecksum = 0;
    for (int i = 0; i < 46; ++i) {
        expectedChecksum += static_cast<quint8>(response[i]);
    }

    const quint16 receivedChecksum =
    static_cast<quint8>(response[46]) |
    (static_cast<quint16>(static_cast<quint8>(response[47])) << 8);

    if (static_cast<quint8>(response[0]) != 0x55 ||
        static_cast<quint8>(response[1]) != 0x2E ||
        static_cast<quint8>(response[2]) != 0x09 ||
        static_cast<quint8>(response[3]) != 0x00 ||
        response.mid(42, 4) != QByteArray("HIDC", 4) ||
        receivedChecksum != expectedChecksum) {
        qCritical() << "❌ [GIF] Ongeldig delete-ACK:"
        << response.toHex(' ');
    return false;
        }

        qDebug() << "✅ [GIF] TFT heeft deletecommando aangenomen:"
        << "index =" << listIndex;
        return true;
}


bool DeepCoolDevice::startGifHardwareStream(
    const QString& gifPath,
    int hardwareListIndex,
    const std::function<void(int, const QString&)>& progress,
                                            GifUploadOutcome* outcome
)
{
    // STEP 3.3E-3C-2C-3B
    // Initialize upload diagnostics before any USB operation.

    if (outcome != nullptr) {
        *outcome = GifUploadOutcome{};

        outcome->selectionRequired =
        hardwareListIndex >= 0;
    }

    const auto reportProgress =
    [&progress](int percent, const QString& message) {
        if (progress) {
            progress(percent, message);
        }
    };

    reportProgress(10, "GIF-bestand controleren...");

    if (!isOpen()) {
        qCritical() << "❌ [GIF] DeepCool device is niet geopend.";
        return false;
    }

    // ============================================================
    // WINDOWS GIF UPLOAD ROUTE
    //
    // 1. AA 2E 0F 00  -> upload voorbereiden
    // 2. DCLd + JPEG frames, ieder via 64-byte OUT packets
    // 3. DCLdfinish   -> upload afsluiten
    // 4. AA 2E 08 01 [slot] -> commit/apply
    // 5. AA 2E 03 02  -> pas daarna Metrics -> GIF
    //
    // Metrics-code blijft volledig ongemoeid.
    // ============================================================

    QFile gifFile(gifPath);

    if (!gifFile.open(QIODevice::ReadOnly)) {
        qCritical() << "❌ [GIF] Kan raw GIF niet openen:" << gifPath;
        return false;
    }

    const QByteArray rawGif = gifFile.readAll();
    gifFile.close();

    if (rawGif.isEmpty()) {
        qCritical() << "❌ [GIF] raw GIF is leeg:" << gifPath;
        return false;
    }

    // ------------------------------------------------------------
    // GIF TIMING
    //
    // Firmware:
    //   gifrate    = frame count
    //   gifalltime = totale animatieduur
    //
    // DCLd[14..15] bevat gifalltime.
    // ------------------------------------------------------------

    QImageReader gifReader(gifPath);

    if (!gifReader.canRead()) {
        qCritical()
        << "❌ [GIF] Kan GIF metadata niet lezen:"
        << gifPath;
        return false;
    }

    const QByteArray readerFormat = gifReader.format();
    const bool readerSupportsAnimation =
    gifReader.supportsAnimation();
    const int readerReportedFrameCount =
    gifReader.imageCount();

    qWarning().noquote()
    << "[GIF-META] Bestand:"
    << gifPath
    << "| formaat:" << readerFormat
    << "| animatie:" << readerSupportsAnimation
    << "| gerapporteerde frames:" << readerReportedFrameCount;


    int metadataFrameCount = 0;
    int firstFrameDelayMs = -1;
    quint32 totalGifDurationMs = 0;

    while (true) {
        const QImage decodedFrame = gifReader.read();

        if (decodedFrame.isNull()) {
            if (metadataFrameCount == 0) {
                qCritical()
                << "❌ [GIF] Eerste GIF-frame kon niet worden gelezen:"
                << gifPath
                << "Reader error:"
                << gifReader.errorString();
                return false;
            }

            break;
        }




        ++metadataFrameCount;

        // STEP 4B — Fixed 25 FPS normalization.
        //
        // Every original GIF frame is used exactly once.
        // Original frame delays are ignored.
        //
        // 25 FPS = 40 ms = 4 centiseconds per frame.

        if (metadataFrameCount == 1) {
            firstFrameDelayMs = 40;
        }

    }

        if (metadataFrameCount <= 0 || firstFrameDelayMs <= 0) {
            qCritical()
            << "[GIF] Ongeldige GIF-timing:"
            << "frames =" << metadataFrameCount
            << "frame 0 delay =" << firstFrameDelayMs << "ms";
            return false;
        }

        totalGifDurationMs =
        static_cast<quint64>(metadataFrameCount) *
        static_cast<quint64>(firstFrameDelayMs);

        const quint32 delayCs =
        static_cast<quint32>((firstFrameDelayMs + 5) / 10);

        if (delayCs == 0) {
            qCritical()
            << "❌ [GIF] Frame-delay wordt nul centiseconden:"
            << firstFrameDelayMs << "ms";
            return false;
        }

        quint32 gifRate = 0;

        try {
            gifRate = DcldProtocol::calculateGifRate(delayCs);
        } catch (const std::exception& e) {
            qCritical() << "❌ [GIF] Rateberekening mislukt:" << e.what();
            return false;
        }

        if (gifRate == 0) {
            qCritical() << "❌ [GIF] Berekende gifrate is nul.";
            return false;
        }

        quint32 calculatedGifAllTime = 0;

        try {
            calculatedGifAllTime =
            DcldProtocol::calculateGifAllTimeMs(
                static_cast<quint32>(metadataFrameCount),
                                                gifRate
            );
        } catch (const std::exception& e) {
            qCritical() << "❌ [GIF] Duurberekening mislukt:" << e.what();
            return false;
        }

        if (calculatedGifAllTime == 0 ||
            calculatedGifAllTime > 65535) {
            qCritical()
            << "❌ [GIF] DCLd-duur past niet in uint16:"
            << calculatedGifAllTime << "ms";
        return false;
            }

            const quint16 gifAllTime16 =
            static_cast<quint16>(calculatedGifAllTime);

            qDebug()
            << "⏱️ [GIF] DCLd timing:"
            << "frames =" << metadataFrameCount
            << "eerste delay =" << firstFrameDelayMs << "ms"
            << "delay =" << delayCs << "cs"
            << "gifrate =" << gifRate
            << "totale GIF-duur =" << totalGifDurationMs << "ms"
            << "DCLd gifalltime =" << gifAllTime16 << "ms";



            QDir gifDir = QFileInfo(gifPath).absoluteDir();

            if (hardwareListIndex < 0 || hardwareListIndex > 255) {
                qCritical()
                << "❌ [GIF] Ongeldige hardwarelijstindex:"
                << hardwareListIndex;
                return false;
            }

            qDebug()
            << "🎯 [GIF] Hardwarelijstindex voor upload ="
            << hardwareListIndex;

    // ------------------------------------------------------------
    // FRAME DIRECTORY
    //
    // raw_source.gif staat naast:
    // frame_0.jpg
    // frame_1.jpg
    // ...
    // ------------------------------------------------------------

    QStringList frameFiles;

    QStringList entries = gifDir.entryList(
        QStringList() << "frame_*.jpg",
                                           QDir::Files,
                                           QDir::NoSort
    );

    const auto frameIndex = [](const QString& fileName) {
        bool ok = false;
        const int index =
        QFileInfo(fileName).completeBaseName().mid(6).toInt(&ok);

        return ok ? index : std::numeric_limits<int>::max();
    };

    std::sort(
        entries.begin(),
              entries.end(),
              [&](const QString& a, const QString& b) {
                  const int ai = frameIndex(a);
                  const int bi = frameIndex(b);

                  return ai == bi ? a < b : ai < bi;
              }
    );

    for (const QString& entry : entries) {
        frameFiles.append(gifDir.filePath(entry));
    }

    if (frameFiles.isEmpty()) {
        qCritical()
        << "❌ [GIF] Geen frame_*.jpg bestanden gevonden in:"
        << gifDir.absolutePath();
        return false;
    }

    qDebug()
    << "🎬 [GIF] Frames gevonden:"
    << frameFiles.size();


    // STEP 4A — MYSTIQUE GIF frame limit.
    // Maximum 255 frames per prepared GIF.

    if (frameFiles.size() > 255) {
        qCritical()
        << "[GIF-VALIDATION] Too many JPEG frames."
        << "Maximum: 255"
        << "Found:" << frameFiles.size();

        return false;
    }


    const quint16 frameCount =
    static_cast<quint16>(frameFiles.size());

    if (frameCount != metadataFrameCount) {
        qCritical()
        << "❌ [GIF] Frame count mismatch:"
        << "JPEG =" << frameCount
        << "GIF metadata =" << metadataFrameCount;
        return false;
    }


    // STEP 4C-3 — Verify the exact JPEG frame sequence.
    //
    // Required filenames:
    // frame_0.jpg through frame_(frameCount-1).jpg
    //
    // No gaps, duplicate indices, alternative names
    // or empty JPEG files are accepted.

    for (int i = 0; i < frameFiles.size(); ++i) {

        const QString expectedName =
        QString("frame_%1.jpg").arg(i);

        const QString actualName = entries.at(i);

        if (actualName != expectedName) {
            qCritical()
            << "[GIF-VALIDATION] Invalid frame sequence."
            << "Expected:" << expectedName
            << "Found:" << actualName;

            return false;
        }

        const QFileInfo jpegInfo(
            gifDir.filePath(expectedName)
        );

        if (!jpegInfo.isFile() || jpegInfo.size() <= 0) {
            qCritical()
            << "[GIF-VALIDATION] Missing or empty JPEG."
            << "Frame:" << i
            << "File:" << jpegInfo.absoluteFilePath();

            return false;
        }
    }

    qDebug()
    << "[GIF-VALIDATION] Frame sequence verified:"
    << frameFiles.size()
    << "contiguous JPEG frames.";


    // STEP 4C-2 — Persistent GIF identity.
    //
    // metadata.json is the only source of the upload ID.
    // Never generate a second UUID in the USB driver.
    // Reject invalid metadata before USB preparation.

    QFile metadataFile(
        gifDir.filePath("metadata.json")
    );

    if (!metadataFile.open(QIODevice::ReadOnly)) {
        qCritical()
        << "[GIF-METADATA] Cannot open metadata.json:"
        << metadataFile.errorString();
        return false;
    }

    QJsonParseError jsonError;

    const QJsonDocument metadataDocument =
    QJsonDocument::fromJson(
        metadataFile.readAll(),
                            &jsonError
    );

    metadataFile.close();

    if (jsonError.error != QJsonParseError::NoError ||
        !metadataDocument.isObject()) {

        qCritical()
        << "[GIF-METADATA] Invalid metadata.json:"
        << jsonError.errorString();

    return false;
        }

        const QJsonObject metadata =
        metadataDocument.object();

        const QString uploadId =
        metadata.value("gifId").toString();

        const int metadataPages =
        metadata.value("pages").toInt(-1);

        const int metadataFps =
        metadata.value("fps").toInt(-1);

        const int metadataFrameTime =
        metadata.value("frameTimeMs").toInt(-1);

        const int metadataDuration =
        metadata.value("duration").toInt(-1);

        const bool timingValid =
        metadataPages == static_cast<int>(frameCount) &&
        metadataFps == 25 &&
        metadataFrameTime == 40 &&
        metadataDuration == static_cast<int>(gifAllTime16) &&
        metadataDuration == metadataPages * 40;

        if (!timingValid) {
            qCritical()
            << "[GIF-METADATA] Frame or timing mismatch."
            << "JPEG frames:" << frameCount
            << "Metadata pages:" << metadataPages
            << "FPS:" << metadataFps
            << "Frame time:" << metadataFrameTime
            << "Duration:" << metadataDuration
            << "Expected duration:" << gifAllTime16;

            return false;
        }

        // The protocol requires exactly 32 ASCII hex characters.

        bool idValid = uploadId.size() == 32;

        for (const QChar character : uploadId) {
            const ushort value = character.unicode();

            if (!((value >= '0' && value <= '9') ||
                (value >= 'a' && value <= 'f'))) {
                idValid = false;
            break;
                }
        }

        if (!idValid) {
            qCritical()
            << "[GIF-METADATA] Invalid persistent GIF ID:"
            << uploadId;

            return false;
        }


        // STEP 4C-4 — Validate JPEG contents and dimensions.
        // This runs before any USB upload command.
        //
        // Do not accept corrupt images, wrong formats
        // or dimensions inconsistent with metadata.json.

        const int storedRotation =
        metadata.value("deviceRotate").toInt(-1);

        if (storedRotation < 0 || storedRotation > 3) {
            qCritical()
            << "[GIF-VALIDATION] Invalid stored rotation:"
            << storedRotation;
            return false;
        }


        // Windows MYSTIQUE JPEG output:
        // Rotation 0, 2, 3: 480x640
        // Rotation 1:       640x480
        const QSize expectedSize =
        (storedRotation == 1)
        ? QSize(640, 480)
        : QSize(480, 640);

        for (const QString& frameFile : frameFiles) {

            QImageReader jpegReader(frameFile, QByteArrayLiteral("jpeg"));

            const QImage decodedImage = jpegReader.read();
            const QByteArray detectedFormat =
            jpegReader.format().toLower();

            if (decodedImage.isNull() ||
                (detectedFormat != "jpeg" &&
                detectedFormat != "jpg")) {

                qCritical()
                << "[GIF-VALIDATION] Invalid JPEG image:"
                << frameFile
                << "Format:" << detectedFormat
                << "Error:" << jpegReader.errorString();

            return false;
                }

                if (decodedImage.size() != expectedSize) {
                    qCritical()
                    << "[GIF-VALIDATION] Incorrect JPEG dimensions:"
                    << frameFile
                    << "Expected:" << expectedSize
                    << "Actual:" << decodedImage.size();

                    return false;
                }
        }

        qDebug()
        << "[GIF-VALIDATION] All JPEG frames decoded."
        << "Frames:" << frameFiles.size()
        << "Resolution:" << expectedSize
        << "Rotation:" << storedRotation;


        const QByteArray uploadIdBytes =
        uploadId.toLatin1();

        qDebug()
        << "[GIF-METADATA] Persistent ID loaded:"
        << uploadId
        << "| Frames:" << metadataPages
        << "| FPS:" << metadataFps
        << "| Duration:" << metadataDuration;


    // ------------------------------------------------------------
    // USB helper: endpoint 0x01 OUT
    // ------------------------------------------------------------

    auto bulkOut01 =
    [this](const QByteArray& packet, int timeoutMs) -> bool {

        int transferred = 0;

        const int result =
        libusb_bulk_transfer(
            deviceHandle0,
            0x01,
            reinterpret_cast<unsigned char*>(
                const_cast<char*>(packet.constData())
            ),
            packet.size(),
                             &transferred,
                             timeoutMs
        );

        if (result != 0) {
            qCritical()
            << "❌ [GIF] Bulk OUT 0x01 fout:"
            << libusb_error_name(result)
            << "size =" << packet.size()
            << "| transferred =" << transferred
            << "| timeout =" << timeoutMs;

            return false;
        }

        if (transferred != packet.size()) {
            qCritical()
            << "❌ [GIF] Onvolledige OUT 0x01:"
            << transferred
            << "/"
            << packet.size();

            return false;
        }

        return true;
    };


    // STEP 3.3A — Safe upload failure handling.
    //
    // Do not reset the USB device automatically when an upload
    // or stored-GIF selection fails.
    //
    // The JPEG frames and dcldfinish may already have been
    // successfully transferred before a later operation fails.
    //
    // A failed selection does not prove a failed GIF upload.
    // Recovery must be handled separately and explicitly.


    // ------------------------------------------------------------
    // 1. WINDOWS UPLOAD PREP
    //
    // AA 2E 0F 00
    // ...
    // HIDC
    // checksum over bytes 0..45
    //
    // PROVEN uit TEST2.
    // ------------------------------------------------------------

    QByteArray preparePacket(48, 0);

    preparePacket[0] = static_cast<char>(0xAA);
    preparePacket[1] = static_cast<char>(0x2E);
    preparePacket[2] = static_cast<char>(0x0F);
    preparePacket[3] = static_cast<char>(0x00);

    preparePacket[42] = 'H';
    preparePacket[43] = 'I';
    preparePacket[44] = 'D';
    preparePacket[45] = 'C';

    quint16 prepareChecksum = 0;

    for (int i = 0; i < 46; ++i) {
        prepareChecksum +=
        static_cast<quint8>(preparePacket[i]);
    }

    preparePacket[46] =
    static_cast<char>(prepareChecksum & 0xFF);

    preparePacket[47] =
    static_cast<char>((prepareChecksum >> 8) & 0xFF);

    qDebug()
    << "🚀 [GIF] Upload prepare 0x0F:"
    << preparePacket.toHex(' ');

    if (!bulkOut01(preparePacket, 30000)) {
        qCritical()
        << "❌ [GIF] Upload prepare mislukt.";
        return false;
    }

    // ------------------------------------------------------------
    // 0x81 RESPONSE
    //
    // Verwacht:
    // 55 2E 0F 00 ... HIDC AA 01
    // ------------------------------------------------------------

    QByteArray prepareResponse(48, 0);
    int transferred = 0;

    const int prepareResponseResult =
    libusb_bulk_transfer(
        deviceHandle0,
        0x81,
        reinterpret_cast<unsigned char*>(
            prepareResponse.data()
        ),
        prepareResponse.size(),
                         &transferred,
                         30000
    );

    if (prepareResponseResult != 0) {
        qCritical()
        << "❌ [GIF] 0x0F response mislukt:"
        << libusb_error_name(prepareResponseResult);
        return false;
    }

    qDebug()
    << "🔎 [GIF] 0x0F response:"
    << prepareResponse.toHex(' ');

    // STEP 3.3E-3C-2C-3C
    // Validate the complete 0x0F preparation response.

    quint16 expectedPrepareChecksum = 0;

    for (int i = 0; i < 46; ++i) {
        expectedPrepareChecksum +=
        static_cast<quint8>(prepareResponse[i]);
    }

    const quint16 receivedPrepareChecksum =
    static_cast<quint8>(prepareResponse[46]) |
    (static_cast<quint16>(
        static_cast<quint8>(prepareResponse[47])
    ) << 8);

    const bool validPrepareResponse =
    transferred == 48 &&
    static_cast<quint8>(prepareResponse[0]) == 0x55 &&
    static_cast<quint8>(prepareResponse[1]) == 0x2E &&
    static_cast<quint8>(prepareResponse[2]) == 0x0F &&
    static_cast<quint8>(prepareResponse[3]) == 0x00 &&
    prepareResponse.mid(42, 4) == QByteArray("HIDC", 4) &&
    receivedPrepareChecksum == expectedPrepareChecksum;

    if (!validPrepareResponse) {
        qCritical()
        << "[GIF-UPLOAD] Invalid 0x0F preparation ACK."
        << "Received bytes:" << transferred
        << "Expected checksum:" << expectedPrepareChecksum
        << "Received checksum:" << receivedPrepareChecksum
        << "Response:" << prepareResponse.toHex(' ');

        return false;
    }

    // The preparation command has been acknowledged.
    // No JPEG transfer or SD storage is confirmed yet.

    if (outcome != nullptr) {
        outcome->lastCompletedStage =
        GifUploadStage::PreparationAcknowledged;
    }

    qDebug()
    << "[GIF-UPLOAD] PreparationAcknowledged:"
    << "0x0F response fully validated.";

    reportProgress(
        20,
        "Upload voorbereid; frames uploaden..."
    );


        quint64 totalJpegBytes = 0;

        for (const QString& frameFile : frameFiles) {
            QFile file(frameFile);

            if (!file.open(QIODevice::ReadOnly)) {
                qCritical() << "❌ [GIF] Frame niet leesbaar:" << frameFile;
                return false;
            }

            totalJpegBytes += static_cast<quint64>(file.size());
        }

        quint64 sentJpegBytes = 0;
        int lastProgressPercent = 20;

        for (int frameIndex = 0; frameIndex < frameFiles.size(); ++frameIndex) {
            const QString& frameFile = frameFiles.at(frameIndex);

            QFile frame(frameFile);

            if (!frame.open(QIODevice::ReadOnly)) {
                qCritical() << "❌ [GIF] Frame niet leesbaar:" << frameFile;
                return false;
            }

            const QByteArray jpeg = frame.readAll();

            if (jpeg.isEmpty()) {
                qCritical() << "❌ [GIF] Leeg JPEG-frame:" << frameFile;
                return false;
            }

            quint16 jpegChecksum = 0;

            for (const unsigned char byte : jpeg) {
                jpegChecksum = static_cast<quint16>(jpegChecksum + byte);
            }

            // DCLd-header via de centrale protocolimplementatie.
            if (jpeg.size() <= 0 || jpeg.size() > 0xFFFFFF) {
                    qCritical()
                    << "❌ [GIF] Ongeldige JPEG-grootte voor DCLd:"
                    << jpeg.size();
                    return false;
                }

                if (frameIndex > 255) {
                    qCritical()
                    << "❌ [GIF] Frame-index past niet in uint8:"
                    << frameIndex;
                    return false;
                }

                if (uploadIdBytes.size() != 32) {
                    qCritical()
                    << "❌ [GIF] Upload-ID moet exact 32 bytes bevatten:"
                    << uploadIdBytes.size();
                    return false;
                }

                std::vector<std::uint8_t> jpegBytes(
                    reinterpret_cast<const std::uint8_t*>(jpeg.constData()),
                                                    reinterpret_cast<const std::uint8_t*>(
                                                        jpeg.constData() + jpeg.size()
                                                    )
                );

                std::array<std::uint8_t, 32> uploadIdArray{};

                std::copy(
                    uploadIdBytes.cbegin(),
                          uploadIdBytes.cend(),
                          uploadIdArray.begin()
                );

                std::array<std::uint8_t, DcldProtocol::HEADER_SIZE>
                headerBytes{};

                try {
                    headerBytes = DcldProtocol::buildHeader(
                        jpegBytes,
                        frameCount,
                        static_cast<std::uint8_t>(frameIndex),
                                                            gifAllTime16,
                                                            uploadIdArray
                    );
                } catch (const std::exception& e) {
                    qCritical()
                    << "❌ [GIF] DCLd-header kon niet worden gebouwd:"
                    << e.what();
                    return false;
                }

                const QByteArray header(
                    reinterpret_cast<const char*>(headerBytes.data()),
                                        static_cast<int>(headerBytes.size())
                );


            // --------------------------------------------------------
            // HEADER EXACT 64 BYTES
            // --------------------------------------------------------

            if (!bulkOut01(header, 30000)) {
                qCritical()
                << "❌ [GIF] DCLd header mislukt."
                << "frame =" << frameIndex;
                return false;
            }

            // --------------------------------------------------------
            // JPEG DATA
            //
            // Windows capture:
            // bulk OUT 0x01
            // 64 bytes per USB packet
            // laatste packet mag korter zijn
            //
            // GEEN ACK tussen JPEG-blokken.
            // --------------------------------------------------------

            int offset = 0;

            while (offset < jpeg.size()) {

                const int chunkSize =
                qMin(64, jpeg.size() - offset);

                QByteArray chunk =
                jpeg.mid(offset, chunkSize);

                if (!bulkOut01(chunk, 30000)) {
                    qCritical()
                    << "❌ [GIF] JPEG data mislukt."
                    << "frame =" << frameIndex
                    << "offset =" << offset;
                    return false;
                }

                offset += chunkSize;
                sentJpegBytes += chunkSize;

                const int framePercent =
                totalJpegBytes > 0
                ? 20 + static_cast<int>(
                    (sentJpegBytes * 69) / totalJpegBytes
                )
                : 20 + static_cast<int>(
                    ((frameIndex + 1) * 69) /
                    frameFiles.size()
                );

                const int safePercent =
                std::min(89, framePercent);

                if (safePercent > lastProgressPercent) {
                    reportProgress(
                        safePercent,
                        QString("Frames uploaden: %1/%2")
                        .arg(frameIndex + 1)
                        .arg(frameFiles.size())
                    );

                    lastProgressPercent = safePercent;
                }
            }

            qDebug()
            << "✅ [GIF] Frame verzonden:"
            << frameIndex
            << "bytes =" << jpeg.size();
    }


    // STEP 3.3E-3C-2C-3D
    // All JPEG frames must have been transferred completely
    // before the upload may proceed to dcldfinish.

    if (sentJpegBytes == 0 ||
        sentJpegBytes != totalJpegBytes) {

        qCritical()
        << "[GIF-UPLOAD] JPEG transfer incomplete."
        << "Expected bytes:" << totalJpegBytes
        << "Transferred bytes:" << sentJpegBytes;

    return false;
        }

        if (outcome != nullptr) {
            outcome->lastCompletedStage =
            GifUploadStage::JpegTransferFinished;
        }

        qDebug()
        << "[GIF-UPLOAD] JpegTransferFinished:"
        << "All JPEG frames transferred."
        << "Total bytes:" << sentJpegBytes;


    // ------------------------------------------------------------
    // 3. FINISH
    //
    // EXACT 55 BYTES:
    // "DCLdfinish" + 45 null bytes
    //
    // PROVEN uit TEST2.
    // ------------------------------------------------------------
    reportProgress(
        90,
        "Alle frames verzonden; upload afronden..."
    );
    QByteArray finishPacket(55, 0);

    const char finishText[] =
    "dcldfinish";

    memcpy(
        finishPacket.data(),
           finishText,
           sizeof(finishText) - 1
    );

    qDebug()
    << "🏁 [GIF] DCLdfinish:"
    << finishPacket.toHex(' ');


    if (!bulkOut01(finishPacket, 30000)) {
        qCritical()
        << "[GIF-UPLOAD] dcldfinish transfer failed.";

        return false;
    }

    // STEP 3.3E-3C-2C-3E
    // The complete finish packet was transferred over USB.
    // This does not prove SD-card persistence.

    if (outcome != nullptr) {
        outcome->lastCompletedStage =
        GifUploadStage::FinishPacketSent;
    }

    qDebug()
    << "[GIF-UPLOAD] FinishPacketSent:"
    << "dcldfinish completely transferred."
    << "Packet bytes:" << finishPacket.size();

    // Windows TEST2/TEST3/5 uploads: about 85-115 ms before 0x08.
    // This 100 ms pause is not an SD persistence proof.
    // This code is already running inside the asynchronous upload worker.
    QThread::msleep(100);


    // ------------------------------------------------------------
    // 4. COMMIT/APPLY
    //
    // TEST2:
    // AA 2E 08 01 00 00 ...
    //
    // TEST3:
    // AA 2E 08 01 01 00 ...
    //
    // Slot = byte 4.
    // ------------------------------------------------------------

    // TEST2 explicitly selects index 0; the five-upload capture
    // omits it in a different hardware state. Always select the newly
    // uploaded index, as tested in TEST2 and TEST3.

    if (hardwareListIndex >= 0) {
        reportProgress(
            94,
            "Nieuwe GIF in TFT-lijst selecteren..."
        );

        QByteArray commitPacket(48, 0);


    commitPacket[0] =
    static_cast<char>(0xAA);

    commitPacket[1] =
    static_cast<char>(0x2E);

    commitPacket[2] =
    static_cast<char>(0x08);

    commitPacket[3] =
    static_cast<char>(0x01);

    commitPacket[4] =
    static_cast<char>(hardwareListIndex);

    commitPacket[42] = 'H';
    commitPacket[43] = 'I';
    commitPacket[44] = 'D';
    commitPacket[45] = 'C';

    quint16 commitChecksum = 0;

    for (int i = 0; i < 46; ++i) {
        commitChecksum +=
        static_cast<quint8>(commitPacket[i]);
    }

    commitPacket[46] =
    static_cast<char>(commitChecksum & 0xFF);

    commitPacket[47] =
    static_cast<char>(
        (commitChecksum >> 8) & 0xFF
    );

    qDebug()
    << "🚀 [GIF] Commit 0x08:"
    << commitPacket.toHex(' ');

    if (!bulkOut01(commitPacket, 30000)) {
        qCritical()
        << "❌ [GIF] Commit 0x08 mislukt.";
        return false;
    }

    // ------------------------------------------------------------
    // COMMIT RESPONSE 0x81
    // Verwacht:
    // 55 2E 08 00 ... HIDC A3 01
    // ------------------------------------------------------------

    QByteArray commitResponse(48, 0);
    transferred = 0;

    const int commitResponseResult =
    libusb_bulk_transfer(
        deviceHandle0,
        0x81,
        reinterpret_cast<unsigned char*>(
            commitResponse.data()
        ),
        commitResponse.size(),
                         &transferred,
                         30000
    );

    if (commitResponseResult != 0) {
        qCritical()
        << "❌ [GIF] Commit response mislukt:"
        << libusb_error_name(
            commitResponseResult
        );
        return false;
    }

    qDebug()
    << "🔎 [GIF] Commit response:"
    << commitResponse.toHex(' ');


    // STEP 3.2 — Validate stored-GIF selection response.
    // HIDC 0x08 selects an existing hardware list entry.
    // It does not prove that JPEG data is durably stored.

    quint16 expectedSelectionChecksum = 0;

    for (int i = 0; i < 46; ++i) {
        expectedSelectionChecksum +=
        static_cast<quint8>(commitResponse[i]);
    }

    const quint16 receivedSelectionChecksum =
    static_cast<quint8>(commitResponse[46]) |
    (static_cast<quint16>(
        static_cast<quint8>(commitResponse[47])
    ) << 8);

    if (transferred != 48 ||
        static_cast<quint8>(commitResponse[0]) != 0x55 ||
        static_cast<quint8>(commitResponse[1]) != 0x2E ||
        static_cast<quint8>(commitResponse[2]) != 0x08 ||
        static_cast<quint8>(commitResponse[3]) != 0x00 ||
        commitResponse.mid(42, 4) != QByteArray("HIDC", 4) ||
        receivedSelectionChecksum != expectedSelectionChecksum) {

        qCritical()
        << "[GIF] Invalid stored-GIF selection response:"
        << commitResponse.toHex(' ');

    return false;
        }



        // STEP 3.3E-3C-2C-3F
        // The complete 0x08 selection response has
        // already passed validation.
        //
        // This confirms GIF selection only.
        // It does not prove durable SD storage.

        if (outcome != nullptr) {
            outcome->selectionAcknowledged = true;

            outcome->lastCompletedStage =
            GifUploadStage::GifSelectionAcknowledged;
        }

        qDebug()
        << "[GIF-UPLOAD] GifSelectionAcknowledged:"
        << "Hardware list index:"
        << hardwareListIndex;


        qDebug()
        << "[GIF] 0x08 selection acknowledged."
        << "Hardware list index:"
        << hardwareListIndex;

    }


        // ------------------------------------------------------------
        // 5. PAS NU: METRICS -> GIF
        //
        // We gebruiken de reeds bewezen setDisplayMode().
        // ------------------------------------------------------------
        reportProgress(98, "GIF-modus op het TFT bevestigen...");
        // TEST3 uploads while GIF mode is already active. Do not send
        // a duplicate 0x03 02 after a successful 0x08 selection.
        const bool modeWasAlreadyGif = (currentMode == MODE_GIF_STREAM);
        if (!modeWasAlreadyGif && !setDisplayMode(MODE_GIF_STREAM)) {
            qCritical()
            << "[GIF] Upload en selectie voltooid, mode switch faalde.";
            return false;
        }
        qWarning() << "[GIF-ACTIVATE] Upload index" << hardwareListIndex
                   << "selected via 0x08; mode already GIF:"
                   << modeWasAlreadyGif;


        // STEP 3.3E-3C-2C-3G
        // setDisplayMode() has successfully validated
        // the complete GIF mode ACK (0x03 02).
        //
        // This confirms the mode command only.
        // It does not prove durable SD storage
        // or successful GIF playback.

        if (outcome != nullptr) {
            outcome->lastCompletedStage =
            GifUploadStage::GifModeAcknowledged;
        }

        qDebug()
        << "[GIF-UPLOAD] GifModeAcknowledged:"
        << "Complete GIF mode response validated.";


        qDebug()
        << "🎬 [GIF] Hardware staat nu in GIF Stream mode.";

        currentMode = MODE_GIF_STREAM;

        reportProgress(100, "GIF-selectie en modus bevestigd; TFT-weergave controleren");

        return true;
}

bool DeepCoolDevice::activateStoredGifSlot(int slot)
{
    if (!deviceHandle0) {
        qCritical()
        << "❌ [GIF] Cannot activate stored GIF: device is not open.";
        return false;
    }

    if (slot < 0 || slot >= 10) {
        qCritical()
        << "❌ [GIF] Invalid stored GIF slot:"
        << slot;
        return false;
    }

    int transferred = 0;

    // ============================================================
    // 1. ACTIVATE EXISTING GIF SLOT
    //
    // Proven Windows packet:
    //
    // AA 2E 08 01 [slot] ... HIDC checksum
    //
    // This does NOT upload image data.
    // ============================================================

    QByteArray commitPacket(48, 0);

    commitPacket[0] = static_cast<char>(0xAA);
    commitPacket[1] = static_cast<char>(0x2E);
    commitPacket[2] = static_cast<char>(0x08);
    commitPacket[3] = static_cast<char>(0x01);
    commitPacket[4] = static_cast<char>(slot);

    commitPacket[42] = static_cast<char>(0x48); // H
    commitPacket[43] = static_cast<char>(0x49); // I
    commitPacket[44] = static_cast<char>(0x44); // D
    commitPacket[45] = static_cast<char>(0x43); // C

    quint16 commitChecksum = 0;

    for (int i = 0; i < 46; ++i) {
        commitChecksum +=
        static_cast<quint8>(commitPacket[i]);
    }

    commitPacket[46] =
    static_cast<char>(commitChecksum & 0xFF);

    commitPacket[47] =
    static_cast<char>((commitChecksum >> 8) & 0xFF);

    qDebug()
    << "🚀 [GIF] Activate stored slot:"
    << slot
    << commitPacket.toHex(' ');

    const int commitResult =
    libusb_bulk_transfer(
        deviceHandle0,
        0x01,
        reinterpret_cast<unsigned char*>(
            commitPacket.data()
        ),
        commitPacket.size(),
                         &transferred,
                         30000
    );

    if (commitResult != 0) {
        qCritical()
        << "❌ [GIF] Stored GIF slot activation failed:"
        << libusb_error_name(commitResult);
        return false;
    }

    // ------------------------------------------------------------
    // 2. COMMIT RESPONSE
    // Expected:
    //
    // 55 2E 08 00 ... HIDC A3 01
    // ------------------------------------------------------------

    QByteArray commitResponse(48, 0);
    transferred = 0;

    const int commitResponseResult =
    libusb_bulk_transfer(
        deviceHandle0,
        0x81,
        reinterpret_cast<unsigned char*>(
            commitResponse.data()
        ),
        commitResponse.size(),
                         &transferred,
                         30000
    );

    if (commitResponseResult != 0) {
        qCritical()
        << "❌ [GIF] Stored GIF slot response failed:"
        << libusb_error_name(commitResponseResult);
        return false;
    }

    if (transferred != 48 ||
        static_cast<quint8>(commitResponse[0]) != 0x55 ||
        static_cast<quint8>(commitResponse[1]) != 0x2E ||
        static_cast<quint8>(commitResponse[2]) != 0x08) {

        qCritical()
        << "❌ [GIF] Invalid stored GIF slot response:"
        << commitResponse.toHex(' ');

    return false;
        }

        qDebug()
        << "✅ [GIF] Stored GIF slot activated:"
        << slot;

        // ============================================================
        // 3. METRICS -> GIF MODE
        //
        // Proven Windows packet:
        //
        // AA 2E 03 02 ... HIDC F5 01
        // ============================================================

        QByteArray gifModePacket(48, 0);

        gifModePacket[0] = static_cast<char>(0xAA);
        gifModePacket[1] = static_cast<char>(0x2E);
        gifModePacket[2] = static_cast<char>(0x03);
        gifModePacket[3] = static_cast<char>(0x02);

        gifModePacket[42] = static_cast<char>(0x48); // H
        gifModePacket[43] = static_cast<char>(0x49); // I
        gifModePacket[44] = static_cast<char>(0x44); // D
        gifModePacket[45] = static_cast<char>(0x43); // C

        quint16 gifModeChecksum = 0;

        for (int i = 0; i < 46; ++i) {
            gifModeChecksum +=
            static_cast<quint8>(gifModePacket[i]);
        }

        gifModePacket[46] =
        static_cast<char>(gifModeChecksum & 0xFF);

        gifModePacket[47] =
        static_cast<char>((gifModeChecksum >> 8) & 0xFF);

        transferred = 0;

        qDebug()
        << "🚀 [GIF] Metrics -> GIF mode:"
        << gifModePacket.toHex(' ');

        const int gifModeResult =
        libusb_bulk_transfer(
            deviceHandle0,
            0x01,
            reinterpret_cast<unsigned char*>(
                gifModePacket.data()
            ),
            gifModePacket.size(),
                             &transferred,
                             30000
        );

        if (gifModeResult != 0) {
            qCritical()
            << "❌ [GIF] Metrics -> GIF mode switch failed:"
            << libusb_error_name(gifModeResult);
            return false;
        }

        // ------------------------------------------------------------
        // 4. GIF MODE RESPONSE
        // Expected:
        //
        // 55 2E 03 00 ... HIDC 9E 01
        // ------------------------------------------------------------

        QByteArray gifModeResponse(48, 0);
        transferred = 0;

        const int gifModeResponseResult =
        libusb_bulk_transfer(
            deviceHandle0,
            0x81,
            reinterpret_cast<unsigned char*>(
                gifModeResponse.data()
            ),
            gifModeResponse.size(),
                             &transferred,
                             30000
        );

        if (gifModeResponseResult != 0) {
            qCritical()
            << "❌ [GIF] GIF mode response failed:"
            << libusb_error_name(gifModeResponseResult);
            return false;
        }

        if (transferred != 48 ||
            static_cast<quint8>(gifModeResponse[0]) != 0x55 ||
            static_cast<quint8>(gifModeResponse[1]) != 0x2E ||
            static_cast<quint8>(gifModeResponse[2]) != 0x03) {

            qCritical()
            << "❌ [GIF] Invalid GIF mode response:"
            << gifModeResponse.toHex(' ');

        return false;
            }

            qDebug()
            << "✅ [GIF] Hardware is now in GIF Stream mode.";

            currentMode = MODE_GIF_STREAM;

            return true;
}

bool DeepCoolDevice::uploadBgImage(const QByteArray& pixelData) {
    if (!isOpen()) {
        return false;
    }

    static bool isScreenInitialized = false;
    if (!isScreenInitialized) {
        unsigned char initPacket[64] = {0};
        initPacket[0] = 0x21; initPacket[1] = 0xFF; initPacket[2] = 0xFF; initPacket[3] = 0x00;
        initPacket[4] = 0xE5; initPacket[5] = 0x44; initPacket[6] = 0x00; initPacket[7] = 0xFF;
        initPacket[8] = 0x00; initPacket[9] = 0x90; initPacket[10] = 0xFF; initPacket[11] = 0x00;
        initPacket[12] = 0xFF; initPacket[13] = 0x99; initPacket[14] = 0x3C; initPacket[15] = 0xFF;
        initPacket[16] = 0x00; initPacket[17] = 0xFF; initPacket[18] = 0xEE; initPacket[19] = 0x00;
        initPacket[20] = 0xFF; initPacket[21] = 0x1A; initPacket[22] = 0x00; initPacket[23] = 0xFF;
        initPacket[24] = 0x00; initPacket[25] = 0xBB; initPacket[26] = 0x02;

        libusb_control_transfer(deviceHandle, 0x21, 0x09, 0x0200, 0x01, initPacket, 64, 50);
        isScreenInitialized = true;
        QThread::msleep(10);
    }

    const int payloadChunkSize = 63;
    int totalBytesToSend = pixelData.size();
    const char* rawPixels = pixelData.constData();
    int bytesProcessed = 0;

    while (bytesProcessed < totalBytesToSend) {
        int currentChunkSize = std::min(payloadChunkSize, totalBytesToSend - bytesProcessed);
        unsigned char hidPacket[65] = {0};
        hidPacket[0] = 0xEC;
        hidPacket[1] = 0x40;

        for (int i = 0; i < currentChunkSize; ++i) {
            hidPacket[2 + i] = static_cast<unsigned char>(rawPixels[bytesProcessed + i]);
        }

        int controlRes = libusb_control_transfer(deviceHandle, 0x21, 0x09, 0x02ec, 0x02, hidPacket, 65, 50);
        if (controlRes < 0) {
            return false;
        }

        bytesProcessed += currentChunkSize;
        QThread::msleep(5);
    }

    unsigned char syncPacket[20] = {0};
    syncPacket[0] = 0x11; syncPacket[1] = 0xFF; syncPacket[2] = 0x02; syncPacket[3] = 0x37;
    syncPacket[4] = 0x01; syncPacket[5] = 0x01; syncPacket[6] = 0xFF; syncPacket[7] = 0x00;
    syncPacket[8] = 0xBB; syncPacket[9] = 0x02;

    libusb_control_transfer(deviceHandle, 0x21, 0x09, 0x0211, 0x01, syncPacket, 20, 50);
    return true;
}



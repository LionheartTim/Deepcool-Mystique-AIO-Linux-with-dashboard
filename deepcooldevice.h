#ifndef DEEPCOOLDEVICE_H
#define DEEPCOOLDEVICE_H
#include <QString>
#include <QByteArray>
#include <libusb-1.0/libusb.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include "device.h"
#include <functional>

enum DisplayMode {
    MODE_CPU_INFO = 0,      // CPU temp main, CPU GHz bottom
    MODE_GPU_INFO = 1,      // CPU temp main, GPU temp bottom
    MODE_SYSTEM_OVERVIEW = 2,
    MODE_CUSTOM = 3,
    MODE_GPU_FOCUS = 4,     // 🚀 COMPILER FIX: De komma staat nu onbreekbaar op zijn plek!
    MODE_GIF_STREAM = 5
};


enum ScreenRotation {
    ROTATION_0   = 0,   // 0° (no rotation)
    ROTATION_90  = 1,   // 90° clockwise
    ROTATION_180 = 2,   // 180°
    ROTATION_270 = 3    // 270° clockwise
};


struct SystemData {
    float cpuTemp;
    float cpuUsage;
    float gpuTemp;
    float gpuUsage;
    float ramUsage;
    float vramUsage = -1.0f;  // -1 = niet beschikbaar
    bool useFahrenheit;
    quint16 gpuFrequency = 0;
};


enum class GifUploadStage {
    NotStarted,
    PreparationAcknowledged,
    JpegTransferFinished,
    FinishPacketSent,
    GifSelectionAcknowledged,
    GifModeAcknowledged
};

struct GifUploadOutcome {
    GifUploadStage lastCompletedStage =
    GifUploadStage::NotStarted;

    bool selectionRequired = false;
    bool selectionAcknowledged = false;
};


class DeepCoolDevice : public Device
{
public:
    DeepCoolDevice();
    ~DeepCoolDevice() override;

    // Implement Device interface
    bool open(const DeviceInfo &deviceInfo) override;
    void close() override;
    bool isOpen() const override {
        return (deviceType == DEVICE_TYPE_HID && fd >= 0) ||
               (deviceType == DEVICE_TYPE_USB_VENDOR && deviceHandle != nullptr);
    }

    bool sendData(const QByteArray &data) override;
    QByteArray receiveData(int length) override;
    QString getDeviceName() const override { return deviceName; }
    QString getDeviceInfo() const override;

    // High-level DeepCool-specific commands
    bool sendStatusRequest();  // Handshake/init
    bool initMachineInfoMode();  // Try to switch device to Machine Info mode
    bool setDisplayMode(DisplayMode mode);
    bool updateDisplay(const SystemData &data);
    bool setRotation(ScreenRotation rotation);
    // MYSTIQUE STEP 4B - COMBINED RGB BRIGHTNESS CONFIG
    // One safe HIDC 0x02 packet always includes rotation, RGB and brightness.
    bool setVisualSettings(int rgbMode, int brightness);
    ScreenRotation getRotation() const { return currentRotation; }

    // 🚀 STAP 2: Kondig de twee nieuwe hardware-engines aan binnen de klasse-structuur
    void stopGifHardwareStream();

    bool startGifHardwareStream(
        const QString& gifPath,
        int hardwareListIndex,
        const std::function<void(int, const QString&)>& progress = {},
                                GifUploadOutcome* outcome = nullptr
    );


    bool activateStoredGifSlot(int slot);
    bool selectGifHardwareSlot(int slotIndex);
    bool uploadBgImage(const QByteArray& pixelData);
    void hardResetUsbDevice();
    bool deleteGifHardwareSlot(int listIndex);

    // 🚨 TIM'S OPENBARE VOLGORDE-GRENDEL
    bool m_isStreamReady = false;

private:
    bool verifyDevice(); // 🚨 REPARATIE: ZET DEZE REGEL HIER TERUG!




private:

    DeviceType deviceType;

    // libusb context — required for USB initialization and cleanup.
    libusb_context *usbContext = nullptr;

    // Main USB handle — alias for interface 0.
    libusb_device_handle *deviceHandle = nullptr;

    // MYSTIQUE 360 ARGB — interface 0.
    libusb_device_handle *deviceHandle0 = nullptr;

    // Legacy handle — interface 1 is not used.
    libusb_device_handle *deviceHandle1 = nullptr;

    int interfaceNumber;
    int endpointOut;
    int endpointIn;


    // For HID devices
    int fd;  // File descriptor for HID device

    // Common
    QString devicePath;
    QString deviceName;
    DisplayMode currentMode;
    ScreenRotation currentRotation;
    int currentRgbMode = 0;
    int currentBrightness = 70;
    bool applyVisualConfig(ScreenRotation rotation, int rgbMode, int brightness);

    // Protocol helpers
    QByteArray buildPacket(quint8 command, const QByteArray &payload);
    bool validateResponse(const QByteArray &response);

    // Command bytes (reverse-engineered from USB capture)
    static const quint8 CMD_UPDATE_DISPLAY = 0x01;  // Send display data
    static const quint8 CMD_CONFIG = 0x02;           // Configuration (rotation, etc.)
    static const quint8 CMD_STATUS_REQUEST = 0x10;   // Status/handshake request

    static const int PACKET_SIZE = 48;  // MYSTIQUE uses 48-byte packets
};

#endif // DEEPCOOLDEVICE_H

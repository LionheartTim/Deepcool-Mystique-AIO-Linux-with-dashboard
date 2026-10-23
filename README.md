# DeepCool MYSTIQUE Control Hub

An independent **Linux / Qt 6** control application for the **DeepCool MYSTIQUE 360 ARGB** liquid cooler. It provides display and GIF management, live system metrics, RGB mode selection, diagnostics, and KDE Plasma tray integration.

This is a community-developed project by **LionheartTim**. It is not an official DeepCool application.

> **Hardware scope:** Development and physical testing have focused on the MYSTIQUE 360 ARGB, USB **VID:PID `3633:0009`**. Other models and USB IDs have not been verified.

## Features

- Re-designed te GUI.
- Manage up to **five GIF slots**, including drag-and-drop, preview, upload, selection, and deletion.
- Switch between the custom GIF display and system metrics; select screen rotation and display brightness.
- Display live CPU, GPU, memory, and related system measurements where Linux exposes suitable sensor data.
- Select **Motherboard RGB**, **Temperature RGB**, or **GIF Motion RGB** modes.
- View read-only hardware/USB/sensor diagnostics, recent events, and thermal alerts.
- Keep application settings and GIF metadata across restarts.
- Minimize to the KDE Plasma system tray, with a **Start with KDE Plasma** switch in **About**.
- Use a guarded, one-time first-launch media cleanup to prepare previously used cooler media storage.

### RGB behavior and limitations

- **Motherboard RGB:** The physical lighting is controlled by the motherboard's existing RGB settings. No OpenRGB, Armoury Crate, or other additional RGB-control application is required by this project. The application's animated **`(rgb simulation)`** preview is *not* a live readback of the motherboard's LED colors.
- **GIF Motion RGB:** Works with the cooler's physical pump lighting on the tested device.
- **Temperature RGB:** The GUI temperature-color preview works, but on the tested cooler the **physical pump lighting remains white**. This is a known unresolved firmware/hardware behavior, **not a claim of working physical temperature-based RGB**.

## Hardware and testing

| Hardware or function | Status |
| --- | --- |
| DeepCool MYSTIQUE 360 ARGB (`3633:0009`) | Tested on real hardware |
| AMD Ryzen 9 9950X3D metrics | Tested on real hardware |
| AMD Radeon RX 9070 XT metrics | Tested on real hardware |
| GIF upload, selection, delete, brightness, rotation | Tested on real hardware |
| GIF Motion RGB and motherboard-controlled physical lighting | Tested on real hardware |
| Intel CPU / Intel GPU readers | Implemented; **not** tested on physical Intel hardware |
| NVIDIA GPU reader | Implemented; **not** tested on physical NVIDIA hardware; Flatpak availability of `nvidia-smi` is not verified |
| Other DeepCool models and Linux distributions | Not verified |

The confirmed development environment is **Bazzite Linux with KDE Plasma**. Sensor availability and accuracy may differ across kernels, hardware, drivers, and Flatpak installations.

## Install the Flatpak bundle

The project's Flatpak application ID is:

`io.github.lionhearttim.deepcool-gui`

Download or build the `.flatpak` bundle, then choose **one** installation type:

```bash
# Install for the current user
flatpak install --user --bundle io.github.lionhearttim.deepcool-gui.flatpak

# OR install system-wide
flatpak install --system --bundle io.github.lionhearttim.deepcool-gui.flatpak
```

Start the application:

```bash
flatpak run io.github.lionhearttim.deepcool-gui
```

Do not install both variants unless you intentionally manage them separately. Normal operation requires access to the cooler's USB interface; Flatpak permissions alone do **not** replace the host's USB device access rules. The development machine uses a dedicated **`99-deepcool.rules`** udev rule. Check and install the rule appropriate for your distribution and device before expecting USB access. You may need to reconnect the device or log out and back in after changing udev permissions.

The build currently grants broad `home` filesystem and device access to preserve the existing local settings, GIF files, and USB behavior. Review the Flatpak manifest before installing on other machines.

## Important: first launch may delete media on the cooler

**Read this before running the app for the first time.**

On a fresh installation, **First Launch Setup** can request a **one-time, irreversible deletion** of existing **GIF and image files** stored on the cooler (`SD:GIF` and `SD:IMG`). This is intended to clear media left by the original Windows software so that the Linux GIF-slot manager can work reliably.

- **Files on the cooler are deleted**; locally stored GIF files on your PC are not the target of this operation.
- The app asks for explicit confirmation before issuing the deletion request.
- After sending the command, the app allows **120 seconds** for the device to process it. Do not disconnect or operate the cooler during this period.
- A USB acknowledgement does **not** independently prove that all SD-card deletions have finished.
- To avoid unintended repeat deletions, uncertain or previously attempted cleanup operations are **not automatically retried**.
- If you cancel the setup, older Windows GIFs/images may remain, and GIF management might not work properly.

This command was previously investigated and tested on the development cooler, including subsequent GIF upload/delete operation. It is **not** a general-purpose SD-card formatter.

## KDE Plasma autostart

After the app has been **opened once** and initial media setup is complete, it enables KDE autostart by default. At the **next login**, it starts minimized in the system tray.

To opt out, open **About** and uncheck **Start with KDE Plasma (minimized to system tray)**. Your choice is retained across subsequent starts.

> Installing a Flatpak package by itself cannot immediately launch the app or enable an interactive user-session autostart. The first manual launch is required.

## Build from source

The application is written in **C++17** with **Qt 6 Widgets** and **libusb-1.0**. GIF preparation also uses **ImageMagick**.

For a native build, install the required Qt 6 development tools, a C++ compiler, qmake, libusb development headers, and ImageMagick, then run from the project root:

```bash
qmake6 -o Makefile deepcool-gui-qt6.pro
make -j2
./deepcool-gui-qt6
```

For a Flatpak bundle, install `flatpak-builder`, configure the Flathub remote, and run the project's **V2 packaging script** from the source directory:

```bash
bash ./build_mystique_flatpak_current_v2.sh "$PWD"
```

The build script uses the **current local source tree** and produces `~/Desktop/io.github.lionhearttim.deepcool-gui.flatpak`. It bundles the dependencies needed for the tested Flatpak configuration and explicitly includes the extensionless `aio-preview` pump artwork. It requires the KDE Flatpak runtime/SDK and access to its dependency sources when building.

### AppStream metadata

`io.github.lionhearttim.deepcool-gui.metainfo.xml` contains application information for software centers. **The current V2 bundle-building script does not yet install this XML file**. Package maintainers should add it to `/app/share/metainfo/` in the Flatpak build before relying on it to appear in software centers. Publishing through Flathub may also require additional screenshots and release metadata.

## Safety and diagnostics

This application communicates directly with supported cooler hardware over USB. Do **not** try unknown USB commands, interrupt an active GIF upload or media cleanup, or run a second controller against the same interface. The Diagnostics tab provides read-only information; it is not intended to reset or format the device.

## License and upstream

**Source-code licensing has not yet been specified in this repository.** Before public distribution, add a `LICENSE` file with the license you select; do not assume that the metadata license in the AppStream XML grants rights to the C++ source code.

Project author: **LionheartTim** — [GitHub profile](https://github.com/LionheartTim).

DeepCool, MYSTIQUE, and other product names are trademarks of their respective owners. This independent application is not affiliated with or endorsed by DeepCool.


<img width="1124" height="866" alt="display" src="https://github.com/user-attachments/assets/42cd497e-5ed5-44d2-a822-d4cc3c877ac3" />

<img width="1124" height="868" alt="diag" src="https://github.com/user-attachments/assets/60956ee9-39b5-4adf-98ed-587b3871f965" />

<img width="1124" height="868" alt="system" src="https://github.com/user-attachments/assets/f158036d-46aa-4c4a-bb19-727b4174b8f0" />

<img width="1124" height="868" alt="about" src="https://github.com/user-attachments/assets/c626069f-92bc-4ab7-9158-d263f1859c3a" />




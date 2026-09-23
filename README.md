# Deepcool-Mystique-AIO-Linux-with-dashboard
This is a fork from https://github.com/mymymy1303/qt-deepcool

# DeepCool Mystique LCD Control 🐧

A lightweight, native Qt6 open-source graphical user interface and background driver daemon for the **DeepCool Mystique AIO Liquid Cooler** on Linux systems. Specifically optimized for atomic desktop environments like **Bazzite**, **SteamOS**, and Fedora Silverblue.

---

## ✨ Features

- **Dynamic Screen Rotation:** Full software preview support for **270 Degrees Counter-Clockwise** layout configurations, matching your physical hardware pump positioning exactly.
- **Hardware Layout Scaling:** High-resolution telemetry dashboard preview with upscaled cinematic binnenscherm (260x170 pixels) filling out the interface borders seamlessly.
- **Real-Time Telemetry Tracking:** Veloce vector graph drawing for live CPU/GPU temperatures, processor utilization spikes, and RAM usage monitoring.
- **Persistent Background Daemon:** Smart Qt6 Wayland asynchronized system tray minimize logic ensuring hardware updates keep pushing without cutting the D-Bus system tray panel connection.
- **Auto-Boot Injection:** Sandbox-integrated autostart script execution creating system configurations seamlessly at profile login.

---

## 🚀 Installation & Setup

### Prerequisites

Since this application interfaces directly with the raw USB bus of your DeepCool Mystique cooling unit, ensure your host has the proper `udev` hardware rule permissions established so the Flatpak sandbox can read the device.

Create a rule file at `/etc/udev/rules.d/99-deepcool.rules` containing:
```udev
SUBSYSTEM=="usb", ATTR{idVendor}=="30bb", ATTR{idProduct}=="0001", MODE="0666", GROUP="plugdev"
```
*Reload your udev configuration afterwards using `sudo udevadm control --reload-rules && sudo udevadm trigger`.*

### Terminal Installation (Bazzite / SteamOS / Fedora)

Open your desktop terminal and run the following commands to initialize the local repository deployment and activate the environment:

```bash
# Clone the repository workspace
cd ~/Desktop
git clone https://github.com
cd qt-deepcool-main

# Reinstall the package tracking references locally
flatpak --user uninstall io.github.lionhearttim.deepcool-gui -y
flatpak --user install ./repo io.github.lionhearttim.deepcool-gui -y
kbuildsycoca6 --noincremental
```

---

## 🛠️ Development & Compilation Workspace

If you want to compile modifications or work inside the absolute sandboxed flatpak container environment, enter your active Distrobox environment and run the native manifest deployment:

```bash
# Move to workspace inside the compiler container
cd /home/USER/Desktop/qt-deepcool-main

# Flush builders caches and run the architecture target compilation
rm -rf .flatpak-builder build-flatpak repo
flatpak-builder --user --install --force-clean --disable-rofiles-fuse build-flatpak io.github.lionhearttim.deepcool-gui.yml

# Seal the build and sync repository index markers
flatpak build-finish build-flatpak
flatpak-builder --repo=repo --export-only build-flatpak io.github.lionhearttim.deepcool-gui.yml
flatpak build-update-repo repo
exit
```

---

## 👤 Author & Credits

- Created and maintained with 💻 by **LionheartTim**
- Built using the **Qt6 framework** and native Linux USB communication interfaces.

---

## 📄 License

This hub implementation driver is open-source software provided under the MIT licensing structures. Contributions, pull requests, and telemetry diagnostic issues are completely welcome!


Dark mode:
<img width="1109" height="610" alt="image" src="https://github.com/user-attachments/assets/9ec964f5-2089-4b5d-b622-6762ba91032d" />
<img width="1107" height="607" alt="image" src="https://github.com/user-attachments/assets/494bbfea-a5c1-484b-8823-bf35bdc1220e" />
<img width="1120" height="612" alt="image" src="https://github.com/user-attachments/assets/8593e384-f2e1-4aeb-8033-e301d0357778" />

Light mode:
<img width="1119" height="612" alt="image" src="https://github.com/user-attachments/assets/ed0f84bb-742a-490e-9a33-31c3d8cff618" />
<img width="1115" height="612" alt="image" src="https://github.com/user-attachments/assets/9d18b551-37a2-4fed-871a-878eda63b16a" />
<img width="1116" height="607" alt="image" src="https://github.com/user-attachments/assets/47cd7951-8c4f-4b58-9c0c-6110bb3526be" />

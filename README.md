# DeepCool Mystique Control Hub 🐧 (v1.2.1)

A lightweight, native Qt6 open-source graphical user interface and background driver daemon for the **DeepCool Mystique AIO Liquid Cooler** on Linux systems. Specifically optimized for atomic and immutable desktop environments like **Bazzite**, **SteamOS**, and Fedora Silverblue.

---

## 🚀 What's New in v1.2.1

- **Fixed AIO Screen Freezing on Reboot:** Resolved a critical race condition where the LCD screen would remain static after a system restart. Built an automated USB hardware polling loop directly into the constructor to immediately wake up and claim the DeepCool pump unit upon login.
- **Removed Deprecated Display Modes:** Completely stripped out the redundant "Full Hardware Overview" option from the mode selection dropdown. The interface is now 100% tailor-made and streamlined specifically for the native firmware of the DeepCool Mystique.
- **Cleaned C++ Class Structure:** Fixed a major Meta-Object Compiler (MOC) scope conflict by rectifying duplicate layout and widget definitions. The application codebase is now fully optimized and stable.

---

## ✨ Features

- **All-in-One Dashboard (v1.2+):** A unified control center layout that completely eliminates legacy sidebars and tabs. The interface displays all telemetry rows and device configurations in a single screen footprint.
- **Gelaagd Telemetrie Matrix:** A clean, optimized monitoring grid organized into two distinct sections: Processor (CPU) Status on top, and Graphics & Memory (GPU/RAM) Status directly below.
- **Real-Time CPU Clock Graph:** A dedicated, live-rendered orange vector graph calculating active CPU core boost frequencies (GHz) dynamically, providing a fluid visual reference for system load.
- **100% Unified Dark Theme:** All application structures—including the telemetry graphs, the core clock meter, and the unified Hardware Configuration block—tint uniformly into a matching graphite dark mode stylesheet.
- **Indexed Screen Rotation:** Full software abstraction for the native `0, 1, 2, 3` AIO controller firmware indices. Choosing the *270 Degrees Counter-Clockwise* option (index 3) instantly shifts the GUI preview into a wide horizontal landscape format.
- **Interactive Community Footers:** Leverages embedded HTML hyperlinking within the main Qt layout, allowing users to interactively launch their native host browser directly into your personal GitHub workspace.

---

## 🚀 Installation & Setup

### Prerequisites

Since this application interfaces directly with the raw USB bus of your DeepCool Mystique cooling unit, ensure your host has the proper `udev` hardware rule permissions established so the Flatpak sandbox can read the device.

Create a rule file at `/etc/udev/rules.d/99-deepcool.rules` containing:
```udev
SUBSYSTEM=="usb", ATTR{idVendor}=="3633", ATTR{idProduct}=="0009", MODE="0666", GROUP="plugdev", TAG+="uaccess"
```
*Reload your udev configuration afterwards using `sudo udevadm control --reload-rules && sudo udevadm trigger`.*

### Terminal Installation from Standalone Bundle

To deploy the production-ready standalone v1.2.1 bundle generated directly on your desktop, execute the following commands on your host terminal (`Tim@Lionheart`):

```bash
# Flush any legacy cached deployment profiles
flatpak --user uninstall io.github.lionhearttim.deepcool-gui -y

# Install the standalone distribution release bundle
flatpak install --user ~/Desktop/DeepCool-Mystique-Control-v1.2.1.flatpak -y
flatpak --user repair

# Rebuild desktop icon caches and KDE Plasma 6 application paths
rm -rf ~/.cache/icon-cache.kcache
gtk-update-icon-cache -f -t ~/.local/share/icons/hicolor
kbuildsycoca6 --noincremental
```

---

## 🛠️ Development & Compilation Workspace

If you want to compile modifications or work inside the sandboxed flatpak container environment (`📦[Tim@deepcool-build]`), enter your active container and run the native manifest deployment:

```bash
# Move to workspace inside the compiler container
cd /home/Tim/Desktop/qt-deepcool

# Flush build caches and build parameters
rm -rf .flatpak-builder build-flatpak repo

# Run the architecture target compilation and manifest deployment
flatpak-builder --user --install --force-clean --disable-rofiles-fuse build-flatpak io.github.lionhearttim.deepcool-gui.yml

# Seal the build and sync repository index markers
flatpak build-finish build-flatpak
flatpak-builder --repo=repo --export-only build-flatpak io.github.lionhearttim.deepcool-gui.yml
flatpak build-update-repo repo
exit
```

### Launching with Live Diagnostic Overlays
To review underlying D-Bus session state triggers, active window management indicators, or status notifier engine flags, invoke the runtime layout using:
```bash
QT_LOGGING_RULES="qt.gui.iconengine.debug=true;kf.config.core.debug=true;kf.statusnotifieritem.debug=true" flatpak run io.github.lionhearttim.deepcool-gui
```

---

## 👤 Author & Credits

- Architected and maintained with 💻 by **[LionheartTim](https://github.com)**
- Built leveraging the **Qt6 framework**, raw native USB bus bridges, and structural Linux sysfs instrumentation.

---

## 📄 License

This hub implementation driver is open-source software provided under the MIT licensing structures. Contributions, pull requests, and tracking diagnostics are entirely welcome!

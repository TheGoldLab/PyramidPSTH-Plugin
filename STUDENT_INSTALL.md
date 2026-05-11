# Install guide (no command line)

This guide is for students who just want to install the `PyramidPSTH` plugin.

## Before you start

- You need **Open Ephys GUI** already installed.
- Download plugin files from this repo's **Releases** page:
  - `PyramidPSTH-mac.zip` for macOS
  - `PyramidPSTH-windows.zip` for Windows

---

## macOS install

1. Open the latest release in GitHub.
2. Download `PyramidPSTH-mac.zip`.
3. Double-click the zip to unzip it.
4. You should get `PyramidPSTH.bundle`.
5. Open Finder to your Open Ephys plugins folder.
6. Drag `PyramidPSTH.bundle` into that `PlugIns` folder.
7. Restart Open Ephys GUI.
8. In the processor list, search for `Pyramid PSTH` and add it.

If macOS blocks the plugin, right-click the plugin/app once and choose **Open**.

---

## Windows install

1. Open the latest release in GitHub.
2. Download `PyramidPSTH-windows.zip`.
3. Right-click the zip and choose **Extract All**.
4. You should get `PyramidPSTH.dll`.
5. Open File Explorer to your Open Ephys plugins folder.
6. Copy `PyramidPSTH.dll` into that `PlugIns` folder.
7. Restart Open Ephys GUI.
8. In the processor list, search for `Pyramid PSTH` and add it.

---

## Quick troubleshooting

- If plugin does not appear, confirm file is in the correct `PlugIns` folder.
- Restart Open Ephys after copying files.
- Make sure OS matches the download:
  - macOS → `.bundle`
  - Windows → `.dll`
- If still not visible, ask lab support to verify Open Ephys version compatibility.

---

## For lab managers

Use tagged releases (for example `v1.0.0`) so students always download a known-good version.

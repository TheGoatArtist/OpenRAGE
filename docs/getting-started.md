# Getting started

OpenRAGE is developed on both **Windows** and **Linux** (CachyOS / Arch). Everything here
works on both.

## 1. Install the tools

| Tool | What it's for | Windows | CachyOS / Arch |
| --- | --- | --- | --- |
| Git | Version control | [git-scm.com](https://git-scm.com/) or `winget install --id Git.Git -e` | `sudo pacman -S git` |
| Python 3 | Project scripts in `tools/` | [python.org](https://www.python.org/downloads/) or `winget install -e --id Python.Python.3.12` | `sudo pacman -S python` (usually preinstalled) |
| ImHex | Hex editor for studying file formats | [imhex.werwolv.net](https://imhex.werwolv.net/) | Package manager (`imhex`) or the official site |
| Ghidra | Studying the game's executables (later) | [ghidra-sre.org](https://ghidra-sre.org/) | Package manager (`ghidra`) or the official site |

On Windows, tick **"Add python.exe to PATH"** in the Python installer.

## 2. Get the code

```bash
git clone https://github.com/TheGoatArtist/OpenRAGE.git
cd OpenRAGE
```

Line endings are handled by [.gitattributes](../.gitattributes), so no Git settings are needed.

## 3. Find your RAGE install

RAGE has no native Linux version. On Linux it runs through Steam's Proton, with the same file
layout as on Windows.

| Platform | Usual location |
| --- | --- |
| Windows | `C:\Program Files (x86)\Steam\steamapps\common\RAGE` or `<drive>:\SteamLibrary\steamapps\common\RAGE` |
| Linux (Steam package) | `~/.local/share/Steam/steamapps/common/RAGE` |
| Linux (Steam Flatpak) | `~/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/RAGE` |

If it isn't there, open Steam → right-click RAGE → **Manage → Browse local files**.

## 4. Run the inventory

```bash
python tools/inventory.py "/path/to/RAGE"
```

On Linux, use `python3` if `python` isn't found. The output is Markdown. Compare it with
[research/install-inventory.md](research/install-inventory.md) and note any differences.
Proton and native Windows installs should match exactly.

To see what's inside one of the game's archives (names, types and sizes only, nothing extracted):

```bash
python tools/resources.py "/path/to/RAGE/base/gameresources_sp_01.patch"
```

Add `--check` to confirm every entry unpacks to its recorded size. The results should match the
table in [research/formats/resources-archive.md](research/formats/resources-archive.md#check-results).

## 5. Save and share changes

```bash
git pull
git add .
git commit -m "Describe what changed"
git push
```

Run `git pull` before you start working so you have your collaborator's latest changes.

## Rules to remember

- Never commit game files or decompiled code. See [CONTRIBUTING.md](../CONTRIBUTING.md).
- When you study a format in ImHex, save your pattern file (`.hexpat`) in the project so the
  other person can load it.

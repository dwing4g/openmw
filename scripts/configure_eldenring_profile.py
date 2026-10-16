#!/usr/bin/env python3
"""Prepare a separate, playable OpenMW Elden combat profile for a local package.

Reads the Morrowind installation without copying or modifying its game files.
Uses OpenMW's standard save directory with separate settings and bindings.
Existing profile configuration and saves are preserved on subsequent runs.
On Windows, existing saves in an old Documents/OneDrive folder are reused when
the current Documents save folder is empty and only one alternative has saves.
"""

import argparse
import os
from pathlib import Path
import re
import shutil


def game_path(path: Path, target: str) -> str:
    value = str(path.resolve())
    if target == "windows" and os.name != "nt":
        match = re.fullmatch(r"/mnt/([a-zA-Z])/(.*)", value)
        if not match:
            raise ValueError("Windows game data must be on a /mnt/<drive>/ mount when configuring from Linux")
        value = match[1].upper() + ":/" + match[2]
    return value.replace("\\", "/")


def quoted_path(value: str) -> str:
    # OpenMW uses '&' to escape quoted config paths, including literal ampersands.
    return '"' + value.replace('&', '&&').replace('"', '&"') + '"'


def windows_documents() -> Path:
    """Use the same known Documents folder as the engine, including redirection."""
    import ctypes
    from ctypes import wintypes
    import uuid

    folder_id = (ctypes.c_byte * 16).from_buffer_copy(
        uuid.UUID("FDD39AD0-238F-46AF-ADB4-6C85480369C7").bytes_le)
    shell = ctypes.WinDLL("shell32")
    shell.SHGetKnownFolderPath.argtypes = (ctypes.c_void_p, wintypes.DWORD,
                                          wintypes.HANDLE, ctypes.POINTER(ctypes.c_void_p))
    shell.SHGetKnownFolderPath.restype = ctypes.c_long
    ole = ctypes.WinDLL("ole32")
    ole.CoTaskMemFree.argtypes = (ctypes.c_void_p,)
    ole.CoTaskMemFree.restype = None
    value = ctypes.c_void_p()
    result = shell.SHGetKnownFolderPath(ctypes.byref(folder_id), 0, None, ctypes.byref(value))
    try:
        if result < 0 or not value.value:
            raise OSError(f"Cannot resolve the Windows Documents folder (HRESULT {result:#x})")
        return Path(ctypes.wstring_at(value))
    finally:
        ole.CoTaskMemFree(value)


def save_data_locations(target: str) -> list[Path]:
    """Return current standard data first, followed by former Documents locations."""
    if target != "windows" or os.name != "nt":
        return []
    documents = [windows_documents()]
    user_profile = os.environ.get("USERPROFILE")
    if user_profile:
        documents.append(Path(user_profile) / "Documents")
        documents.append(Path(user_profile) / "OneDrive" / "Documents")
    for name in ("OneDrive", "OneDriveConsumer", "OneDriveCommercial"):
        if os.environ.get(name):
            documents.append(Path(os.environ[name]) / "Documents")
    # Known Documents and the environment may refer to the same OneDrive folder.
    return list(dict.fromkeys(path.resolve() / "My Games" / "OpenMW" for path in documents))


def count_saved_games(data: Path) -> int:
    """Count slots in character folders, matching OpenMW's save discovery layout."""
    saves = data / "saves"
    if not saves.exists():
        return 0
    return sum(1 for character in saves.iterdir() if character.is_dir()
               for slot in character.iterdir() if slot.suffix.lower() == ".omwsave" and slot.is_file())


def repair_save_data(config: Path, target: str, save_data: Path | None = None,
                     backup: bool = True) -> None:
    """Select existing saves without moving them or importing other user settings."""
    original = config.read_bytes()
    pattern = rb'(?m)^((?:\xef\xbb\xbf)?[ \t]*user-data[ \t]*=[ \t]*)([^\r\n]*)(\r?$)'
    matches = list(re.finditer(pattern, original))
    if len(matches) > 1:
        raise ValueError(f"Multiple user-data settings in {config}; keep only the intended one")
    if save_data is None:
        # Explicit custom paths and populated portable profiles are intentional.
        if matches and matches[0][2].strip() not in (b'"?userdata?"', b'?userdata?'):
            return
        try:
            locations = save_data_locations(target)
            if not locations:
                return
            if count_saved_games(locations[0]):
                return
            populated = [path for path in locations[1:] if count_saved_games(path)]
        except OSError as error:
            print(f"Cannot inspect existing saves; the save path was preserved: {error}")
            return
        if len(populated) != 1:
            if populated:
                print("Multiple previous save folders found; use --save-data to select one:")
                for path in populated:
                    print(f"  {path}")
            return
        save_data = populated[0]
    if not count_saved_games(save_data):
        raise ValueError(f"No OpenMW character saves in {save_data / 'saves'}. "
                         "Pass the OpenMW directory containing saves, not the saves directory itself.")
    setting = quoted_path(game_path(save_data, target)).encode("utf-8")
    if matches:
        match = matches[0]
        updated = original[:match.start(2)] + setting + original[match.end(2):]
    else:
        newline = b"\r\n" if b"\r\n" in original else b"\n"
        updated = original + (b"" if original.endswith(b"\n") else newline)
        updated += b"user-data=" + setting + newline
    if updated == original:
        return
    if backup:
        destination = config.with_name(config.name + ".before-save-repair")
        index = 0
        while True:
            try:
                with destination.open("xb") as stream:
                    stream.write(original)
                break
            except FileExistsError:
                index += 1
                destination = config.with_name(config.name + f".before-save-repair.{index}")
        print(f"Configuration backup: {destination}")
    config.write_bytes(updated)
    print(f"Using existing saved games: {save_data / 'saves'}")
    print("No saved games, settings or input bindings were moved or overwritten.")


def select_local_profile(package: Path) -> None:
    """Keep installed resource defaults and select the portable profile by default."""
    local_config = package / "openmw.cfg"
    if local_config.exists():
        lines = local_config.read_text(encoding="utf-8-sig").splitlines()
    else:
        lines = ["resources=./resources", "data=./resources/vfs-mw"]
    # Relative config-file paths are resolved from the file, not the launch directory.
    lines = [line for line in lines if not re.match(r"^\s*config\s*=", line)]
    lines.append('config="elden-profile"')
    local_config.write_text("\n".join(lines) + "\n", encoding="utf-8")


def upgrade_empty_portable_profile(config: Path) -> None:
    """Upgrade the old generated save path without hiding existing saved games."""
    original = config.read_bytes()
    # Only change the old generator's default. Custom paths and all other
    # launcher settings (including content order and INI fallbacks) survive.
    pattern = rb'(?m)^([ \t]*user-data[ \t]*=[ \t]*)"\./user-data"([ \t]*\r?$)'
    if not re.search(pattern, original):
        return
    saves = config.parent / "user-data" / "saves"
    if saves.exists() and any(saves.iterdir()):
        print(f"Existing portable saves retained: {saves}")
        print('To use standard saves, copy them to OpenMW\'s standard saves directory, '
              'then set user-data="?userdata?" in this profile\'s openmw.cfg.')
        return
    config.write_bytes(re.sub(pattern, rb'\1"?userdata?"\2', original))


def configure(package: Path, morrowind: Path, target: str, allow_unbuilt: bool,
              portable_saves: bool = False, save_data: Path | None = None) -> None:
    package = package.resolve()
    data = morrowind if morrowind.name.lower() == "data files" else morrowind / "Data Files"
    entries = {item.name.lower(): item.name for item in data.iterdir() if item.is_file()}
    for required in ("morrowind.esm", "morrowind.bsa"):
        if required not in entries:
            raise ValueError(f"Missing {required} in {data}")
    executable = package / ("openmw.exe" if target == "windows" else "openmw")
    if not allow_unbuilt and not executable.is_file():
        raise ValueError(f"The engine has not been built: {executable}")

    profile = package / "elden-profile"
    profile.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Separate Elden combat profile; paths are relative to this file.",
        "replace=content",
        "replace=fallback-archive",
        'data-local="./data"',
        'user-data="./user-data"' if portable_saves else 'user-data="?userdata?"',
        "data=" + quoted_path(game_path(data, target)),
    ]
    for name in ("morrowind", "tribunal", "bloodmoon"):
        if name + ".esm" in entries:
            lines.append("content=" + entries[name + ".esm"])
        if name + ".bsa" in entries:
            lines.append("fallback-archive=" + entries[name + ".bsa"])
    # The installation's INI can select different language-specific fonts or
    # weather; the native build helper imports these after engine installation.
    config = profile / "openmw.cfg"
    existing_config = config.exists()
    if not existing_config:
        config.write_text("\n".join(lines) + "\n", encoding="utf-8")
    elif not portable_saves:
        upgrade_empty_portable_profile(config)
    if not portable_saves or save_data is not None:
        repair_save_data(config, target, save_data, backup=existing_config)
    settings = profile / "settings.cfg"
    if not settings.exists():
        settings.write_text(
            "[Game]\nelden ring combat = true\n\n"
            "[Video]\nresolution x = 1280\nresolution y = 720\nwindow mode = 2\n",
            encoding="utf-8",
        )

    select_local_profile(package)

    if target == "windows":
        diagnostic_name = "eldenring-diagnose-audio-windows.ps1"
        shutil.copyfile(Path(__file__).resolve().parents[1] / "CI" / diagnostic_name,
                        package / diagnostic_name)
        (package / "Start-EldenCombat-Audio-Diagnostic.cmd").write_text(
            '@echo off\r\nsetlocal DisableDelayedExpansion\r\n'
            'pushd "%~dp0" || exit /b 1\r\n'
            'powershell.exe -NoProfile -ExecutionPolicy Bypass '
            '-File ".\\eldenring-diagnose-audio-windows.ps1" %*\r\n'
            'set "elden_result=%errorlevel%"\r\npause\r\npopd\r\nexit /b %elden_result%\r\n',
            encoding="utf-8", newline="",
        )
        (package / "Start-EldenCombat.cmd").write_text(
            '@echo off\r\nsetlocal DisableDelayedExpansion\r\n'
            'pushd "%~dp0" || exit /b 1\r\n'
            'if not exist "%~dp0openmw.exe" (\r\n'
            '  echo openmw.exe has not been built. Run CI\\eldenring-build-windows.ps1 first.\r\n'
            '  pause\r\n  popd\r\n  exit /b 1\r\n)\r\n'
            '".\\openmw.exe" --replace config --config "elden-profile" %*\r\n'
            'set "elden_result=%errorlevel%"\r\npopd\r\nexit /b %elden_result%\r\n',
            encoding="utf-8", newline="",
        )
        # The Qt launcher reads the package's local openmw.cfg; it does not
        # parse the engine's --config command-line option.
        (package / "Start-EldenCombat-Launcher.cmd").write_text(
            '@echo off\r\nsetlocal DisableDelayedExpansion\r\n'
            'pushd "%~dp0" || exit /b 1\r\n'
            'if not exist "%~dp0openmw-launcher.exe" (\r\n'
            '  echo openmw-launcher.exe is missing. Rebuild without -WithoutLauncher.\r\n'
            '  pause\r\n  popd\r\n  exit /b 1\r\n)\r\n'
            '".\\openmw-launcher.exe" %*\r\n'
            'set "elden_result=%errorlevel%"\r\npopd\r\nexit /b %elden_result%\r\n',
            encoding="utf-8", newline="",
        )
    else:
        launcher = package / "Start-EldenCombat.sh"
        launcher.write_text(
            '#!/usr/bin/env bash\nset -euo pipefail\n'
            'elden_package="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"\n'
            'cd -- "$elden_package"\n'
            'exec "$elden_package/openmw" --replace config --config "elden-profile" "$@"\n',
            encoding="utf-8",
        )
        launcher.chmod(0o755)
        settings_launcher = package / "Start-EldenCombat-Launcher.sh"
        settings_launcher.write_text(
            '#!/usr/bin/env bash\nset -euo pipefail\n'
            'elden_package="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"\n'
            'cd -- "$elden_package"\n'
            'exec "$elden_package/openmw-launcher" "$@"\n',
            encoding="utf-8",
        )
        settings_launcher.chmod(0o755)
    print(f"Profile: {profile}")
    print("Game files: " + game_path(data, target))
    save_settings = re.findall(r'^\s*user-data\s*=(.*)$', config.read_text(encoding="utf-8-sig"), re.MULTILINE)
    if save_settings:
        print("Save data setting (saves are in its saves subdirectory): " + save_settings[-1].strip())
    else:
        print("Save data setting: inherited from the package's openmw.cfg")
    if allow_unbuilt and not executable.is_file():
        print("Staged configuration only; this directory does not yet contain a built engine.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--morrowind", type=Path)
    parser.add_argument("--target", choices=("windows", "linux"), default="windows" if os.name == "nt" else "linux")
    parser.add_argument("--allow-unbuilt", action="store_true", help="stage configuration before the engine is available")
    parser.add_argument("--portable-saves", action="store_true",
                        help="use package-local saves for a new profile; preserve existing profile save paths")
    parser.add_argument("--save-data", type=Path,
                        help="select an existing OpenMW directory containing saves (explicitly overrides its save path)")
    parser.add_argument("--repair-saves-only", action="store_true",
                        help="repair an existing package's save location without rebuilding or changing game settings")
    args = parser.parse_args()
    if args.portable_saves and args.save_data is not None:
        parser.error("--portable-saves and --save-data cannot be combined")
    if args.portable_saves and args.repair_saves_only:
        parser.error("--portable-saves cannot be used with --repair-saves-only")
    if not args.repair_saves_only and args.morrowind is None:
        parser.error("--morrowind is required unless --repair-saves-only is specified")
    try:
        if args.repair_saves_only:
            config = args.package / "elden-profile" / "openmw.cfg"
            if args.save_data is None:
                upgrade_empty_portable_profile(config)
            repair_save_data(config, args.target, args.save_data)
            print(f"Profile: {config}")
            for line in config.read_text(encoding="utf-8-sig").splitlines():
                if re.match(r"^\s*user-data\s*=", line):
                    print("Save data setting: " + line.split("=", 1)[1].strip())
        else:
            configure(args.package, args.morrowind, args.target, args.allow_unbuilt,
                      args.portable_saves, args.save_data)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Profile configuration failed: {error}\n")


if __name__ == "__main__":
    main()

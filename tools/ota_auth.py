# PlatformIO pre-script for [env:ota]: take the espota host and password from
# include/secrets.h so they live in ONE gitignored place (never in platformio.ini).
import re
from pathlib import Path

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

secrets = Path(env.subst("$PROJECT_DIR")) / "include" / "secrets.h"  # noqa: F821
text = secrets.read_text(encoding="utf-8") if secrets.exists() else ""


def define(name):
    m = re.search(r'^\s*#define\s+%s\s+"?([^"\n]*)"?' % name, text, re.M)
    return m.group(1).strip() if m else None


if define("ENABLE_OTA") != "1":
    print("[ota] ENABLE_OTA is not 1 in include/secrets.h - the board will refuse OTA")
host = define("OTA_HOSTNAME")
address = define("OTA_ADDRESS")      # the board's IP: mDNS is off on the device (RAM, v3)
password = define("OTA_PASSWORD")
if not env.GetProjectOption("upload_port", "") and not env.subst("$UPLOAD_PORT"):  # noqa: F821
    # --upload-port <ip> on the command line wins; else OTA_ADDRESS; else <host>.local
    if address:
        env.Replace(UPLOAD_PORT=address)  # noqa: F821
    elif host:
        env.Replace(UPLOAD_PORT=host + ".local")  # noqa: F821
if password:
    env.Append(UPLOAD_FLAGS=["--auth=" + password])  # noqa: F821
else:
    print("[ota] no OTA_PASSWORD in include/secrets.h")

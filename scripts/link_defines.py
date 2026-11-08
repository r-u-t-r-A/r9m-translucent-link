# Pre-build script: reads link_defines.txt and adds its -D lines to the build.
# MY_BINDING_PHRASE is turned into MY_UID (first 6 bytes of the MD5 of the
# whole define line, the same derivation ExpressLRS uses).
Import("env")
import hashlib
import os
import re

# LINK_DEFINES=<file> selects another file (e.g. for CI)
path = os.environ.get("LINK_DEFINES") or os.path.join(env.subst("$PROJECT_DIR"), "link_defines.txt")
flags = []
have_uid = False
have_domain = False

with open(path, "r") as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if not line.startswith("-D"):
            raise SystemExit("link_defines.txt: unexpected line %r (only -D... allowed)" % line)
        if line.startswith("-DMY_BINDING_PHRASE="):
            uid = hashlib.md5(line.encode()).digest()[:6]
            line = "-DMY_UID=" + ",".join(str(b) for b in uid)
            have_uid = True
            print("Binding UID: " + line[len("-DMY_UID="):])
        if re.match(r"-DRegulatory_Domain_\w+$", line):
            have_domain = True
        flags.append(line)

if not have_uid:
    raise SystemExit("link_defines.txt: set -DMY_BINDING_PHRASE=\"...\" (same on both modules)")
if not have_domain:
    raise SystemExit("link_defines.txt: set one -DRegulatory_Domain_xxx")

env.Append(BUILD_FLAGS=flags)

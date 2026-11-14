# Pre-build script: adds the -D lines of link_defines.txt to the build, then
# those of link_defines.local.txt (git-ignored, for the binding phrase and
# other private settings) and link_defines_<env>.txt (e.g.
# link_defines_slave.txt) if present. Later files override settings of the
# same name; the per-env file only for that env.
#
# MY_BINDING_PHRASE is turned into MY_UID (first 6 bytes of the MD5 of the
# whole define line, the same derivation ExpressLRS uses).
#
# LINK_DEFINES=<file> selects another shared file (e.g. for CI); the local
# and per-env files are then looked up next to it.
Import("env")
import hashlib
import os
import re

# Settings that define the radio link itself. Both modules must agree on
# these, so they are only allowed in the shared file.
SHARED_ONLY = re.compile(
    r"^(MY_BINDING_PHRASE|Regulatory_Domain_\w+|LINK_PRESET|LINK_SF|LINK_BW_KHZ|LINK_CR|"
    r"LINK_PKT_LEN|LINK_PREAMBLE_LEN|LINK_TURNAROUND_US|LINK_GUARD_US)$")


def read_defines(path):
    """Return [(name, flag)] for the -D lines in path."""
    result = []
    name_of_file = os.path.basename(path)
    with open(path, "r") as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            m = re.match(r"-D(\w+)", line)
            if not m:
                raise SystemExit("%s:%d: unexpected line %r (only -D... allowed)" % (name_of_file, lineno, line))
            result.append((m.group(1), line))
    return result


shared_path = os.environ.get("LINK_DEFINES") or os.path.join(env.subst("$PROJECT_DIR"), "link_defines.txt")
defines_dir = os.path.dirname(os.path.abspath(shared_path))
local_path = os.path.join(defines_dir, "link_defines.local.txt")
env_path = os.path.join(defines_dir, "link_defines_%s.txt" % env["PIOENV"])

defines = {}  # name -> flag, insertion ordered
for name, flag in read_defines(shared_path):
    defines[name] = flag

if os.path.isfile(local_path):
    print("Using settings from " + os.path.basename(local_path))
    for name, flag in read_defines(local_path):
        if name.startswith("Regulatory_Domain_"):  # replaces the shared file's domain
            defines = {n: f for n, f in defines.items() if not n.startswith("Regulatory_Domain_")}
        defines[name] = flag

if os.path.isfile(env_path):
    print("Using overrides from " + os.path.basename(env_path))
    for name, flag in read_defines(env_path):
        if SHARED_ONLY.match(name):
            raise SystemExit("%s: %s must be the same on both modules, set it in link_defines.txt"
                             % (os.path.basename(env_path), name))
        defines[name] = flag

if "MY_BINDING_PHRASE" not in defines:
    raise SystemExit("set -DMY_BINDING_PHRASE=\"...\" in link_defines.local.txt or link_defines.txt (same on both modules)")
if not any(name.startswith("Regulatory_Domain_") for name in defines):
    raise SystemExit("set one -DRegulatory_Domain_xxx in link_defines.local.txt or link_defines.txt")

flags = []
for name, flag in defines.items():
    if name == "MY_BINDING_PHRASE":
        uid = hashlib.md5(flag.encode()).digest()[:6]
        flag = "-DMY_UID=" + ",".join(str(b) for b in uid)
        print("Binding UID: " + flag[len("-DMY_UID="):])
    flags.append(flag)

env.Append(BUILD_FLAGS=flags)

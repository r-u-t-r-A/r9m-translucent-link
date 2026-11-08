# Upload over ST-Link (st-flash): ExpressLRS R9M bootloader at 0x08000000, then
# the application at 0x08004000. Also writes firmware.elrs (a copy of the
# .bin) for flashing from EdgeTX/OpenTX onto a module that already runs the
# ELRS R9M bootloader.
Import("env")
import os
import shutil

BOOTLOADER = os.path.join(env.subst("$PROJECT_DIR"), "bootloader", "r9m_bootloader.bin")
FLASH_START = 0x08000000
APP_START = 0x08004000


def find_st_flash():
    pkg = os.path.join(env.subst("$PROJECT_PACKAGES_DIR"), "tool-stm32duino", "stlink")
    for name in ("st-flash", "st-flash.exe"):
        p = os.path.join(pkg, name)
        if os.path.isfile(p):
            return p
    return shutil.which("st-flash") or "st-flash"


def upload(source, target, env):
    st_flash = find_st_flash()
    firmware = str(source[0])
    ret = env.Execute('"%s" write "%s" 0x%08x' % (st_flash, BOOTLOADER, FLASH_START))
    if ret == 0:
        ret = env.Execute('"%s" --reset write "%s" 0x%08x' % (st_flash, firmware, APP_START))
    return ret


def gen_elrs(source, target, env):
    out = os.path.join(os.path.dirname(str(target[0])), "firmware.elrs")
    shutil.copyfile(str(target[0]), out)
    print("Wrote " + out)


env.Replace(UPLOADCMD=upload)
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", gen_elrs)

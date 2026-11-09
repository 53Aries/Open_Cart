# Open_Cart: PlatformIO build hook.
# Before each build: bump the minor number in version.txt (1.3 -> 1.4). The major number only changes when you edit
# version.txt yourself: write e.g. 2.0+ and the next build is exactly 2.0. CMakeLists.txt reads it into PROJECT_VER,
# which is what the web page shows as "Running version".
# After each build: copy firmware.bin to open_cart_v<version>.bin next to it, the name the Wi-Fi updater requires.
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

root = env.subst("$PROJECT_DIR")  # noqa: F821
vfile = os.path.join(root, "version.txt")

with open(vfile) as f:
    current = f.read().strip()
if current.endswith("+"):
    # "2.0+" means: use 2.0 as written for this build (to start a new major version), then continue 2.1, 2.2, ...
    version = current[:-1]
else:
    major, minor = current.split(".")[:2]
    version = "%s.%d" % (major, int(minor) + 1)
with open(vfile, "w", newline="\n") as f:
    f.write(version + "\n")
print("Open_Cart version -> %s" % version)


def name_image(source, target, env):  # noqa: ARG001
    bin_path = os.path.join(env.subst("$BUILD_DIR"), "firmware.bin")  # noqa: F821
    if os.path.exists(bin_path):
        dst = os.path.join(os.path.dirname(bin_path), "open_cart_v%s.bin" % version)
        shutil.copyfile(bin_path, dst)
        print("Wi-Fi update image: %s" % dst)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", name_image)  # noqa: F821

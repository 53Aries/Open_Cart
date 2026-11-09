# Open_Cart: PlatformIO build hook.
# Before each build: bump the minor number in version.txt (1.3 -> 1.4). CMakeLists.txt reads it into PROJECT_VER,
# which is what the web page shows as "Running version".
# After each build: copy firmware.bin to open_cart_v<version>.bin next to it, the name the Wi-Fi updater requires.
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

root = env.subst("$PROJECT_DIR")  # noqa: F821
vfile = os.path.join(root, "version.txt")

with open(vfile) as f:
    major, minor = f.read().strip().split(".")[:2]
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
        # Also keep the newest image in firmware/ (tracked by git); older ones are removed to keep the repo small.
        out = os.path.join(root, "firmware")
        os.makedirs(out, exist_ok=True)
        for old in os.listdir(out):
            if old.startswith("open_cart_v") and old.endswith(".bin"):
                os.remove(os.path.join(out, old))
        shutil.copyfile(bin_path, os.path.join(out, os.path.basename(dst)))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", name_image)  # noqa: F821

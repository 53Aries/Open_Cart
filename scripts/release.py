# Open_Cart: publish the last local build as a GitHub Release.
# Usage: pio run -e s3   then   python scripts/release.py [env]
# Commits version.txt, pushes, and creates release v<version> with open_cart_v<version>.bin attached.
import os
import shutil
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
env = sys.argv[1] if len(sys.argv) > 1 else "s3"


def find_gh():
    # A terminal opened before `winget install GitHub.cli` may not have gh on PATH yet.
    found = shutil.which("gh")
    if found:
        return found
    pkgs = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Microsoft", "WinGet", "Packages")
    for base in (pkgs, os.environ.get("ProgramFiles", "") + "\GitHub CLI"):
        for dirpath, _, files in os.walk(base):
            if "gh.exe" in files:
                return os.path.join(dirpath, "gh.exe")
    sys.exit("GitHub CLI not found. Install it with `winget install GitHub.cli`, then run `gh auth login`.")


def run(*args, capture=False):
    r = subprocess.run(args, cwd=root, text=True, capture_output=capture)
    if r.returncode != 0:
        sys.exit("failed: %s\n%s" % (" ".join(args), (r.stdout or "") + (r.stderr or "")))
    return r.stdout.strip() if capture else None


with open(os.path.join(root, "version.txt")) as f:
    version = f.read().strip()
image = os.path.join(root, ".pio", "build", env, "open_cart_v%s.bin" % version)
if not os.path.exists(image):
    sys.exit("%s not found. Run `pio run -e %s` first." % (image, env))

dirty = [l for l in run("git", "status", "--porcelain", capture=True).splitlines() if not l.endswith("version.txt")]
if dirty:
    sys.exit("Commit or stash these first so the release matches the code you built:\n" + "\n".join(dirty))

tag = "v" + version
if run("git", "tag", "--list", tag, capture=True):
    sys.exit("tag %s already exists; build again to get a new version." % tag)

run("git", "add", "version.txt")
if run("git", "status", "--porcelain", "version.txt", capture=True):
    run("git", "commit", "-m", "Release %s\n\nCo-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>" % tag)
run("git", "push", "origin", "HEAD")
run(find_gh(), "release", "create", tag, image, "--title", tag, "--generate-notes", "--target", run("git", "rev-parse", "HEAD", capture=True))
print("Released %s" % tag)

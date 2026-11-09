# Open_Cart: publish the last local build as a GitHub Release.
# Usage: pio run -e s3   then   python scripts/release.py [env]
# Commits version.txt, pushes, and creates release v<version> with open_cart_v<version>.bin attached.
import os
import subprocess
import sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
env = sys.argv[1] if len(sys.argv) > 1 else "s3"


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
run("gh", "release", "create", tag, image, "--title", tag, "--generate-notes", "--target", run("git", "rev-parse", "HEAD", capture=True))
print("Released %s" % tag)

#!/usr/bin/env python3
"""Posts a release announcement to Discord: first what changed, then the newly supported devices.

Runs in the release job of .github/workflows/build.yml after a version tag is published. It reads the
history between the previous tag and this one, so nothing has to be written by hand:
  * every merged pull request ("Merge PR #N" / "Merge pull request #N") and every direct commit
  * a change that adds a driver file, or whose title adds a device, counts as a new device
  * everything else that touches the app counts as an update; README / docs / CI-only changes are skipped

    usage: discord-release.py --tag v1.0.7 [--ref HEAD] [--prev v1.0.6] [--repo owner/name] [--dry-run]

The webhook comes from the DISCORD_WEBHOOK environment variable (a GitHub Actions secret, never stored in
the repository). Without it, or with --dry-run, the messages are printed instead of sent. GITHUB_TOKEN is
used to read pull request titles and authors; without it the PR's own commit subject is used.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request

SITE_SUPPORTED = "https://krisgfx.com/tools/#supported"
SITE_REQUEST = "https://krisgfx.com/tools/#request"
COLOR_UPDATES = 0x5865F2
COLOR_DEVICES = 0x2EA44F
MAX_DESCRIPTION = 3800  # Discord allows 4096 per embed; keep room for the footer line

# Files that are not part of the app: a change that only touches these is not announced.
NON_APP = re.compile(r"^(README\.md|CONTRIBUTING\.md|LICENSE|docs/|\.github/|scripts/|tests/|\.git)")
DEVICE_TITLE = re.compile(r"\b(add(s|ed)?|support(s|ed)?|read(s)?)\b", re.I)
NOT_DEVICE_TITLE = re.compile(r"\b(widget|menu|taskbar|tray|readme|translation|ui|option|setting|freeze|order)\b", re.I)


def git(*args):
    return subprocess.run(["git", *args], capture_output=True, text=True, encoding="utf-8", check=True).stdout.strip()


def previous_tag(ref):
    try:
        return git("describe", "--tags", "--abbrev=0", "--match", "v*", f"{ref}^")
    except subprocess.CalledProcessError:
        return None


def changed_files(commit, merge):
    base = f"{commit}^1" if merge else f"{commit}^"
    return [f for f in git("diff", "--name-only", base, commit).splitlines() if f]


def github_pr(repo, number):
    req = urllib.request.Request(f"https://api.github.com/repos/{repo}/pulls/{number}",
                                 headers={"Accept": "application/vnd.github+json", "User-Agent": "wireless-battery-release"})
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.load(r)
    except (urllib.error.URLError, TimeoutError, ValueError):
        return None


def clean_title(title, device=False):
    title = re.sub(r"^(feat|fix|chore|docs|refactor)(\([^)]*\))?!?:\s*", "", title.strip(), flags=re.I)
    if device:  # "Add Foo battery support" -> "Foo": the message title already says these are new devices
        title = re.sub(r"^adds?\s+(support\s+for\s+)?(the\s+)?", "", title, flags=re.I)
        title = re.sub(r"\s+(battery\s+)?support$", "", title, flags=re.I)
    return title[:1].upper() + title[1:]


def collect(repo, prev, ref):
    span = f"{prev}..{ref}" if prev else ref
    updates, devices = [], []
    for line in git("log", "--first-parent", "--reverse", "--format=%H%x09%P%x09%s", span).splitlines():
        sha, parents, subject = line.split("\t", 2)
        merge = len(parents.split()) > 1
        files = changed_files(sha, merge)
        if not files or all(NON_APP.match(f) for f in files):
            continue  # documentation, CI or repository housekeeping only
        added_driver = any(f.startswith("src/drivers/") and f.endswith(".cpp") for f in
                           git("diff", "--name-only", "--diff-filter=A", f"{sha}^1" if merge else f"{sha}^", sha).splitlines())
        m = re.match(r"Merge (?:PR|pull request) #(\d+)", subject)
        if merge and m:
            number = int(m.group(1))
            pr = github_pr(repo, number)
            title = pr["title"] if pr else git("log", "-1", "--format=%s", f"{sha}^2")
            author = pr["user"]["login"] if pr else None
            url = pr["html_url"] if pr else f"https://github.com/{repo}/pull/{number}"
            link = f" ([#{number}]({url}))" + (f" by {author}" if author else "")
        elif merge:
            continue  # a merge without a PR number (e.g. syncing a branch)
        else:
            title, link = subject, ""
        touches_drivers = any(f.startswith("src/drivers/") for f in files)
        is_fix = re.match(r"^(fix|fix\(|correct|repair)", clean_title(title), re.I)
        is_device = not is_fix and (added_driver or (touches_drivers and DEVICE_TITLE.search(title) and not NOT_DEVICE_TITLE.search(title)))
        (devices if is_device else updates).append(clean_title(title, is_device) + link)
    return updates, devices


def chunks(items):
    out, cur = [], ""
    for it in items:
        line = f"• {it}\n"
        if cur and len(cur) + len(line) > MAX_DESCRIPTION:
            out.append(cur)
            cur = ""
        cur += line
    if cur:
        out.append(cur)
    return out


def messages(tag, repo, updates, devices):
    release = f"https://github.com/{repo}/releases/tag/{tag}"
    msgs = []
    for i, body in enumerate(chunks(updates)):
        last = i == len(chunks(updates)) - 1
        embed = {"title": f"BatteryHub {tag}: what's new" + (" (continued)" if i else ""), "url": release,
                 "description": body + (f"\n[Download {tag}]({release})" if last else ""), "color": COLOR_UPDATES}
        msgs.append({"username": "BatteryHub", "embeds": [embed]})
    for i, body in enumerate(chunks(devices)):
        last = i == len(chunks(devices)) - 1
        footer = f"\n[All supported devices]({SITE_SUPPORTED}) · [Request a device]({SITE_REQUEST})" if last else ""
        embed = {"title": f"New supported devices in {tag}" + (" (continued)" if i else ""), "url": SITE_SUPPORTED,
                 "description": body + footer, "color": COLOR_DEVICES}
        msgs.append({"username": "BatteryHub", "embeds": [embed]})
    return msgs


def send(webhook, payload):
    data = json.dumps(payload).encode("utf-8")
    for _ in range(5):
        req = urllib.request.Request(webhook, data=data, method="POST",
                                     headers={"Content-Type": "application/json", "User-Agent": "wireless-battery-release"})
        try:
            with urllib.request.urlopen(req, timeout=20):
                return True
        except urllib.error.HTTPError as e:
            if e.code == 429:  # rate limited: Discord says how long to wait
                try:
                    wait = float(json.load(e).get("retry_after", 1))
                except ValueError:
                    wait = 2
                time.sleep(min(wait, 30))
                continue
            print(f"Discord answered {e.code}: {e.read()[:200]!r}", file=sys.stderr)
            return False
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--ref")
    ap.add_argument("--prev")
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", "krisgfx9/Wireless-Device-Battery-Monitor"))
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    ref = a.ref or a.tag
    prev = a.prev or previous_tag(ref)
    updates, devices = collect(a.repo, prev, ref)
    msgs = messages(a.tag, a.repo, updates, devices)
    print(f"{a.tag}: {len(updates)} update(s), {len(devices)} new device change(s) since {prev or 'the beginning'}")
    webhook = os.environ.get("DISCORD_WEBHOOK", "").strip()
    if a.dry_run or not webhook:
        if not webhook and not a.dry_run:
            print("DISCORD_WEBHOOK is not set; printing instead of sending.")
        for m in msgs:
            e = m["embeds"][0]
            print(f"\n=== {e['title']}\n{e['description']}")
        return 0
    ok = True
    for m in msgs:  # updates first, then devices: the order Discord shows them in
        ok = send(webhook, m) and ok
        time.sleep(1)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

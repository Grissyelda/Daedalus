#!/usr/bin/env python3
"""GrizzTech4 repository hygiene check.

GrizzTech4 is a public repository. This check stops personal information, local
machine details, secrets and design documents from being committed.

It only looks at what a change ADDS, so third-party code that already lives in
the tree (which carries its authors' own notices) is never flagged.

Usage:
  check_hygiene.py --staged              check the staged changes (pre-commit hook)
  check_hygiene.py --commits A..B        check every commit in a range (CI)
  check_hygiene.py --commit SHA          check one commit

Personal terms (your real name, other emails, machine names) go in a file named
.hygiene-denylist at the repository root, one per line. That file is ignored by
git, so the terms themselves never reach the repository.

A line can opt out of the content checks by containing the marker
"hygiene: allow". Use it sparingly and only for false positives.
"""

import argparse
import fnmatch
import os
import re
import subprocess
import sys

ALLOW_MARKER = "hygiene: allow"

# Addresses that are fine to commit.
ALLOWED_EMAIL_PATTERNS = [
    r"^[0-9]+\+[A-Za-z0-9-]+@users\.noreply\.github\.com$",
    r"^[A-Za-z0-9-]+@users\.noreply\.github\.com$",
    r"^noreply@github\.com$",
    r"^noreply@anthropic\.com$",
    r"@example\.(com|org|net)$",
]

EMAIL_RE = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")

CONTENT_RULES = [
    ("local Windows user path", re.compile(r"[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s\"']+", re.I)),
    ("local Windows work path", re.compile(r"[A-Za-z]:[\\/]+Claude[\\/]", re.I)),
    ("local macOS user path", re.compile(r"/Users/[A-Za-z0-9._-]+/")),
    ("local Linux home path", re.compile(r"/home/(?!runner/|claude/)[A-Za-z0-9._-]+/")),
    ("GitHub token", re.compile(r"\bgh[pousr]_[A-Za-z0-9]{30,}\b")),
    ("GitHub fine-grained token", re.compile(r"\bgithub_pat_[A-Za-z0-9_]{40,}\b")),
    ("AWS access key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("Anthropic API key", re.compile(r"\bsk-ant-[A-Za-z0-9_-]{20,}")),
    ("OpenAI-style API key", re.compile(r"\bsk-[A-Za-z0-9]{32,}\b")),
    ("Slack token", re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}")),
    ("private key", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("hard-coded credential", re.compile(
        r"(?i)\b(api[_-]?key|secret|password|passwd|auth[_-]?token)\b\s*[:=]\s*[\"'][^\"'\s]{8,}[\"']")),
]

# Files that must never be added, wherever they are.
BLOCKED_PATH_GLOBS = [
    "*.hygiene-denylist",
    ".env", "*.env", ".env.*",
    "*.pem", "*.key", "*.p12", "*.pfx",
    "id_rsa*", "id_ed25519*",
    "*design*doc*", "*Design*Doc*", "*DESIGN*DOC*",
    "*GDD*", "*gdd*",
    "*ROADMAP*", "*roadmap*",
    "*.docx", "*.doc", "*.xlsx", "*.pptx", "*.odt",
    "CLAUDE.local.md",
]

# Commits that bring in upstream RBDOOM-3-BFG code carry this trailer. Their
# diffs contain third-party authors' addresses, so only secrets and denylisted
# terms are checked for them.
UPSTREAM_TRAILER = "Hygiene-Upstream-Sync: yes"
UPSTREAM_SKIP_RULES = {"email address", "local Windows user path", "local macOS user path", "local Linux home path"}

SELF_PATH = "scripts/hygiene/check_hygiene.py"


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True,
                          text=True, encoding="utf-8", errors="replace").stdout


def repo_root():
    return git("rev-parse", "--show-toplevel").strip()


def load_denylist(root):
    path = os.path.join(root, ".hygiene-denylist")
    if not os.path.exists(path):
        return []
    terms = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                terms.append(line)
    return terms


def email_allowed(addr):
    return any(re.search(p, addr, re.I) for p in ALLOWED_EMAIL_PATTERNS)


def path_blocked(path):
    name = os.path.basename(path)
    return any(fnmatch.fnmatch(name, g) or fnmatch.fnmatch(path, g) for g in BLOCKED_PATH_GLOBS)


def parse_added_lines(diff_text):
    """Yield (path, line_number, text) for every added line in a unified diff."""
    path = None
    line_no = 0
    for raw in diff_text.splitlines():
        if raw.startswith("+++ "):
            target = raw[4:]
            path = None if target == "/dev/null" else target[2:] if target.startswith("b/") else target
            continue
        if raw.startswith("@@"):
            m = re.search(r"\+(\d+)", raw)
            line_no = int(m.group(1)) if m else 0
            continue
        if path is None:
            continue
        if raw.startswith("+") and not raw.startswith("+++"):
            yield path, line_no, raw[1:]
            line_no += 1
        elif not raw.startswith("-"):
            line_no += 1


def check_diff(diff_text, added_paths, denylist, upstream=False):
    problems = []
    for p in added_paths:
        if path_blocked(p):
            problems.append(f"{p}: this kind of file must not be committed (personal, secret or design document)")
    lowered = [(t, t.lower()) for t in denylist]
    for path, line_no, text in parse_added_lines(diff_text):
        if path == SELF_PATH or ALLOW_MARKER in text:
            continue
        where = f"{path}:{line_no}"
        if not upstream:
            for addr in EMAIL_RE.findall(text):
                if not email_allowed(addr):
                    problems.append(f"{where}: email address '{addr}'")
        for label, rx in CONTENT_RULES:
            if upstream and label in UPSTREAM_SKIP_RULES:
                continue
            if rx.search(text):
                problems.append(f"{where}: {label}")
        low = text.lower()
        for term, term_low in lowered:
            if term_low in low:
                problems.append(f"{where}: a term from your .hygiene-denylist")
    return problems


def check_identity(name_email_pairs):
    problems = []
    for who, addr in name_email_pairs:
        if addr and not email_allowed(addr):
            problems.append(f"{who} email '{addr}' is not a GitHub noreply address")
    return problems


def check_staged(root, denylist):
    diff = git("diff", "--cached", "-M", "-U0", "--no-color", "--no-ext-diff")
    added = [p for p in git("diff", "--cached", "-M", "--name-only", "--diff-filter=AR").splitlines() if p]
    problems = check_diff(diff, added, denylist)
    try:
        addr = git("config", "user.email").strip()
    except subprocess.CalledProcessError:
        addr = ""
    problems += check_identity([("commit author", addr)])
    if not addr:
        problems.append("git user.email is not set; use your GitHub noreply address")
    return problems


def check_commit(sha, denylist):
    message = git("log", "-1", "--format=%B", sha)
    upstream = UPSTREAM_TRAILER in message
    parents = git("log", "-1", "--format=%P", sha).split()
    if parents:
        diff = git("diff", "-M", "-U0", "--no-color", f"{parents[0]}", sha)
        added = [p for p in git("diff", "-M", "--name-only", "--diff-filter=AR", parents[0], sha).splitlines() if p]
    else:
        diff = git("show", "-U0", "--no-color", "--format=", sha)
        added = [p for p in git("show", "--name-only", "--diff-filter=AR", "--format=", sha).splitlines() if p]
    problems = check_diff(diff, added, denylist, upstream=upstream)
    author = git("log", "-1", "--format=%ae", sha).strip()
    committer = git("log", "-1", "--format=%ce", sha).strip()
    problems += check_identity([("author", author), ("committer", committer)])
    if not upstream:
        for addr in EMAIL_RE.findall(message):
            if not email_allowed(addr):
                problems.append(f"commit message: email address '{addr}'")
    return [f"{sha[:10]}: {p}" for p in problems]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--staged", action="store_true")
    g.add_argument("--commits", metavar="A..B")
    g.add_argument("--commit", metavar="SHA")
    args = ap.parse_args()

    root = repo_root()
    os.chdir(root)
    denylist = load_denylist(root)

    if args.staged:
        problems = check_staged(root, denylist)
    elif args.commit:
        problems = check_commit(args.commit, denylist)
    else:
        shas = [s for s in git("rev-list", "--reverse", args.commits).splitlines() if s]
        problems = []
        for sha in shas:
            problems += check_commit(sha, denylist)

    if problems:
        print("Hygiene check failed. GrizzTech4 is public, so these must not be committed:\n")
        for p in problems:
            print("  - " + p)
        print("\nRemove them, or if one is a genuine false positive, add 'hygiene: allow' to that line.")
        return 1
    print("Hygiene check passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

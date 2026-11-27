# Contributing to Daedalus

Daedalus is a public repository, so keep personal information out of it.

## One-time setup per clone

1. **Commit with your GitHub noreply address**, never a personal email:

   ```
   git config user.name "<your GitHub username>"
   git config user.email "<id>+<username>@users.noreply.github.com"
   ```

   You can find your noreply address under GitHub → Settings → Emails.

2. **Enable the hygiene hook:**

   ```
   git config core.hooksPath .githooks
   ```

3. **Optional but recommended:** create a `.hygiene-denylist` file in the repository root, listing one per line any personal terms that should never be committed: your real name, personal email addresses, machine names, local folder names. It is ignored by git, so the list itself stays on your machine.

## What the hygiene check blocks

The pre-commit hook and the Hygiene workflow on GitHub check only what each change adds. They block:

- email addresses other than GitHub and Anthropic noreply addresses (third-party `LICENSE`, `COPYING`, `NOTICE` and `AUTHORS` files are exempt, since they must keep their authors' details)
- local paths such as Windows user folders and home directories
- API keys, tokens, private keys and hard-coded passwords
- terms from your `.hygiene-denylist`
- design documents, roadmaps, office documents, `.env` files and key files

If a line is a genuine false positive, add `hygiene: allow` to it.

## Pull requests

- Keep each pull request to one change, with a short description of what it does and how it was tested.
- Upstream RBDOOM-3-BFG syncs follow [docs/UPSTREAM.md](docs/UPSTREAM.md).

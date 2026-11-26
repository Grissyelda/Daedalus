# Upstream

GrizzTech4 is based on RBDOOM-3-BFG. Its history was not imported. The fork point is recorded here so upstream changes can be brought in deliberately.

| | |
| --- | --- |
| Upstream repository | https://github.com/RobertBeckebans/RBDOOM-3-BFG |
| Imported commit | `ea29c006e84fedcc0e7c173c383a087ce0a8c0d5` (2026-08-07) |
| Submodule `neo/extern/nvrhi` | `dafbd407f6fb8b91078da72ca1712dbbd6ac2496` |
| Submodule `neo/extern/ShaderMake` | `13867771f6142f35690a5e2103c1e1efdd90cb0e` |

## Syncing with upstream

1. Fetch upstream into a scratch clone and create a patch from the recorded commit to the new one: `git diff ea29c006e8..<new> > upstream.patch`.
2. Apply it on a branch with `git apply --3way upstream.patch` and resolve conflicts.
3. Update the table above, and commit with this trailer, so the hygiene check knows the diff carries third-party authors' notices:

   ```
   Hygiene-Upstream-Sync: yes
   ```
4. Open a pull request.

Bug fixes that apply to RBDOOM-3-BFG itself should also be offered upstream.

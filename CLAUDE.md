# Blackwood Hollow — agent rules

- **Never use Live Coding.** After any C++ change, ask Robert to close the editor and run `BH_BuildAndLaunch.bat` (project root), which builds and relaunches the editor.
- **Commit as you go:** commit modified Blueprints, levels (`.umap`, `__ExternalActors__`) and other `.uasset` files along with C++, one commit per step, on the current phase branch. Robert merges PRs.
- Everything must be replicated (co-op). Docs live in the claude.ai Project (`claude/*.md`, `master-roadmap.md`).

# eink-learner-aid

A fork of the [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) e-reader firmware for the BOOX Picco that adds:

- **RSVP reading**: words shown one at a time at a set pace, on any EPUB.
- **Retrieval-practice checkpoints**: prepared books stop at section breaks and ask the reader to recall what they read.

It also carries a workflow for preparing books with checkpoint data and a simulator that runs in GitHub Codespaces.

The pinned CrossPoint base is recorded in [docs/BASE.md](docs/BASE.md); upstream's own README is kept at [docs/upstream/README.md](docs/upstream/README.md).

| Document | What it is |
| --- | --- |
| [docs/PRD.md](docs/PRD.md) | What v1 must do |
| [docs/PLAN.md](docs/PLAN.md) | The technical plan |
| [docs/STATUS.md](docs/STATUS.md) | Current milestone, open work, blockers |
| [docs/IMPLEMENTER_GUIDE.md](docs/IMPLEMENTER_GUIDE.md) | Standing rules for anyone working on an issue |
| [docs/SIMULATOR.md](docs/SIMULATOR.md) | Try the firmware in a browser tab: a Codespace and `./scripts/sim.sh` |

**Do not run CrossPoint's stock upload target on a Picco.** Flashing is done only with this project's own scripts, described in the plan's section 10.

Personal project. Not affiliated with Onyx or the CrossPoint maintainers.

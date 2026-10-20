# Native ports

Omakade bundles its own curated catalog in `resources/native-ports.json`. Installed ports are
detected from local desktop launchers and executable AppImages in `~/Applications`, `~/AppImages`,
`~/.local/bin` and `~/Games`, including one folder below each location. Detection uses the catalog's
executable and installation names, so an unlisted port or a custom name may need a catalog addition.

An owned emulator dump can show matching native projects in its details. Matches require the same
console plus a known dump serial or normalized title. Installed ports are linked as another way to
play that game and omitted from the suggestions. A repository link opens the upstream project;
Omakade does not download ports, install them or supply game data. Linux support describes an
upstream claim, not a guarantee of compatibility, completeness or current stability.

## Curating records

Each record has a stable `id`, display `name`, original `game`, console `system`, individual GitHub
`repo`, `linux` support flag, known dump `serials`, matching dump `titles` and installed `files`
tokens. Empty serial arrays permit title matching on the same console. Keep filename tokens specific
enough to avoid unrelated software. Shared launchers need deliberate handling; a second game must
not claim the same installed executable automatically.

Additions and changes to detection identities need human curation against the individual project and
its supported game dumps. The maintenance script never adds records or changes `id`, `game`,
`system`, `serials`, `titles` or `files`. Review a port's own build instructions before changing its
Linux flag manually. Wine or Proton support for a Windows game alone does not mean native Linux
support.

## Monthly maintenance

The **Native port catalog** workflow runs at 09:23 UTC on the first of each month and supports **Run
workflow**. It validates the catalog and runs deterministic offline tests, then queries only the
existing records' individual GitHub repositories. It follows GitHub repository renames and reads an
optional root `.recomp.json` through the GitHub contents API. It does not read or copy an external
directory's table.

The script can refresh only:

- `repo` from GitHub's canonical repository URL.
- `name` from a nonempty manifest `project` string.
- `linux` from an explicit, nonempty manifest `targets` string array containing or excluding `Linux`.

Missing manifests and omitted or invalid optional fields preserve curated values. Authentication
errors, missing repositories, rate limits, malformed manifests and other failed requests fail the
refresh before it writes the catalog. Requests have a 30 second timeout; manifests are limited to
128 KiB and duplicate repositories are fetched once. The workflow also has a 10 minute refresh
timeout.

A changed catalog produces a review PR on the reserved `automation/native-port-catalog` branch.
Later runs replace that branch and update the same open PR; do not use it for manual work. Unchanged
metadata causes no commit or PR. There is no automatic merge. The PR body lists each changed field
and its individual repository source. No timestamps, release counters or status snapshots are
written to the catalog.

The repository must allow GitHub Actions to create pull requests. The default `GITHUB_TOKEN` needs
contents and pull request write permissions, declared in the workflow. PRs opened with that token do
not trigger GitHub's `pull_request` workflows. To run the normal PR checks automatically, configure
an optional `CATALOG_PR_TOKEN` repository secret with contents and pull requests read/write access
to this repository and read access to public upstream repositories. Use a suitable fine-grained
token or GitHub App installation token. Required checks and human review still apply before merging.

Run the same maintenance locally from the repository root:

```sh
python3 tests/NativePortsCatalogTests.py
python3 scripts/update-native-ports.py --check
python3 scripts/update-native-ports.py --report /tmp/native-ports-review.md
git diff -- resources/native-ports.json
```

`--check` is offline and does not write files. The refresh uses the authenticated GitHub CLI and
writes changes only after every repository has been checked. Use `--gh /path/to/gh` or `GH_BIN` when
an account wrapper is required. Review the diff and report before committing. The catalog is bundled
at build time; end users do not make background requests for updates.

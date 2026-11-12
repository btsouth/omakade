# Releasing Omakade

Pushing a `vX.Y.Z` tag publishes a release, so it needs the maintainer's
authorization for that release and green automated validation of the exact
candidate commit. Before asking for that authorization, record the candidate
commit, the checks run, known limitations and any manual tests from the next
section.

## Decide what manual testing the release needs

The release workflow builds, tests, installs, launches, reinstalls and removes
both packages. Manual testing is only needed when the release changes behavior
those checks cannot reach. Look at what changed since the last tag and test the
exact candidate package for each gap that applies:

- Controllers: guide button, rumble, hotplug or new pad mappings on real pads.
- TV and audio: Game Mode display modes, HDMI audio or a real panel's
  fullscreen behavior (see [Game Mode on real hardware](docs/ARCHITECTURE.md#game-mode-on-real-hardware)).
- Launchers: launch, playtime or achievement paths that fixtures do not cover,
  such as a new launcher or a change to how a real game is started or tracked.
- ARM64: aarch64-specific code, dependencies or build flags, on supported ARM64
  hardware.
- Desktop session: services, the guide plugin link or Omarchy integration that
  the isolated test desktop does not exercise.

Docs, CI, packaging metadata and fixes covered by tests need no manual testing.
Say which gaps applied, who tested them and what passed. A critical hotfix may
skip a gap only when the maintainer explicitly accepts that for the release.

## Prepare the candidate

1. Update the version in `CMakeLists.txt`, AppStream metadata, the changelog,
   README package commands, and public feature descriptions. Update
   `omastore.toml` if asset names, executables or the service changed.
2. Run the release configure, build, and tests:

   ```bash
   cmake --preset release
   cmake --build --preset release
   ctest --preset release
   ```

3. Validate the desktop and AppStream files. Inspect dependency scan diagnostics:
   an unknown-distribution warning is not proof of a clean package. For local
   x86_64 Arch SBOM scans, use `grype sbom:PACKAGE.spdx.json --distro arch:rolling
   --only-fixed --fail-on high` and verify a valid vulnerability database. Keep
   ARM64 distribution coverage explicit.
4. Install into an empty staging directory and inspect every installed file.
   Render visual fixtures without opening a desktop window when needed:

   ```bash
   QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
     ./build/release/omakade-uitest --render-screenshot=/tmp/omakade.png \
     --render-size=1380x880
   ```

   Add `--render-overlay=settings` or `--render-overlay=picker` to capture those
   overlays as well.
5. Confirm CI passes on both x86_64 and aarch64. Run the Release workflow
   manually from the candidate branch to build package artifacts without
   publishing them or their attestations. Run any manual tests the release
   needs on those exact artifacts, then give the maintainer the candidate,
   checks and results for authorization.

The source tarball is `git archive` of the tagged commit. `.gitattributes`
leaves out the website, demo media and desktop preview tools; keep
anything a build, `ctest` or the installed docs need out of that list.

## Publish

6. Tag the authorized commit as `vX.Y.Z` and push the tag. Do not push the
   version update to `main` yet.
7. Confirm the Release workflow builds x86_64 and aarch64 packages, installs,
   launches, reinstalls, removes, and reinstalls each package, scans their Arch
   runtime dependencies, and publishes SPDX SBOMs, SHA-256 checksums, and signed
   provenance before creating the GitHub release.
8. Download the public assets, verify their checksums and provenance, then push
   the reviewed commit to `main`. This prevents README install links from going
   live before their release assets.

   ```bash
   sha256sum -c SHA256SUMS
   gh attestation verify omakade-X.Y.Z-1-x86_64.pkg.tar.zst -R btsouth/omakade
   gh attestation verify omakade-X.Y.Z-1-aarch64.pkg.tar.zst -R btsouth/omakade
   gh attestation verify omakade-X.Y.Z.tar.gz -R btsouth/omakade
   ```

9. When the release changes installation, upgrades, services or the plugin
   link, also install, upgrade, remove, and reinstall the published package in
   a disposable Omarchy environment.

Do not publish a package while the source URL or checksum is a placeholder.

## Distribute

These channels pick up a release on their own schedule. None of this is
automated from this repository; check each one after the GitHub release is
public.

### Signed repository (btsouth/pkgs)

The [btsouth/pkgs](https://github.com/btsouth/pkgs) publish workflow runs every
hour and promotes the release packages unchanged after checking them against
`SHA256SUMS` and their attestations. To publish straight away:

```bash
gh workflow run publish.yml -R btsouth/pkgs -f package=omakade -f tag=vX.Y.Z
```

It refuses a package that needs a newer Qt or glibc than Omarchy stable. When
the run passes, `scripts/verify-live.sh` in a btsouth/pkgs checkout confirms
that `pkgs.btso.dev` serves signed databases, and
`pacman -Sl btsouth | grep omakade` shows `X.Y.Z-1` after `omarchy update`.

### Omarchy Package Repository (OPR)

[omacom/omarchy-pkgs](https://github.com/omacom/omarchy-pkgs) builds its own
package from the release tarball. Its sync updates only `pkgver` and the
checksum in `pkgbuilds/omakade/PKGBUILD`. Compare that file with the release's
`PKGBUILD-X.Y.Z` asset:

```bash
gh api repos/omacom/omarchy-pkgs/contents/pkgbuilds/omakade/PKGBUILD \
  --jq .content | base64 -d | diff - PKGBUILD-X.Y.Z
```

If `depends`, `optdepends`, `makedepends`, `install`, `build()` or `package()`
differ, open a pull request there with the same changes. The `install` line needs
`packaging/arch/omakade.install` beside the PKGBUILD there too. OPR moves packages from edge to rc
to stable. Once stable carries the release, confirm its package runs on
stable's Qt, since OPR builds it in its own environment.

### OmaStore

[OmaStore](https://github.com/KitsuneForgering/OmaStore) reads `omastore.toml`
from `main` and installs the latest release's package into the user's home
directory. After the release, run `omastore check btsouth/omakade` and confirm
it finds both architecture packages, the `omakade-sessiond` and
`omakade-guide-button` services and no blockers.

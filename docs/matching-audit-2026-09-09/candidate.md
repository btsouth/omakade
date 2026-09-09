# Local candidate

The September 9 matching candidate is included in the full local Release installation. See [the installation handoff](../LOCAL-INSTALL-2026-09-09.md) for checks, workspace state, backups, and rollback.

The normal launcher is `/home/bts/.local/bin/omakade`. Its target and the exact source commit are recorded in `/home/bts/.local/state/omakade/local-install-20260909/installed-candidate.json`.

Installed application SHA-256: `a1d668f596b33bc4c78e170f29719ee1ced9ef3132d16b3673eda5abb1214e76`.

Validation: fresh Release build; 213/213 isolated CTests; two SBOM generator tests; desktop/AppStream validation; exact staged application smoke test. The matching regression includes all 68 recovered IGDB cases. The optional real-ROM probe remains skipped without probe inputs.

Manual acceptance still covers scrolling the real library in desktop/Couch Mode, editing both search fields, checking recovered identities, and preserving custom artwork. Provider availability is not visual verification of every cover, and the audit's remaining ambiguous identities still require review. No publication approval is implied by this local installation.

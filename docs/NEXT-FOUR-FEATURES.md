# Next four features: complete delivery plan

September 12, 2026. This is the forward execution plan. Older handoffs remain historical
evidence, not the current definition of what to build next.

## Starting point and completion standard

- Development branch: `codex/1.9-testing`, starting at `cb6a699`.
- Installed testing baseline: source `3e57cfa`, with its rollback copy preserved.
- That installed baseline passed 232 tests. Later cover/RomM work passed a full build
  and 21 selected checks; it has not yet passed the expanded full suite.
- RomM has parser, transport, cache and credential helpers. It is not yet a usable source.
- Save backups, eight recent sessions, launch resolution, Needs Review filters and manual
  matching already exist. Extend them rather than creating competing implementations.

A feature is implementation-complete only when its entire user journey works: entry point,
configuration, normal operation, persistence, failures, recovery, keyboard/controller use,
and regression coverage. A parser, service, unconnected panel, or passing unit suite alone
is not feature completion. Track implementation complete, automated validation complete,
installed, and maintainer accepted as separate states.

## Phase 0: consolidate the existing candidate

Deliver a trustworthy development baseline before adding the four features below.

1. Reconcile the current handoff into one current-state section, retaining dated history.
2. Keep the installed app stable while the maintainer uses it. Reproduce reported issues in
   disposable fixtures and fix confirmed regressions before unrelated expansion.
3. Run the expanded full suite at a stable checkpoint, including cover loading and RomM
   transport tests. Verify staged startup, package metadata, and app/recorder compatibility.
4. Record source commit, binary hashes, checks, known limitations, install and rollback paths.
5. Carry already implemented cover and save fixes into the next integrated candidate.

Exit: clean, committed development state; reproducible validation; one clearly identified
installed baseline. Human gameplay acceptance remains distinct and can continue while
independent development proceeds.

## Feature 1: a complete read-only RomM library source

### User outcome

Connect a RomM server, select its locally mounted library, browse supported games alongside
other sources, and launch them with the existing emulator setup. A server outage must not
break the rest of the library or erase the last successful catalog.

### 1A. Connection and credential lifecycle

- Add an opt-in RomM connection section with server URL, mounted folder, masked token entry,
  connection status, Test Connection, Refresh, and Disconnect/Forget Token actions.
- Run keyring operations on workers. Keep network work asynchronous, cancel obsolete requests,
  and prevent old callbacks from applying after server/root/account changes.
- Explain the required read scope in setup. Never display a stored token or include it in
  logs, exports, SQLite, diagnostics, or personal-data archives.
- Distinguish disabled, unconfigured, connecting, connected, stale, unauthorized, unavailable
  mount, and local-storage failure states. Give each failure a useful next action.

### 1B. Library and launch integration

- Implement `RommGameModel` with stable server-scoped identities and all common source roles.
- Connect it to the unified library, source filters, console cards, details, Home where
  eligible, and existing manual linking/preferred-installation behavior.
- Avoid automatic title-only merging. Preserve per-game choices across refresh and restart.
- Route Play through `PlayRequest` and existing emulator resolution. Revalidate file and
  mount availability at launch. Include save protection and recording where the adapter
  supports them; explain unsupported coverage.
- Preserve cached catalog identity independently of whether the mount is temporarily
  available. Mark unavailable games instead of treating a missing mount as a deletion.
- Supply existing artwork fallback and manual artwork selection. Remote cover fetching is
  optional and must not block this feature or reuse authenticated tokens across origins.

### 1C. Failure handling and acceptance

Test invalid token, locked/unavailable keyring, unauthorized scope, expired/removed token,
server changes, offline startup, interrupted refresh, changing pagination, missing/remounted
folder, vanished file, duplicate names, linked installations and cancellation on disable.
Exercise native and Flatpak launch routes where supported. Validate desktop and Couch Mode
setup, folder selection, focus restoration, status and launch-error recovery.

Acceptance journey: configure -> connect -> see catalog -> filter -> open details -> launch
one supported game -> exit -> verify history/save coverage -> restart -> disconnect network
-> inspect cached catalog -> reconnect -> refresh without losing game preferences.

Done: that complete journey works in fixtures and on a real configured server. If a real
server/token is unavailable, finish all mock-backed integration, mark only live acceptance
pending, and continue the next feature without pretending live acceptance passed.

## Feature 2: per-game launch setup and actionable diagnostics

### User outcome

Know what Play will launch, fix a missing or incorrect setup in Omakade, and have the choice
remain correct after restart and rescanning.

### 2A. One resolved launch description

- Reuse the actual launch resolver to expose selected installation, emulator/core, runtime
  availability, game path, native/Flatpak route, and save/recording coverage.
- Use the same resolved result for Home Play, library Play, details, controller actions and
  supported command-line entry points. Do not build a second approximate resolver for UI.
- Distinguish a delegated launch request from verified process start. Detect direct-launch
  failures without claiming that a launcher URL proves the game is running.

### 2B. Setup and recovery UI

- Add a concise Launch Setup area to Game Details: automatic choice, explicit supported
  emulator/core selection, preferred linked installation, and Reset to Automatic.
- Show current selection and why it is unavailable. Offer the relevant selection/folder action
  for missing game files, runtimes or cores, rather than a generic failure dialog.
- Persist settings at installation scope. Define precedence between automatic defaults,
  explicit choices and linked preferred installation; preserve it across rescans and upgrades.
- Add a redacted Copy Launch Details action for diagnosing issues. Do not include credentials,
  unrestricted environment dumps or arbitrary shell execution.
- Keep launch feedback, repeated-press protection, focus and return-to-library behavior
  consistent on desktop and controller.

### 2C. Acceptance

Cover automatic vs explicit choices, unavailable runtimes, path moves, spaces/non-ASCII paths,
archives/playlists, native/Flatpak installs, linked sources, delegated launchers, and failed
start/retry. Confirm source-specific behavior is not flattened into unsupported overrides.

Acceptance journey: encounter unavailable game -> inspect reason -> choose a supported setup
-> launch -> exit -> return to the same selection -> restart -> launch using the saved setup
-> reset to automatic. Verify direct-launch process evidence and delegated behavior separately.

Done: no contradictory launch decisions between entry points; choices survive rescans;
failures are actionable; controller focus returns predictably. Universal command templates,
Windows runner installation and arbitrary environment profiles are outside this feature.

## Feature 3: save-protection overview and recovery management

### User outcome

See which games are protected, what is included, when the last verified backup was made,
and safely manage or recover saves without understanding filesystem layouts.

### 3A. Coverage and storage overview

- Build on `SaveBackups`/save sets. Add a Save Protection view showing supported games,
  coverage state, latest verified backup, count, storage use and actionable errors.
- Distinguish protected, no saves yet, unsupported, ambiguous layout, unavailable folder,
  shared storage, and interrupted recovery. Never call discovery alone a verified backup.
- Make shared memory cards/storage explicit and group their storage accounting to avoid
  presenting the same shared bytes as independent protection for each game.
- Provide selection into the existing per-game snapshot, restore, undo and deletion flow.

### 3B. Configuration and recovery

- Add bounded, validated retention/storage settings using the existing defaults as migration
  defaults. Explain pruning before applying changes; do not silently delete existing backups
  merely because a limit was lowered. Offer a reviewable cleanup action.
- Add an understandable layout-inspection/custom-layout flow only for supported adapters,
  with validation and scope preview. Refuse broad or ambiguous save locations.
- Show restore scope, backup timestamp and shared-storage impact before restoration.
  Preserve the current save as an undo version and surface interrupted recovery clearly.
- Keep operations blocked while affected emulators run. Use conservative detection where
  association cannot be established. Do not weaken safety to make the UI appear successful.

### 3C. Acceptance

Use disposable save trees for unreadable files, full disk, damaged manifests, interrupted
restores, shared cards, legitimate empty sets, retention boundaries and invalid layouts.
Verify original data on every refused operation, successful restore/undo contents, restart
recovery, accurate storage totals and keyboard/controller navigation.

Acceptance journey: identify uncovered game -> inspect/validate its supported save location
-> create verified backup -> change disposable save -> restore -> undo -> restart -> review
history -> delete a selected backup without changing current saves.

Done: protection status and restore behavior agree, failures retain usable backups, and every
operation is reachable on desktop and controller. Cloud synchronization, cross-device conflict
resolution, game downloads and arbitrary save editing are outside this feature.

## Feature 4: a guided library-repair workflow

### User outcome

Work through unmatched games, incorrect artwork, broken paths and duplicate installations in
one understandable queue, with durable corrections and a clear indication of what remains.

### 4A. Unified review state

- Extend existing Needs Review and metadata/editor workflows. Define separate reasons for
  uncertain identity, missing artwork, unavailable installation and potentially duplicate
  installation. Do not label temporary provider/network failure as a bad match.
- Show counts and source filters, and reuse the same review state in library/details.
- Keep candidate duplicates as suggestions; never merge or delete based solely on title.

### 4B. Repair flow and persistence

- Add a next/previous review flow with source/platform/path evidence, current identity and art,
  search/correct, retry, skip, and open launch setup actions.
- Reuse existing matching, explicit rejection, artwork choice, manual linking and preferred
  installation controls. Make explicit choices survive refresh, restart and reimport.
- Support recovery from a mistaken correction with an undo/revert mechanism whose scope is
  explicit. Treat identity and artwork as separate choices.
- Allow safe bulk retry/rescan of selected entries with progress and cancellation. Require
  individual identity confirmation where evidence is ambiguous.
- Preserve review progress when leaving/reopening the queue. Cancellation must not undo
  completed corrections or apply pending choices.

### 4C. Acceptance

Cover same-title different-platform games, regional editions, editions with similar artwork,
offline providers, failures during refresh, disappeared installations, linked games and
undo after restart. Include long libraries and small desktop/Couch layouts.

Acceptance journey: filter Needs Review -> correct identity -> choose artwork -> repair launch
setup where needed -> move to next entry -> cancel background retry -> restart -> confirm
choices remain -> rescan -> verify resolved entries leave the queue and unresolved ones stay.

Done: users can finish a review session without losing place or corrections. No silent
identity replacement, cross-platform merge, or destructive source removal is permitted.

## Sequence, dependencies and delivery

| Milestone | Scope | Why this order | Exit artifact |
| --- | --- | --- | --- |
| M0 | Current correctness baseline | Keep existing work usable and traceable | Exact validated candidate record |
| M1 | Entire RomM feature, 1A through 1C | Finish the feature already underway | Installable source integration plus acceptance results |
| M2 | Entire launch setup feature, 2A through 2C | Fix daily launch friction and support RomM recovery | Installable launch workflow plus route matrix |
| M3 | Entire save management feature, 3A through 3C | Reuse trustworthy installation identity and launch coverage | Installable protection/recovery workflow |
| M4 | Entire library repair feature, 4A through 4C | Reuse source, launch and save diagnostics | Installable repair workflow plus persistence evidence |

Within each milestone: reproduce/specify -> implement backend and data changes -> wire the
full UI journey -> cover failure/recovery paths -> validate -> commit/push -> stage/install
an agreed testing candidate -> record acceptance. Backend and UI substeps are working steps,
not stopping points or separate claims of feature delivery.

Use focused tests during implementation and a full suite plus staged package/startup checks
at each integrated feature milestone. Add meaningful migration fixtures for any new persisted
schema. Do not use a fixed test count as the target; it will grow as workflows are covered.
Use a short physical-controller/gameplay checklist per candidate, covering multiple completed
changes together where possible. Capture exact build and binary identities.

Keep commits coherent and the remote development branch current after relevant checks pass.
Use a draft PR for unfinished integration if a PR is needed. Do not merge main, tag or publish
a release until the exact release candidate is tested and explicitly approved. This plan
makes no promise that all four features must ship in one 1.9 release: choose release boundaries
at completed feature milestones rather than holding fixes for unrelated unfinished work.

## Working agreement: continuous progress without repeated permission prompts

- Existing authorization covers implementation, tests, local commits and development-branch
  pushes. Do not ask again at every file, backend slice, UI slice or test run.
- Keep moving through the full current milestone. If one external acceptance check is blocked,
  record it precisely and continue independent work from this plan.
- Preserve the maintainer's installed testing baseline while coding. Batch candidate installs
  at meaningful checkpoints; avoid restarting the app during an active test session.
- Ask only for genuinely unavailable credentials/hardware decisions, destructive actions not
  already authorized, or final release approval. Ask once with a concrete deliverable.
- Keep workstation limits: no ARM builds/emulation; no compilation during gameplay without an
  explicit override; otherwise low-priority single-job builds with CPUQuota=100%, MemoryHigh=3G,
  MemoryMax=4G and MemorySwapMax=0. Never stop games or VMs to clear a build slot.
- Status reports state completed user outcomes, actual checks and remaining work. Do not
  present infrastructure as a finished feature or configuration checks as gameplay proof.

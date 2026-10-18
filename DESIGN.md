# Haiku Package Installer: Design Document

Oct 14, 2026 · @Sikosis

## Overview and goals

This is a C++17 terminal app for Haiku that installs, updates and removes apps published as `.hpkg` release assets on git hosts, from a catalog you control. It runs alongside HaikuDepot and HaikuPorts and does not depend on either. The tool is called `umami`.

**Goals**

- Install an app from a catalog entry by downloading its release `.hpkg` and handing it to `pkgman`.
- Let Haiku's package system do the real work: activation, file layout, dependency resolution and clean removal.
- Track which catalog entries are installed, at what version, and from which release.
- Update and uninstall by catalog name.
- Stay small: one binary, a plain Makefile, few dependencies.

**Non-goals (for now)**

- Replacing HaikuDepot or hosting a package repository.
- Building from source on the device (kept as an optional later stage, see the source-build section).
- A graphical front end. The core is a library plus a CLI, so a Haiku GUI could be added later.

## Haiku packaging background

On Haiku, `/boot/system` is a read-only view built by packagefs from the active `.hpkg` files, so a traditional `make install` into it does not work. The tool therefore installs packages instead of loose files.

- **Activation:** a `.hpkg` placed in `~/config/packages` (per user) or `/boot/system/packages` (system-wide) is activated automatically by the package daemon.
- **Removal:** removing the package removes everything it provided. User settings and data under `~/config/settings` are left alone.
- **Local files:** `pkgman install` accepts a path to a local `.hpkg` and resolves its dependencies from the enabled repositories. This was tested on the target machine and works.
- **Fallback:** if a Haiku release ever stops accepting local paths, copying the file into the packages directory still activates it, at the cost of automatic dependency fetching.

**Commands the tool relies on**

| Command | Used for |
| --- | --- |
| `pkgman install <file or name>` | Installing a downloaded package and its dependencies |
| `pkgman uninstall <name>` | Removing a package |
| `pkgman update <name>` | Updating from enabled repositories (not used for catalog updates) |
| `pkgman search -i -a <package>` | Confirming that an installed package is active |
| `pkgman resolve-dependencies` | Optional pre-check of unmet requirements |
| `package list -i <file>` | Reading and sanity-checking downloaded package metadata |
| `getarch` | Finding the machine's primary architecture |

Package filenames carry the architecture (for example `x86_64`, `x86_gcc2`), so the tool must pick the asset that matches the machine.

## Architecture

The tool is a core library with a thin CLI on top, so a graphical front end could reuse it later.

&#91;embedded content: architecture · 10 parts in 4 layers\]

Read it top to bottom: the CLI calls the core library, which reads the catalog, writes state only after a step succeeds, and hands the real work to the providers, the downloader and the installer adapter. Only the installer adapter touches Haiku's package system.

**Implementation choices**

- **Language and build:** C++17 with `std::filesystem` and `std::regex`, built with a plain Makefile on Haiku.
- **Processes:** run `pkgman`, `package` and `getarch` with `posix_spawn` and an argument array, capturing their output. Never go through a shell.
- **Networking:** start by running `curl`; move to Haiku's `BHttpRequest` later only if needed.
- **JSON:** a small repository-owned parser for the versioned catalog and state formats, keeping the binary free of an additional JSON dependency.
- **No git library:** release downloads need no clone. Git is only used by the optional source-build mode.
- **Interfaces:** `Provider` and `Installer` are abstract classes, so a new host or a fallback install strategy is one new class.

## Data formats

There are two JSON files: a read-only catalog that lists what can be installed, and a state file the tool writes to record what it installed. Default locations are `~/config/settings/umami/catalog.json` and `~/config/settings/umami/installed.json`. Both carry a `version` field so the format can change later.

**Catalog entry**

```json
{
  "version": 1,
  "apps": [
    {
      "id": "exampleapp",
      "name": "Example App",
      "description": "A sample Haiku application",
      "provider": "github",
      "repo": "someuser/exampleapp",
      "asset_pattern": "^exampleapp-.*-{arch}\\.hpkg$",
      "package_name": "exampleapp",
      "channel": "stable",
      "checksum_asset": "{asset}.sha256"
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `id` | Short unique name used on the command line |
| `provider` | `github`, `gitlab`, `gitea`, or `url` for a direct link |
| `repo` | Owner and repository on the provider |
| `asset_pattern` | Regular expression for the release file; `{arch}` expands to the machine's architecture |
| `package_name` | The name Haiku knows the package by, used for uninstall and verification |
| `channel` | `stable` (latest non-prerelease) or `prerelease` |
| `checksum_asset` | Optional companion file with a SHA-256 hash |

**Installed state entry**

```json
{
  "version": 1,
  "installed": {
    "exampleapp": {
      "package_name": "exampleapp",
      "package_version": "1.2.0-1",
      "release_tag": "v1.2.0",
      "asset": "exampleapp-1.2.0-1-x86_64.hpkg",
      "sha256": "<hash of the downloaded file>",
      "scope": "user",
      "installed_at": "2026-10-14T21:40:00+10:00"
    }
  }
}
```

The state file is a record of intent, not the source of truth. Haiku's own package list is. On `list` and `remove` the tool cross-checks against the packages directory and reports entries that were removed outside the tool.

## Command-line interface

The CLI is a thin layer over the core library. Every command that changes the system asks for confirmation unless `-y` is given.

| Command | What it does |
| --- | --- |
| `umami list` | Show catalog apps with their installed version and the latest release |
| `umami list --installed` | Show only apps installed through the tool |
| `umami info <id>` | Show catalog details, installed version, latest release and asset |
| `umami install <id>[@tag]` | Download, verify and install the latest (or a given) release |
| `umami remove <id>` | Uninstall the package and clear its state entry |
| `umami update [<id>]` | Update one app, or every installed app with a newer release |
| `umami refresh` | Download, validate and atomically install the generated umami cookbook catalogue |
| `umami doctor` | Check state against installed packages, cache and tools |

**Common options**

- `-y` skips confirmation prompts.
- `--system` installs into `/boot/system/packages` instead of `~/config/packages` (needs write permission there).
- `--prerelease` includes prerelease tags for this run.
- `--catalog <file>` uses an alternative catalog.
- `-v` prints each external command and its output.

**Exit codes**

| Code | Meaning |
| --- | --- |
| 0 | Success, or nothing to do |
| 1 | Usage error |
| 2 | Catalog or state file problem |
| 3 | Network or provider failure |
| 4 | Verification failed (checksum, architecture or package metadata) |
| 5 | `pkgman` failed |

## Core flows

**Install**

1. Look up the catalog entry and detect the architecture with `getarch`.
2. Ask the provider for the release (latest stable, or the requested tag).
3. Pick the asset whose name matches `asset_pattern` with `{arch}` filled in. If none matches, stop and list the architectures that are available.
4. Download to the cache directory (`~/config/cache/umami`) under a temporary name, then rename on completion.
5. If a checksum asset exists, download it and compare SHA-256. A mismatch deletes the file and exits with code 4.
6. Run `package list -i <file>` to confirm its name and architecture match the catalogue entry and machine.
7. Run `pkgman install -H -y <file>` and report its output. Dependencies come from the enabled repositories.
8. Confirm the package is now active (`pkgman search -i -a <package_name>`), then write the state entry.

**Remove**

1. Look up the state entry. If the app is not tracked but a package of that name is installed, offer to remove it anyway.
2. Run `pkgman uninstall -H -y <package_name>`.
3. Remove the state entry and optionally the cached `.hpkg` (`--purge-cache`).
4. Settings and data in the user's `~/config/settings` are left in place and the tool says so.

**Update**

1. For each installed entry, fetch the latest release and compare its package version with the installed one.
2. If newer, run the install flow. Installing the newer package should replace the old version. Confirm this on the target machine; if it does not, uninstall first (see open questions).
3. Update the state entry. On failure the old version stays active and the state is unchanged.

**Error handling and safety**

- State is written only after `pkgman` succeeds, and written atomically (temporary file, then rename).
- A lock file in the settings directory stops two instances running at once.
- Network calls have timeouts and a bounded retry with backoff.
- Version comparison uses Haiku's package version rules (such as `1.2.0~beta1-1`), not plain string ordering. A first version can shell out to `pkgman` or `package` for this.

## Staged roadmap

Build in six stages. Stage 1 gives a working install-and-remove tool, and the source-build mode comes last because it is the hardest and the most optional.

&#91;embedded content: staged roadmap · 6 stages, 6 gates\]

Each stage ends at a gate: do not start the next stage until the gate holds on a real Haiku machine.

| Stage | Deliverables | Rough effort |
| --- | --- | --- |
| 0 Setup | Project skeleton, plain Makefile, process-runner wrapper (`posix_spawn`), logging, settings directory handling | About 1 day |
| 1 MVP | Catalog and state parsing, GitHub release provider, download, `pkgman install`, `remove`, `list`, `info` (implemented; Haiku package-operation verification pending) | 2 to 3 days |
| 2 Trust | Architecture matching, SHA-256 verification, `package list -i` check, `update`, `doctor`, state reconciliation, lock file | 3 to 4 days |
| 3 Reach | GitLab, Gitea and direct-URL providers, response caching with ETags, retries, prerelease channel, `--system`, Haiku version comparison | 4 to 5 days |
| 4 Polish | Remote catalogue refinements, tests, user docs, packaging the tool as its own `.hpkg` | 3 to 5 days |
| 5 Source mode (optional) | Recipe format, build runner, staging and layout map, `.PackageInfo` generation, `package create` | 2 to 4 weeks |

Efforts are rough estimates for one developer comfortable with C++. The last stage varies most, depending on how many build systems it supports.

## Optional build-from-source mode

This mode is deferred to the last stage. Most of its effort is packaging, not compiling, and it only applies to catalog entries that have no release `.hpkg`.

A catalog entry opts in by adding a `source` block with a recipe:

```json
"source": {
  "git": "https://github.com/someuser/exampleapp.git",
  "ref": "v1.2.0",
  "build": ["cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/boot/system", "cmake --build build -j4"],
  "stage": "DESTDIR={stage} cmake --install build",
  "layout_map": { "share/applications": "data/applications" },
  "package_info": "packaging/exampleapp.PackageInfo.in"
}
```

**Steps**

1. Clone or update the repository into a work directory and check out `ref`.
2. Run the build commands, logging all output to a per-app build log.
3. Install into a staging directory using `DESTDIR`.
4. Remap Linux-style paths into Haiku's layout (`bin`, `lib`, `apps`, `data`, `documentation`, `develop`) using `layout_map`.
5. Fill in a `.PackageInfo` from the template (name, version, architecture, requires, provides).
6. Run `package create -C <stage> <name>.hpkg`, then continue with the normal install flow from the `pkgman install` step.

**Why it is hard**

- Upstream projects assume `/usr` or `/usr/local` and often glibc extras.
- Build systems vary (make, cmake, meson, jam, qmake), so each needs its own handling and flags.
- Build-time dependencies must be installed first, which makes failures common and logs essential.
- The `.PackageInfo` requires list must match what the binaries actually link against.

## Risks, security and open questions

The biggest practical risk is supply: a catalog entry only works if its author publishes a Haiku `.hpkg` for your architecture.

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Few projects publish `.hpkg` release assets | Catalog is small | Allow direct-URL entries; add source-build mode later |
| Package is malicious or tampered with | Arbitrary code runs as the user | HTTPS only, checksums, show source and version before install, catalog is user-controlled |
| Wrong architecture picked | Install fails or package is rejected | Match with `getarch`, verify with `package list -i`, list available architectures on failure |
| Provider API rate limits (unauthenticated GitHub) | `list` and `update` fail on large catalogs | Cache responses with ETags, allow an optional token, check lazily |
| Local `pkgman` behaviour changes between Haiku releases | Install step breaks | Isolate it behind one install function with a copy-to-packages fallback; `umami doctor` probes it |
| State file out of sync with the system | Wrong status shown | Treat Haiku's package list as the truth and reconcile on `list`, `remove` and `doctor` |

**Security notes**

- The tool never runs downloaded code itself. It only hands the package to `pkgman`.
- Catalog entries and release metadata are untrusted input: validate names and URLs, and never pass them through a shell. Use `posix_spawn` with an argument array.
- Signature verification is not available for plain release assets, so the checksum is only as trustworthy as the host. Pinning a hash in the catalog is the strongest option and could be a later feature.

**Open questions**

- [x] Store source recipes in `sikosis/umami-cookbook`; generate `catalog.json` there and fetch it with `umami refresh`.
- [ ] Per-user installs only, or also `--system` from the start?
- [ ] Libraries for networking and JSON: shell out to `curl` and use nlohmann/json, or use Haiku's `BHttpRequest` and `BJson`?
- [ ] Does Haiku replace an older package of the same name cleanly when a newer local file is installed, or does `update` need to uninstall first? Test on the target machine.
- [ ] Which hosts beyond GitHub matter first (GitLab, Codeberg, a plain URL)?

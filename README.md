# umami

`umami` is a small C++17 command-line package installer for Haiku. It will install, update and remove applications published as `.hpkg` release assets from a user-controlled catalog. Haiku's own `pkgman` remains responsible for package activation, dependency resolution and removal.

The project is currently at **v0.09**. It can refresh and list the shared cookbook, resolve architecture-specific packages from GitHub releases, verify package metadata, install or update packages through `pkgman`, remove tracked packages, and persist installed state atomically.

## Build

On Haiku with a C++17 compiler:

```sh
make
```

The executable is written to `build/umami`.

## Test

```sh
make test
```

## Usage

```text
umami [common options] <command> [arguments]
```

Run `umami --help` for the available and planned command list. Commands which are not implemented yet fail explicitly rather than changing the system.

At startup, `umami` creates these directories when needed:

- `~/config/settings/umami`
- `~/config/cache/umami`

See `DESIGN.md` for the full architecture and staged roadmap.

## Catalog

Download the latest generated catalogue from the shared [umami cookbook](https://github.com/sikosis/umami-cookbook):

```sh
./build/umami refresh
./build/umami list
```

The download is validated before it atomically replaces the local catalogue, so an interrupted download or malformed update leaves the existing catalogue intact.

`list` prints the catalogue source before its application table:

```text
Catalog: https://raw.githubusercontent.com/sikosis/umami-cookbook/main/catalog.json
```

Alternatively, provide a catalog path explicitly:

```sh
umami --catalog /path/to/catalog.json list
umami --catalog /path/to/catalog.json info hum
```

The version 1 reader rejects malformed JSON, missing required fields, duplicate IDs, unsupported providers and unsupported channels. The installed-state file is optional until umami records its first successful installation.

The repository's `catalog.example.json` is a bundled fallback copy. The cookbook contains the Haiku applications and terminal tools published by [sikosis](https://github.com/sikosis). Add, edit or delete an individual file under the cookbook's `recipes/` directory; its workflow validates the recipes and automatically regenerates `catalog.json` after the change reaches `main`. Catalogue membership prepares a project for release lookup but does not guarantee that an installable package has been published. At the time of the v0.09 catalogue update, `fatcat` provides a matching x86_64 `.hpkg`; the other entries require future release assets matching their declared patterns.

## Package operations

On Haiku, start with the currently packaged application:

```sh
./build/umami info fatcat
./build/umami install fatcat
./build/umami update fatcat
./build/umami remove fatcat
```

Use `-y` to skip umami's confirmation prompt. Installation downloads the matching GitHub release asset into `~/config/cache/umami`, checks its name and architecture with `package list -i`, records its SHA-256 checksum, and then invokes `pkgman install -H` for a home-directory installation. State is written only after `pkgman` succeeds and the installed package can be queried.

Package operations must be built and tested on Haiku. In particular, confirm the installed package location and that installing a newer local package cleanly replaces its older version before treating bulk `update` as production-ready.

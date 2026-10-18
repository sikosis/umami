# umami

`umami` is a small C++17 command-line package installer for Haiku. It will install, update and remove applications published as `.hpkg` release assets from a user-controlled catalog. Haiku's own `pkgman` remains responsible for package activation, dependency resolution and removal.

The project is currently at **v0.07**. It can download and validate the shared cookbook catalog, validate an installed-state file, list catalog applications and show local catalog information. Network release lookup and package operations are not implemented yet.

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

Alternatively, provide a catalog path explicitly:

```sh
umami --catalog /path/to/catalog.json list
umami --catalog /path/to/catalog.json info hum
```

The version 1 reader rejects malformed JSON, missing required fields, duplicate IDs, unsupported providers and unsupported channels. The installed-state file is optional until umami records its first successful installation.

The repository's `catalog.example.json` is a bundled fallback copy. The cookbook contains the Haiku applications and terminal tools published by [sikosis](https://github.com/sikosis). Add, edit or delete an individual file under the cookbook's `recipes/` directory; its workflow validates the recipes and automatically regenerates `catalog.json` after the change reaches `main`. Catalogue membership prepares a project for release lookup but does not guarantee that an installable package has been published. At the time of the v0.07 catalogue update, `fatcat` provides a matching x86_64 `.hpkg`; the other entries require future release assets matching their declared patterns.

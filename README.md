# Dache

A content-addressed build cache and asset versioning tool.

## Quick Start

```sh
# Build
make

# Cache a build command
dache -i src/ -o build/ -- make

# Snapshot assets for versioning
dache snapshot -o assets.json assets/
dache restore assets.json
```

## Installation

**Dependencies:** libarchive, OpenSSL, zlib

```sh
# Debian/Ubuntu
sudo apt install libarchive-dev libssl-dev zlib1g-dev

# Build
make
sudo cp build/dache /usr/local/bin/
```

## Usage

### Build Caching

```sh
dache [options] -- <command...>
dache cache [options] -- <command...>  # explicit form

Options:
  -i, --input FILE    Input file/directory (can repeat)
  -o, --output FILE   Output file/directory (can repeat)
  -e, --env VAR=VAL   Environment variable for cache key (can repeat)
  -r, --remote PATH   Remote cache directory for sharing (can repeat)
```

**Examples:**

```sh
# Cache a make build
dache -i src/ -o build/ -e CC=gcc -- make

# Cache a single compilation
dache -i main.c -i util.c -o main -e CC=clang -- clang main.c util.c -o main

# Include config files that affect the build
dache -i src/ -i .cargo/config.toml -o target/ -- cargo build

# Share cache across machines via NFS/shared filesystem
dache -i src/ -o build/ -r /mnt/shared/dache-cache -- make
```

### Asset Snapshots

Version large binary assets (textures, models, audio) without bloating git:

```sh
dache snapshot -o <manifest.json> [-r remote] <files/dirs...>
dache restore [-r remote] <manifest.json>
```

**Workflow:**

```sh
# 1. Snapshot your assets
dache snapshot -o assets.json assets/

# 2. Commit the manifest (small JSON file)
git add assets.json
git commit -m "Update assets"

# 3. Teammates restore from manifest
git pull
dache restore assets.json
```

Assets are stored as content-addressed blobs in `~/.cache/dache/blobs/`. Same file = same blob (deduplication). Different branches can have different manifests pointing to different versions.

### Remote Cache

Share cache artifacts across machines using a shared filesystem:

```sh
# Build with shared remote cache
dache -i src/ -o build/ -r /mnt/nfs/cache -- make

# Snapshot with remote blob storage
dache snapshot -o assets.json -r /mnt/nfs/blobs assets/

# Restore from remote
dache restore -r /mnt/nfs/blobs assets.json
```

**Lookup order:**
1. Local cache (`~/.cache/dache/`)
2. Hook scripts (if present in `~/.config/dache/hooks/`)
3. Remote directory (`--remote` path)

**Custom backends via hooks:**

For S3, HTTP, or other backends, create executable scripts in `~/.config/dache/hooks/`:

```sh
~/.config/dache/hooks/
├── remote-get       # Called: remote-get <key> <local_path>
├── remote-put       # Called: remote-put <key> <local_path>
├── remote-get-blob  # Called: remote-get-blob <sha256> <local_path>
└── remote-put-blob  # Called: remote-put-blob <sha256> <local_path>
```

Example S3 hook (`remote-get`):
```sh
#!/bin/sh
aws s3 cp "s3://my-bucket/cache/$1.tar.gz" "$2" 2>/dev/null
```

## Why?

### or: How I Learned to Stop Worrying and Love the .tar.gz

This project came primarily from game-dev workflows where build cache systems are TOO COMPLICATED.

Dache is a simple thing: just give inputs, outputs, env vars, and the command.

That's it.

It will calculate a cache key from the inputs and environment variables, if found, will restore the cached outputs; if not, it will run the command and cache.

It does not try to canonicalize the command.

It does not try to canonicalize the environment.

It does not try to "infer" what dotfiles your build tool implicitly touches by listening to syscalls.

If you give Dache that information, consistently, it will cache; and if your build tool does that nonsense, that's on them, not us. If you understand your build tool -- tell us what it touches! We'll add it to the cache key. If your build tool doesn't even produce an artifact, but does something like upload a container to a local registry (looking at you, Docker) -- well, that's not cachable. Stop that.

**Give us real inputs.** Not "whatever this script happens to touch." Actual files. Actual directories. Actual content. Not your shell history, the weather in Manila, or the contents of `/proc`.

**Emit real outputs.** If your tool "builds a container and pushes it to a daemon," that's adorable, but it's not an output. We don't cache vibes. Make it output a `.tar.gz`, a `.wasm`, a fat `.jar`. Something we can hash and reuse.

**Declare your dependencies.** We don't have time to spy on your build. You know what you used—just tell us. The alternative? We'd have to subscribe to syscalls or sandbox and chroot the entire build—and then you get... whatever Bazel and Nix are.

**It's okay to rebuild sometimes.** You know what's faster than fighting the cache? `make clean && make`. We're not trying to be perfect. We're trying to be worth it. Dache is a convenience, not a doctrine.

### The Deal

If you give Dache clear inputs, clear outputs, and deterministic commands—it gives you less waiting, fewer re-compiles, and a snappy little wink every time it cache-hits.

If you give Dache nothing? That's okay too. We'll still hang out. We just won't remember anything you did.

## How It Works

**Build cache:**
1. Hash all input files + env vars + command → SHA-256 cache key
2. Cache hit? Extract archived outputs. Done.
3. Cache miss? Run command, archive outputs, store in `~/.cache/dache/<key>.tar.gz`
4. Push to remote (if configured)

**Snapshots:**
1. Hash each file → store as `~/.cache/dache/blobs/<sha256>`
2. Write manifest JSON (paths, hashes, sizes, permissions)
3. Push blobs to remote (if configured)
4. Restore copies blobs back to original paths (fetching from remote if needed)

## Technical Notes

- Written in portable C89 for maximum compatibility
- Uses POSIX extensions (`strdup`, etc.) via `_POSIX_C_SOURCE`
- No external dependencies beyond libarchive, OpenSSL, and zlib

## License

MIT

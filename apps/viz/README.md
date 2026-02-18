# Hephora Viz — ASCII tree visualizer

Hephora Viz is a standalone C++ binary named hephora-sdk-viz.
It renders your workspace data as an ASCII tree.

It can also output JSON for tooling/automation.

## What it does

- Loads your workspace through hephora-sdk-cli commands
- Runs hephora-sdk-cli in non-interactive mode
- Discovers the root profile from schemas
- Lists root nodes
- Recursively expands children with get-children
- Prints a tree with profile, id, and label
- Optionally includes reference links (`--include-refs`) or renders refs only (`--refs-only`)

## Why it is a separate binary

- Keeps hephora-sdk-cli focused on data operations
- Lets visualization evolve independently
- Reuses the same CLI contract without duplicating engine logic

## Build

From repository root:

```bash
cmake -S . -B build
cmake --build build --target viz -j 6
```

Binary path:

- build/apps/viz/hephora-sdk-viz

## Usage

Path inputs follow the same non-interactive convention as hephora-sdk-cli:

- You can pass `--schemas`, `--data`, `--scripts`
- Or omit them and use env vars: `HEPHORA_SCHEMAS`, `HEPHORA_DATA`, `HEPHORA_SCRIPTS`
- CLI flags override environment values

CLI binary resolution order:

1. `HEPHORA_CLI_BIN` (if set)
2. Build-time CLI path (when available)
3. `hephora-sdk-cli` from `PATH`

### Visualize all root nodes

```bash
./build/apps/viz/hephora-sdk-viz \
  --schemas ./temp/schemas \
  --data ./temp/data \
  --scripts ./temp/scripts
```

### Visualize using environment variables

```bash
export HEPHORA_SCHEMAS=./temp/schemas
export HEPHORA_DATA=./temp/data
export HEPHORA_SCRIPTS=./temp/scripts

./build/apps/viz/hephora-sdk-viz
```

### Visualize one subtree

```bash
./build/apps/viz/hephora-sdk-viz \
  --schemas ./temp/schemas \
  --data ./temp/data \
  --scripts ./temp/scripts \
  --profile project \
  --id <project_id> \
  --max-depth 5
```

### JSON output

```bash
./build/apps/viz/hephora-sdk-viz \
  --format json \
  --schemas ./temp/schemas \
  --data ./temp/data \
  --scripts ./temp/scripts
```

### JSON output with reference edges

```bash
./build/apps/viz/hephora-sdk-viz \
  --format json \
  --include-refs \
  --schemas ./temp/schemas \
  --data ./temp/data \
  --scripts ./temp/scripts
```

### References only (tree or JSON)

```bash
# tree
./build/apps/viz/hephora-sdk-viz --refs-only --schemas ./temp/schemas --data ./temp/data --scripts ./temp/scripts

# json
./build/apps/viz/hephora-sdk-viz --format json --refs-only --schemas ./temp/schemas --data ./temp/data --scripts ./temp/scripts
```

### JSON output with all fields (including private keys)

```bash
./build/apps/viz/hephora-sdk-viz \
  --format json \
  --show-all-fields \
  --schemas ./temp/schemas \
  --data ./temp/data \
  --scripts ./temp/scripts
```

Includes extra keys per node: `_profile`, `_id`, `_label`, `_parent_id`.

### Filter roots with query passthrough

`--query` passes JSON directly to `hephora-sdk-cli query` and uses matching rows as visualization roots.

```bash
./build/apps/viz/hephora-sdk-viz \
  --query '{"profile":"attachment","query":[[{"field":"filename","operator":"EQ","value":"spec.pdf"}]]}' \
  --schemas ./temp/schemas \
  --data ./temp/data \
  --scripts ./temp/scripts
```

### Help

```bash
./build/apps/viz/hephora-sdk-viz --help
```

### Colors

Tree output supports ANSI colors:

```bash
# default: colors only when writing to a terminal

# disable colors explicitly
./build/apps/viz/hephora-sdk-viz --no-color ...
```

## Arguments

- --schemas <dir> : schema folder
- --data <dir> : data folder
- --scripts <dir> : Lua scripts folder
- --format <tree|json> : output format (default: tree)
- --show-all-fields : include full fields and private keys in JSON output only (ignored in tree format)
- --query <json> : passthrough to CLI `query`; returned rows become traversal roots
- --include-refs : include reference links in output
- --refs-only : show only reference links (implies `--include-refs`)
- --no-color : disable tree colors
- --profile <name> : optional start profile
- --id <node-id> : optional start id (requires --profile)
- --max-depth <n> : optional traversal limit (default 6)

## How traversal works

1. If --profile and --id are provided, traversal starts from that node.
2. Otherwise, the tool runs schemas to find the root profile.
3. Then it runs list <root-profile> to fetch root nodes.
4. For each node, it runs get-children <profile> <id> recursively.
5. A visited set prevents cycles from looping forever.

## Notes

- hephora-sdk-viz depends on hephora-sdk-cli at build time.
- It requires valid workspace paths, same as non-interactive CLI mode.
- Those paths can come from flags or `HEPHORA_*` env vars.
- Output is terminal-friendly and suitable for quick inspection.

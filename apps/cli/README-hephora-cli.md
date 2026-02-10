# Hephora CLI — Usage Guide

This document explains how to use the interactive **hephora-sdk-cli** to manage
schema-driven YAML data stored via an SQLite backend. It covers creating and
updating nodes, working with arrays (strings, references, and arrays of
objects), querying, and exporting to YAML.

> **Platform**: Linux only (currently).

---

## Quick Start

```bash
./hephora-sdk-cli
hephora-sdk-cli — type 'help' to see commands.
> load-schemas ./schemas
schemas loaded. root = project
```

## Non-interactive mode

```bash
./hephora-sdk-cli --schemas ./schemas --data ./data --scripts ./scripts list project
```

> Non-interactive mode requires `--schemas`, `--data`, and `--scripts` so the
> workspace is always initialized before the command runs.

> The CLI must load your **profile schemas** before you can create data. The
> directory should contain `.yaml` or `.yml` files that define profiles such as
> `project`, `requirement`, and `attachment`.

## Output format

All commands return **JSON** on stdout so tooling can parse responses reliably.

Success:
```
{"ok":true,"command":"list","result":{...}}
```

Error:
```
{"ok":false,"command":"create","error":"..."}
```

### Create some data

> IDs are auto-generated. The created node (including its `id`) is returned in the `create` response.

```bash
> create project label="Phoenix" project_name=Phoenix version=1
{"ok":true,"command":"create","result":{...}}

> create requirement parent=<project_id> title="Brake latency under 100 ms" priority=high active=true
{"ok":true,"command":"create","result":{...}}

> create attachment parent=<project_id> filename="spec.pdf" filetype=pdf path="/docs/spec.pdf"
{"ok":true,"command":"create","result":{...}}
```

### Link a requirement to an attachment

`references` is an `array<reference>` to profile `attachment`.
Use the array helpers:

```bash
> arr-add requirement <requirement_id> references <attachment_id>       # push one reference
{"ok":true,"command":"arr-add","result":{...}}

> get requirement <requirement_id>
{"ok":true,"command":"get","result":{...}}
```

---

## Commands

```
help
load-schemas <dir>
load-scripts <dir>
load-workspace <schemas> <data> <scripts>
create <profile> [k=v ...]
update <profile> <id> [k=v ...]
delete <profile> <id>
get <profile> <id>
get-many <profile> <id...>
get-by-id <id>
get-by-ids <id...>
get-field <profile> <id> <field>
get-select <profile> <id> <field>[i][.sub]
list <profile>
query <json>
get-children <profile> <id>
get-children-by-id <id>
get-refs-to-by-id <id>
get-profiles
get-schema <profile>
execute-command <profile> <id> <command>
schemas
load-data <dir>

# Arrays
arr-add <profile> <id> <field> <value...>
arr-del <profile> <id> <field> [index=<n>] [value=<v>]
arr-set <profile> <id> <field>[i][.sub]=<value>
```

### `create` vs `update`

- **create** inserts a new row. For **child** profiles you **must** pass
  `parent=<parent_id>`. For root profiles, do **not** pass `parent`.
- **update** patches an existing row. You only pass the fields you want to
  change; label/parent are preserved if you don’t supply them.
- **arrays** are **not** allowed in `create`/`update` values. Use the `arr-*`
  commands to add, set, or remove array items.

**Examples**

```bash
# Create
create project label="Nova" project_name=Nova version=2
create requirement parent=<project_id> title="Boot under 2s" priority=medium active=true

# Patch specific fields
update requirement <requirement_id> priority=high
update requirement <requirement_id> title="Boot under 1.5s"
```

---

## Reading data

### Whole node
```bash
get <profile> <id>
```

### Batch and id‑only helpers
```bash
get-many <profile> <id...>
get-by-id <id>
get-by-ids <id...>
```

### One field only
```bash
get-field <profile> <id> <field>
# examples
get-field requirement <requirement_id> title
get-field requirement <requirement_id> specs
get-field requirement <requirement_id> references
get-field requirement <requirement_id> objects
```

### Array selectors
```bash
get-select <profile> <id> <field>[i][.sub]
# examples
get-select requirement <requirement_id> references[0]
get-select requirement <requirement_id> objects[0]          # whole object
get-select requirement <requirement_id> objects[0].name     # one property
```

> The CLI pretty-printer expands arrays of objects for readability in `get`,
> `get-field`, and `get-select`.

### Children and references (id‑only)
```bash
get-children-by-id <id>
get-refs-to-by-id <id>
```
> These return **summaries only** (no `fields`). Use `get` for full nodes.

---

## Working with arrays

Array fields can be `array<string>`, `array<reference>`, or `array<object>`.

### Add elements — `arr-add`

```
arr-add <profile> <id> <field> <value...>
```

- For `array<string>` and `array<reference>`: pass a single token (quote it if
  it has spaces).  
  `arr-add requirement R-1 tags "hard real-time"`  
  `arr-add requirement R-1 references A-1`

- For `array<object>`: **two styles** are supported

  1) **Inline literal** (JSON-ish):
     ```bash
     arr-add requirement R-1 objects '{name:"Brake ECU", value:"HW-REV-B"}'
     ```

  2) **Key-value pairs**:
     ```bash
     arr-add requirement R-1 objects name="Brake ECU" value="HW-REV-B"
     ```

> The CLI merges the new element with the existing array and patches the row.

### Modify an element — `arr-set`

```
arr-set <profile> <id> <field>[i][.sub]=<value>
```

- Set a scalar/ref at index:  
  `arr-set requirement R-1 tags[0]=timing`

- Set a property of an object at index:  
  `arr-set requirement R-1 objects[0].name="Brake ECU v2"`

> If the target index doesn’t exist yet, the CLI expands the array and fills
> gaps with `null`.

### Remove elements — `arr-del`

```
arr-del <profile> <id> <field> index=<n> | value=<v>
```

- By index:  
  `arr-del requirement R-1 objects index=0`

- By value (strings/refs only):  
  `arr-del requirement R-1 tags value="hard real-time"`

---

## Objects and nested fields

You can set object sub-fields during create/update with `top.sub=value`:

```bash
create requirement R-3 parent=P-1 title="Abs test" specs.manufacturer=Acme specs.warranty_years=5
get requirement R-3
# ...
# specs: {
#   manufacturer: "Acme"
#   warranty_years: 5
# }
```

> For arrays of objects, use `arr-add` with object style or `arr-set
> <field>[i].sub=value` for editing.

---

## Persistence

Changes are persisted to YAML automatically after mutating commands
(`create`, `update`, `delete`, `arr-add`, `arr-del`, `arr-set`, `execute-command`).

### Load data from YAML

```bash
load-data ./data
```

The CLI walks the directory recursively, reads all `.yml/.yaml`, and loads them
in parent→child order with a two-pass strategy so that `array<reference>` fields
are resolved after all rows exist.

---

## Troubleshooting & Tips

- **“unknown profile”** — run `load-schemas <dir>` first.
- **“profile is a child … use parent=<id>”** — you must pass a parent for child
  nodes on create.
- **Updating arrays does nothing** — remember the rules:
  - `arr-add` merges a **single** element into the existing array.
  - `arr-set field[i]=…` replaces a slot or sub-field.
  - `arr-del` needs either `index=<n>` or `value=<v>`.
- **Not seeing array objects in `get`** — use `get-field` or `get-select` to
  focus the output, or ensure your build contains the newer pretty-printer that
  expands arrays of objects (see CLI source).

---

## Examples (end-to-end)

```bash
# 1) Load schemas
load-schemas ./schemas

# 2) Create project and requirement
create project label="Phoenix" project_name=Phoenix version=1
create requirement parent=<project_id> title="Brake latency under 100 ms" priority=high active=true

# 3) Add references and objects to the requirement
create attachment parent=<project_id> filename="spec.pdf" filetype=pdf path="/docs/spec.pdf"
arr-add requirement <requirement_id> references <attachment_id>
arr-add requirement <requirement_id> tags "hard real-time"
arr-add requirement <requirement_id> objects name="Brake ECU" value="HW-REV-B"

# 4) Inspect
get requirement <requirement_id>
get-field requirement <requirement_id> objects
get-select requirement <requirement_id> objects[0]
get-select requirement <requirement_id> objects[0].name

# 5) Modify and remove
arr-set requirement <requirement_id> objects[0].name="Brake ECU v2"
arr-del requirement <requirement_id> tags value="hard real-time"

# 6) Save to YAML
# (auto-flushed after mutations)
```

---

## Command Reference (concise)

- `create <prof> [k=v ...]`
- `update <prof> <id> [k=v ...]`
- `delete <prof> <id>`
- `get <prof> <id>`
- `get-many <prof> <id...>`
- `get-by-id <id>`
- `get-by-ids <id...>`
- `get-field <prof> <id> <field>`
- `get-select <prof> <id> <field>[i][.sub]`
- `list <prof>`
- `arr-add <prof> <id> <field> <value...>`
- `arr-del <prof> <id> <field> index=<n> | value=<v>`
- `arr-set <prof> <id> <field>[i][.sub]=<value>`
- `load-schemas <dir>`
- `load-data <dir>`
- `get-children <prof> <id>`
- `get-children-by-id <id>`
- `get-refs-to-by-id <id>`
Changes are auto-flushed; no explicit flush commands.

---

Happy hacking!

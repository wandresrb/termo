# Plugins

A plugin is a Git repository with a `termo.json` manifest at its root and Lua under
`lua/`. `termo.pack` installs, pins, updates and loads them; the design is Neovim's
`vim.pack`.

## Declaring plugins

```lua
termo.pack.setup({
	"someone/termo-git",
	{ src = "https://git.example/x.git", name = "x", version = "v2.1.0" },
	{ src = "someone/termo-tabs", version = { range = "1.x" } },
}, { confirm = true, load = true, done = function(errors) end })
```

A string is a GitHub `owner/repo`; a table takes `src` (anything `git clone` accepts),
`name` (default: the repository name), `version` (a branch, tag, commit, or
`{ range = "1.x" }` matched against the repository's semver tags, default: the remote's
default branch), `data` (kept for the plugin) and `dependencies` (specs installed and
loaded before it).

`setup` is the one declaration in `init.lua`. It loads what is installed and, for what is
missing, asks once on the attached client (or on the first client that attaches when the
server starts detached) before cloning. `confirm = false` skips the question; `load =
false` installs and registers without running anything; `load` may also be a function
`({ spec, path, manifest })` that decides.

## Lockfile

`~/.config/termo/termo-pack-lock.json`, next to `init.lua`, records the commit of every
plugin after each install, update or removal. On a machine with the lockfile, `setup`
installs those exact commits. Commit it with your dotfiles.

## Updating and removing

`termo.pack.update(names, { force, offline, done })` fetches, resolves the target of each
`version`, and for every plugin with new commits shows them in a menu with Update and
Skip; `force = true` applies without asking. `done(report, errors)` receives one entry
per plugin with `old`, `new`, `log` and `applied`.

A plugin removed from the `setup` list stays on disk, inactive; `termo.pack.del(names)`
removes plugins and `termo.pack.clean()` removes every installed plugin the current list
does not declare or depend on. `termo.pack.get(names)` returns `{ name, spec, path, rev,
active, load_ms }` per plugin; `load_ms` is the CPU time its main chunk took.

Every change fires `@pack-changed-pre` before and `@pack-changed` after, with `name`,
`kind` (`install`, `update`, `delete`), `src`, `path` and `rev`.

## Lazy loading

A plugin with triggers is installed and registered but its `main` runs on the first
trigger. Triggers go in the spec:

```lua
termo.pack.setup({
	{ src = "someone/termo-git", keys = { { "prefix", "g", "run-lua 'termo.git.open()'" } } },
	{ src = "someone/termo-stats", format = "stats", event = "session-created" },
	{ src = "someone/termo-mac", cond = function() return termo.eval("#{host}") == "mac" end },
	{ src = "someone/termo-tool", lazy = true },
})
```

- `keys`: a list of `{ table, key, action, note = }` (or `{ key = , command = }`); the stub
  loads the plugin and runs `action`, a command string or a function of the key event.
  The manifest's `keys` are triggers too, so a plugin that declares its keys is lazy by
  itself.
- `event`: an event name or list; the first one loads the plugin.
- `format`: a `#{name}` or list; the first evaluation loads the plugin. The plugin's own
  `termo.format.add` for that name takes over from there.
- `cond`: a boolean or a function; `false` leaves the plugin installed and inactive.
- `lazy = true`: only `termo.pack.load(name)` loads it; `lazy = false` loads eagerly
  even when the manifest declares keys.

- `cmd`: a command name or list; the stub command loads the plugin and runs the
  plugin's own definition of the command with the same arguments. The manifest's
  `commands` (`{ name, desc, nargs }`) are `cmd` triggers too.
- `mode`: a mode name or list; entering the mode loads the plugin.
`termo.pack.get()` reports `lazy` and `active` per plugin.

## The manifest

```json
{
	"name": "termo-git",
	"main": "lua/termo-git.lua",
	"version": "0.3.0",
	"dependencies": ["someone/termo-lib"],
	"lib": "target/release/libtermo_git.so"
}
```

`name` is required and is the install directory. `main` defaults to `lua/<name>.lua`;
the chunk receives one argument, `{ name, dir, manifest, spec, lib }`, runs with the
plugin's `lua/` on `package.path`, and its return value is kept. It runs in an
environment over a read-only `_G`: setting a global is an error. `keys` and `formats` are lazy-loading
triggers, and so are `commands` (above); `events` is parsed and not acted on yet.

## Native plugins

`lib` names a shared library relative to the plugin directory. `termo.pack` loads it
with LuaJIT's `ffi.load` and hands it to the chunk as `plugin.lib`; the chunk declares the
signatures with `ffi.cdef`. Plugins are trusted code, as in Neovim: the read-only
environment guards against accidental globals, not against intent, and a panic or crash
in native code takes the server down. Build the library with C signatures, `extern "C"`
in Rust:

```rust
#[unsafe(no_mangle)]
pub extern "C" fn termo_git_status(path: *const c_char) -> *mut c_char { ... }
```

# Crates

Rust dependencies that link into the termo binary are vendored as Meson subprojects and
built without cargo (`docs/sdlc/intent/006-rust.md`, decision 2). Every entry here was
read before it was vendored; the note says what was checked.

| Crate | Version | Licence | Vendored in | Note |
|---|---|---|---|---|
| `unicode-width` | 0.2.2 | MIT OR Apache-2.0 | plan 006 step 4 | Unicode width tables; no `build.rs`, no dependencies; built with `feature="cjk"` and `feature="std"`; `src/` is generated tables plus one lookup function, read end to end |
| `unicode-segmentation` | 1.13.3 | MIT OR Apache-2.0 | plan 006 step 4 | UAX #29 grapheme, word and sentence boundaries; no `build.rs`, no dependencies; only `GraphemeCursor` and `graphemes()` are used |

Each crate is a Meson wrap (`subprojects/<name>.wrap`, `wrap-file` from crates.io pinned by
sha256) with a `subprojects/packagefiles/<name>/meson.build` that builds it as an rlib with
plain `rustc` (`rust_abi: 'rust'`, `--cap-lints allow`, the crate's edition and features as
`rust_args`) and overrides `dependency('<name>')`. Meson downloads the tarball at setup and
extracts it under `subprojects/`, which is gitignored; `meson subprojects download` fetches
ahead of time. A crate with a `build.rs` or with dependencies of its own does not fit this
scheme and is not on the list.

`src/rs/Cargo.toml` exists for the tooling only (`cargo clippy`, `cargo fmt`,
`cargo +nightly miri test`, rust-analyzer); it references the same sources by `path` and
is never what builds `libtermo_rs.a`.

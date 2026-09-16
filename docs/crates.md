# Crates

Rust dependencies that link into the termo binary are vendored as Meson subprojects and
built without cargo (`docs/sdlc/intent/006-rust.md`, decision 2). Every entry here was
read before it was vendored; the note says what was checked.

| Crate | Version | Licence | Vendored in | Note |
|---|---|---|---|---|

`src/rs/Cargo.toml` exists for the tooling only (`cargo clippy`, `cargo fmt`,
`cargo +nightly miri test`, rust-analyzer); it references the same sources by `path` and
is never what builds `libtermo_rs.a`.

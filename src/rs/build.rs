use std::{env, fs, path::PathBuf};

fn main() {
    let out = PathBuf::from(env::var("OUT_DIR").unwrap()).join("bindings.rs");
    let build = env::var("TERMO_BUILD_DIR").unwrap_or_else(|_| "../../build".to_string());
    let src = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap())
        .join(build)
        .join("bindings.rs");
    println!("cargo:rerun-if-changed={}", src.display());
    println!("cargo:rerun-if-env-changed=TERMO_BUILD_DIR");
    fs::copy(&src, &out)
        .unwrap_or_else(|e| panic!("{}: {e}; run meson setup build first", src.display()));
}

#![deny(
    unsafe_code,
    unsafe_op_in_unsafe_fn,
    improper_ctypes,
    improper_ctypes_definitions,
    clippy::arithmetic_side_effects,
    clippy::cast_possible_truncation,
    clippy::cast_sign_loss
)]
#![warn(clippy::pedantic)]
#![allow(clippy::indexing_slicing)]

#[allow(
    unsafe_code,
    non_camel_case_types,
    non_upper_case_globals,
    non_snake_case,
    dead_code,
    improper_ctypes,
    clippy::all,
    clippy::pedantic
)]
mod bindings {
    #[cfg(termo_meson)]
    include!("bindings/bindings.rs");
    #[cfg(not(termo_meson))]
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub mod ffi;
pub mod sys;

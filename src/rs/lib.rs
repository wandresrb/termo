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

pub mod ffi;

pub const ABI: u32 = 1;

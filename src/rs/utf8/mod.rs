pub mod combined;
pub mod cstr;
pub mod decode;
pub mod pack;
pub mod vis;
pub mod width;

pub const MAX_BYTES: usize = 32;

pub use decode::{Decode, INVALID_WIDTH, Utf8Char};
pub use pack::{Intern, Packed};
pub use width::Widths;

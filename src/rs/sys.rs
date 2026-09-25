use core::mem::{align_of, size_of};

use crate::bindings;

const _: () =
    assert!(size_of::<bindings::utf8_data>() == 35 && align_of::<bindings::utf8_data>() == 1);
const _: () =
    assert!(size_of::<bindings::grid_cell>() == 56 && align_of::<bindings::grid_cell>() == 4);
const _: () = assert!(
    size_of::<bindings::grid_cell_entry>() == 5 && align_of::<bindings::grid_cell_entry>() == 1
);
const _: () = assert!(
    size_of::<bindings::grid_extd_entry>() == 23 && align_of::<bindings::grid_extd_entry>() == 1
);
const _: () =
    assert!(size_of::<bindings::grid_line>() == 40 && align_of::<bindings::grid_line>() == 8);
const _: () = assert!(size_of::<bindings::grid>() == 56 && align_of::<bindings::grid>() == 8);
const _: () = assert!(bindings::UTF8_SIZE == 32);

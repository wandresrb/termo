pub const ABI: u32 = 1;

#[unsafe(no_mangle)]
pub extern "C" fn termo_rs_abi() -> u32 {
    ABI
}

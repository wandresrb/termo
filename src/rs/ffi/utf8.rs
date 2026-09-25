use core::ffi::{CStr, c_char, c_int};
use core::mem::size_of;
use core::ptr;
use std::sync::{LazyLock, Mutex, MutexGuard, PoisonError};

use crate::bindings::{
    self, VIS_DQ, hanguljamo_state, u_char, u_int, utf8_char, utf8_data, utf8_state, wchar_t,
};
use crate::utf8::combined::{self, HangulState};
use crate::utf8::{Decode, Intern, MAX_BYTES, Packed, Utf8Char, Widths, cstr, pack, vis};

static WIDTHS: LazyLock<Mutex<Widths>> = LazyLock::new(|| Mutex::new(Widths::new()));
static INTERN: LazyLock<Mutex<Intern>> = LazyLock::new(|| Mutex::new(Intern::default()));

fn widths() -> MutexGuard<'static, Widths> {
    WIDTHS.lock().unwrap_or_else(PoisonError::into_inner)
}

fn intern() -> MutexGuard<'static, Intern> {
    INTERN.lock().unwrap_or_else(PoisonError::into_inner)
}

const _: () = assert!(size_of::<Utf8Char>() == size_of::<utf8_data>());

unsafe fn chr<'a>(ud: *const utf8_data) -> &'a Utf8Char {
    unsafe { &*ud.cast::<Utf8Char>() }
}

unsafe fn chr_mut<'a>(ud: *mut utf8_data) -> &'a mut Utf8Char {
    unsafe { &mut *ud.cast::<Utf8Char>() }
}

unsafe fn bytes<'a>(s: *const c_char) -> &'a [u8] {
    unsafe { CStr::from_ptr(s) }.to_bytes()
}

unsafe fn bytes_n<'a>(s: *const c_char, n: usize) -> &'a [u8] {
    if n == 0 {
        return &[];
    }
    unsafe { core::slice::from_raw_parts(s.cast::<u8>(), n) }
}

unsafe fn items<'a>(s: *const utf8_data) -> &'a [Utf8Char] {
    let mut n = 0;
    while unsafe { (*s.add(n)).size } != 0 {
        n = n.strict_add(1);
    }
    unsafe { core::slice::from_raw_parts(s.cast::<Utf8Char>(), n) }
}

fn malloc(n: usize) -> *mut u8 {
    let Ok(len) = n.try_into() else {
        std::process::abort();
    };
    let p = unsafe { bindings::malloc(len) };
    if p.is_null() {
        std::process::abort();
    }
    p.cast::<u8>()
}

fn cstring(b: &[u8]) -> *mut c_char {
    let p = malloc(b.len().strict_add(1));
    unsafe {
        ptr::copy_nonoverlapping(b.as_ptr(), p, b.len());
        p.add(b.len()).write(0);
    }
    p.cast::<c_char>()
}

fn state(d: Decode) -> utf8_state {
    match d {
        Decode::More => utf8_state::UTF8_MORE,
        Decode::Done => utf8_state::UTF8_DONE,
        Decode::Error => utf8_state::UTF8_ERROR,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_towc(ud: *const utf8_data, wc: *mut wchar_t) -> utf8_state {
    let c = unsafe { chr(ud) }.first_char();
    match c.and_then(|c| wchar_t::try_from(u32::from(c)).ok()) {
        Some(w) => {
            unsafe { wc.write(w) };
            utf8_state::UTF8_DONE
        }
        None => utf8_state::UTF8_ERROR,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_fromwc(wc: wchar_t, ud: *mut utf8_data) -> utf8_state {
    let c = u32::try_from(wc).ok().and_then(char::from_u32);
    match c.and_then(|c| Utf8Char::from_char(c, &widths())) {
        Some(c) => {
            *unsafe { chr_mut(ud) } = c;
            utf8_state::UTF8_DONE
        }
        None => utf8_state::UTF8_ERROR,
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_update_width_cache() {
    let mut entries: Vec<Vec<u8>> = Vec::new();
    unsafe {
        let options = ptr::addr_of!(bindings::global_options).read();
        let o = bindings::options_get(options, c"codepoint-widths".as_ptr());
        let mut a = if o.is_null() {
            ptr::null_mut()
        } else {
            bindings::options_array_first(o)
        };
        while !a.is_null() {
            let s = (*bindings::options_array_item_value(a)).string;
            if !s.is_null() {
                entries.push(bytes(s).to_vec());
            }
            a = bindings::options_array_next(a);
        }
    }
    widths().rebuild(entries.iter().map(Vec::as_slice));
}

#[unsafe(no_mangle)]
pub extern "C" fn utf8_build_one(ch: u_char) -> utf8_char {
    Packed::ascii(ch).0
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_from_data(ud: *const utf8_data, uc: *mut utf8_char) -> utf8_state {
    let (st, p) = match pack::pack(unsafe { chr(ud) }, |c| intern().put(c)) {
        Ok(p) => (utf8_state::UTF8_DONE, p),
        Err(p) => (utf8_state::UTF8_ERROR, p),
    };
    unsafe { uc.write(p.0) };
    st
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_to_data(uc: utf8_char, ud: *mut utf8_data) {
    *unsafe { chr_mut(ud) } = pack::unpack(Packed(uc), |i| intern().get(i).copied());
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_set(ud: *mut utf8_data, ch: u_char) {
    *unsafe { chr_mut(ud) } = Utf8Char::ascii(ch);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_copy(to: *mut utf8_data, from: *const utf8_data) {
    let from = unsafe { from.cast::<Utf8Char>().read() };
    unsafe { chr_mut(to) }.copy_from(&from);
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_open(ud: *mut utf8_data, ch: u_char) -> utf8_state {
    state(unsafe { chr_mut(ud) }.open(ch))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_append(ud: *mut utf8_data, ch: u_char) -> utf8_state {
    let c = unsafe { chr_mut(ud) };
    let st = c.append(ch);
    if st == Decode::Done {
        c.set_width(&widths());
    }
    state(st)
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_isvalid(s: *const c_char) -> c_int {
    c_int::from(cstr::is_valid(unsafe { bytes(s) }))
}

fn escape(c: u8, next: u8, flag: c_int, out: &mut Vec<u8>) {
    let mut buf = [0u8; 8];
    let end = unsafe {
        bindings::vis(
            buf.as_mut_ptr().cast::<c_char>(),
            c_int::from(c),
            flag,
            c_int::from(next),
        )
    };
    let n = usize::try_from(unsafe { end.cast::<u8>().offset_from(buf.as_ptr()) }).unwrap_or(0);
    out.extend_from_slice(&buf[..n.min(buf.len())]);
}

fn strvis(src: &[u8], flag: c_int) -> Vec<u8> {
    let mut out = Vec::with_capacity(src.len());
    let dollar = u32::try_from(flag).is_ok_and(|f| f & VIS_DQ != 0);
    vis::strvis(src, dollar, &mut out, |c, next, out| {
        escape(c, next, flag, out);
    });
    out
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_strvis(
    dst: *mut c_char,
    src: *const c_char,
    len: usize,
    flag: c_int,
) -> usize {
    let out = strvis(unsafe { bytes_n(src, len) }, flag);
    unsafe {
        ptr::copy_nonoverlapping(out.as_ptr(), dst.cast::<u8>(), out.len());
        dst.add(out.len()).write(0);
    }
    out.len()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_stravis(
    dst: *mut *mut c_char,
    src: *const c_char,
    flag: c_int,
) -> usize {
    let out = strvis(unsafe { bytes(src) }, flag);
    unsafe { dst.write(cstring(&out)) };
    out.len()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_stravisx(
    dst: *mut *mut c_char,
    src: *const c_char,
    srclen: usize,
    flag: c_int,
) -> usize {
    let out = strvis(unsafe { bytes_n(src, srclen) }, flag);
    unsafe { dst.write(cstring(&out)) };
    out.len()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_sanitize(src: *const c_char) -> *mut c_char {
    cstring(&cstr::sanitize(unsafe { bytes(src) }, &widths()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_strlen(s: *const utf8_data) -> usize {
    unsafe { items(s) }.len()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_strwidth(s: *const utf8_data, n: isize) -> u_int {
    cstr::str_width(unsafe { items(s) }, usize::try_from(n).ok())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_fromcstr(src: *const c_char) -> *mut utf8_data {
    let items = cstr::from_cstr(unsafe { bytes(src) }, Some(&widths()));
    let p = malloc(items.len().strict_add(1).strict_mul(size_of::<Utf8Char>())).cast::<Utf8Char>();
    unsafe {
        ptr::copy_nonoverlapping(items.as_ptr(), p, items.len());
        p.add(items.len()).write(Utf8Char::empty());
    }
    p.cast::<utf8_data>()
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_tocstr(src: *mut utf8_data) -> *mut c_char {
    cstring(&cstr::to_cstr(unsafe { items(src) }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_cstrwidth(s: *const c_char) -> u_int {
    cstr::cstr_width(unsafe { bytes(s) }, &widths())
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_padcstr(s: *const c_char, width: u_int) -> *mut c_char {
    cstring(&cstr::pad(unsafe { bytes(s) }, width, &widths()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_rpadcstr(s: *const c_char, width: u_int) -> *mut c_char {
    cstring(&cstr::rpad(unsafe { bytes(s) }, width, &widths()))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_cstrhas(s: *const c_char, ud: *const utf8_data) -> c_int {
    c_int::from(cstr::cstr_has(
        unsafe { bytes(s) },
        unsafe { chr(ud) },
        &widths(),
    ))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_has_zwj(ud: *const utf8_data) -> c_int {
    c_int::from(combined::has_zwj(unsafe { chr(ud) }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_is_zwj(ud: *const utf8_data) -> c_int {
    c_int::from(combined::is_zwj(unsafe { chr(ud) }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_is_vs(ud: *const utf8_data) -> c_int {
    c_int::from(combined::is_vs(unsafe { chr(ud) }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_is_hangul_filler(ud: *const utf8_data) -> c_int {
    c_int::from(combined::is_hangul_filler(unsafe { chr(ud) }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn utf8_should_combine(
    with: *const utf8_data,
    add: *const utf8_data,
) -> c_int {
    c_int::from(combined::should_combine(unsafe { chr(with) }, unsafe {
        chr(add)
    }))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn hanguljamo_check_state(
    p_ud: *const utf8_data,
    ud: *const utf8_data,
) -> hanguljamo_state {
    match combined::hangul_state(unsafe { chr(p_ud) }, unsafe { chr(ud) }) {
        HangulState::NotHanguljamo => hanguljamo_state::HANGULJAMO_STATE_NOT_HANGULJAMO,
        HangulState::Choseong => hanguljamo_state::HANGULJAMO_STATE_CHOSEONG,
        HangulState::Composable => hanguljamo_state::HANGULJAMO_STATE_COMPOSABLE,
        HangulState::NotComposable => hanguljamo_state::HANGULJAMO_STATE_NOT_COMPOSABLE,
    }
}

const _: () = assert!(MAX_BYTES == bindings::UTF8_SIZE as usize);

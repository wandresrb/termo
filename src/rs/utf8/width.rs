use std::collections::HashMap;

use unicode_width::UnicodeWidthChar;

use super::cstr::{Item, scan};
use super::decode::first_char;

pub struct Widths {
    cache: HashMap<u32, u8>,
}

impl Default for Widths {
    fn default() -> Self {
        Self::new()
    }
}

impl Widths {
    pub fn new() -> Self {
        let mut w = Self {
            cache: HashMap::new(),
        };
        w.rebuild(core::iter::empty::<&[u8]>());
        w
    }

    pub fn rebuild<'a>(&mut self, entries: impl IntoIterator<Item = &'a [u8]>) {
        self.cache.clear();
        self.cache.extend(TABLE.iter().copied());
        for entry in entries {
            self.add(entry);
        }
    }

    pub fn width(&self, c: char) -> u8 {
        if let Some(&w) = self.cache.get(&u32::from(c)) {
            return w;
        }
        match c {
            '\u{80}'..='\u{9f}' => 0,
            _ => c.width().map_or(1, |w| u8::try_from(w.min(2)).unwrap_or(2)),
        }
    }

    fn add(&mut self, entry: &[u8]) {
        let Some(at) = entry.iter().position(|&b| b == b'=') else {
            return;
        };
        let (key, value) = (&entry[..at], &entry[at.wrapping_add(1)..]);
        let Some(width) = parse_width(value) else {
            return;
        };
        if let Some(rest) = key.strip_prefix(b"U+") {
            if let Some((start, end)) = parse_range(rest) {
                for wc in start..=end {
                    self.cache.insert(wc, width);
                }
            }
            return;
        }
        let mut items = scan(key, None);
        let (Some(item), None) = (items.next(), items.next()) else {
            return;
        };
        let c = match item {
            Item::Char(c) => c.first_char(),
            Item::Byte(b) => first_char(&[b]),
        };
        if let Some(c) = c {
            self.cache.insert(u32::from(c), width);
        }
    }
}

const WCHAR_MAX: u64 = 0x7fff_ffff;

fn skip_space(s: &[u8]) -> &[u8] {
    let n = s
        .iter()
        .take_while(|b| matches!(b, b' ' | b'\t'..=b'\r'))
        .count();
    &s[n..]
}

fn parse_width(s: &[u8]) -> Option<u8> {
    let s = core::str::from_utf8(skip_space(s)).ok()?;
    match s.parse::<i64>().ok()? {
        w @ 0..=2 => u8::try_from(w).ok(),
        _ => None,
    }
}

fn parse_hex(s: &[u8]) -> Option<(u64, &[u8])> {
    let s = skip_space(s);
    let s = s.strip_prefix(b"+").unwrap_or(s);
    let body = match s.strip_prefix(b"0x").or_else(|| s.strip_prefix(b"0X")) {
        Some(rest) if rest.first().is_some_and(u8::is_ascii_hexdigit) => rest,
        _ => s,
    };
    let digits = body.iter().take_while(|b| b.is_ascii_hexdigit()).count();
    if digits == 0 {
        return None;
    }
    let n = u64::from_str_radix(core::str::from_utf8(&body[..digits]).ok()?, 16).ok()?;
    Some((n, &body[digits..]))
}

fn parse_range(s: &[u8]) -> Option<(u32, u32)> {
    let (start, rest) = parse_hex(s)?;
    if start == 0 || start > WCHAR_MAX {
        return None;
    }
    let start = u32::try_from(start).ok()?;
    if rest.is_empty() {
        return Some((start, start));
    }
    let (end, rest) = parse_hex(rest.strip_prefix(b"-U+")?)?;
    if !rest.is_empty() || end == 0 || end > WCHAR_MAX {
        return None;
    }
    let end = u32::try_from(end).ok()?;
    (end >= start).then_some((start, end))
}

const TABLE: [(u32, u8); 162] = [
    (0x0261D, 2),
    (0x026F9, 2),
    (0x0270A, 2),
    (0x0270B, 2),
    (0x0270C, 2),
    (0x0270D, 2),
    (0x1F1E6, 1),
    (0x1F1E7, 1),
    (0x1F1E8, 1),
    (0x1F1E9, 1),
    (0x1F1EA, 1),
    (0x1F1EB, 1),
    (0x1F1EC, 1),
    (0x1F1ED, 1),
    (0x1F1EE, 1),
    (0x1F1EF, 1),
    (0x1F1F0, 1),
    (0x1F1F1, 1),
    (0x1F1F2, 1),
    (0x1F1F3, 1),
    (0x1F1F4, 1),
    (0x1F1F5, 1),
    (0x1F1F6, 1),
    (0x1F1F7, 1),
    (0x1F1F8, 1),
    (0x1F1F9, 1),
    (0x1F1FA, 1),
    (0x1F1FB, 1),
    (0x1F1FC, 1),
    (0x1F1FD, 1),
    (0x1F1FE, 1),
    (0x1F1FF, 1),
    (0x1F385, 2),
    (0x1F3C2, 2),
    (0x1F3C3, 2),
    (0x1F3C4, 2),
    (0x1F3C7, 2),
    (0x1F3CA, 2),
    (0x1F3CB, 2),
    (0x1F3CC, 2),
    (0x1F3FB, 2),
    (0x1F3FC, 2),
    (0x1F3FD, 2),
    (0x1F3FE, 2),
    (0x1F3FF, 2),
    (0x1F442, 2),
    (0x1F443, 2),
    (0x1F446, 2),
    (0x1F447, 2),
    (0x1F448, 2),
    (0x1F449, 2),
    (0x1F44A, 2),
    (0x1F44B, 2),
    (0x1F44C, 2),
    (0x1F44D, 2),
    (0x1F44E, 2),
    (0x1F44F, 2),
    (0x1F450, 2),
    (0x1F466, 2),
    (0x1F467, 2),
    (0x1F468, 2),
    (0x1F469, 2),
    (0x1F46B, 2),
    (0x1F46C, 2),
    (0x1F46D, 2),
    (0x1F46E, 2),
    (0x1F470, 2),
    (0x1F471, 2),
    (0x1F472, 2),
    (0x1F473, 2),
    (0x1F474, 2),
    (0x1F475, 2),
    (0x1F476, 2),
    (0x1F477, 2),
    (0x1F478, 2),
    (0x1F47C, 2),
    (0x1F481, 2),
    (0x1F482, 2),
    (0x1F483, 2),
    (0x1F485, 2),
    (0x1F486, 2),
    (0x1F487, 2),
    (0x1F48F, 2),
    (0x1F491, 2),
    (0x1F4AA, 2),
    (0x1F574, 2),
    (0x1F575, 2),
    (0x1F57A, 2),
    (0x1F590, 2),
    (0x1F595, 2),
    (0x1F596, 2),
    (0x1F645, 2),
    (0x1F646, 2),
    (0x1F647, 2),
    (0x1F64B, 2),
    (0x1F64C, 2),
    (0x1F64D, 2),
    (0x1F64E, 2),
    (0x1F64F, 2),
    (0x1F6A3, 2),
    (0x1F6B4, 2),
    (0x1F6B5, 2),
    (0x1F6B6, 2),
    (0x1F6C0, 2),
    (0x1F6CC, 2),
    (0x1F90C, 2),
    (0x1F90F, 2),
    (0x1F918, 2),
    (0x1F919, 2),
    (0x1F91A, 2),
    (0x1F91B, 2),
    (0x1F91C, 2),
    (0x1F91D, 2),
    (0x1F91E, 2),
    (0x1F91F, 2),
    (0x1F926, 2),
    (0x1F930, 2),
    (0x1F931, 2),
    (0x1F932, 2),
    (0x1F933, 2),
    (0x1F934, 2),
    (0x1F935, 2),
    (0x1F936, 2),
    (0x1F937, 2),
    (0x1F938, 2),
    (0x1F939, 2),
    (0x1F93D, 2),
    (0x1F93E, 2),
    (0x1F977, 2),
    (0x1F9B5, 2),
    (0x1F9B6, 2),
    (0x1F9B8, 2),
    (0x1F9B9, 2),
    (0x1F9BB, 2),
    (0x1F9CD, 2),
    (0x1F9CE, 2),
    (0x1F9CF, 2),
    (0x1F9D1, 2),
    (0x1F9D2, 2),
    (0x1F9D3, 2),
    (0x1F9D4, 2),
    (0x1F9D5, 2),
    (0x1F9D6, 2),
    (0x1F9D7, 2),
    (0x1F9D8, 2),
    (0x1F9D9, 2),
    (0x1F9DA, 2),
    (0x1F9DB, 2),
    (0x1F9DC, 2),
    (0x1F9DD, 2),
    (0x1FAC3, 2),
    (0x1FAC4, 2),
    (0x1FAC5, 2),
    (0x1FAF0, 2),
    (0x1FAF1, 2),
    (0x1FAF2, 2),
    (0x1FAF3, 2),
    (0x1FAF4, 2),
    (0x1FAF5, 2),
    (0x1FAF6, 2),
    (0x1FAF7, 2),
    (0x1FAF8, 2),
];

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn table_then_unicode_width_then_one() {
        let w = Widths::new();
        assert_eq!(w.width('\u{1F1E6}'), 1);
        assert_eq!(w.width('\u{261D}'), 2);
        assert_eq!(w.width('a'), 1);
        assert_eq!(w.width('中'), 2);
        assert_eq!(w.width('\u{301}'), 0);
        assert_eq!(w.width('\u{85}'), 0);
        assert_eq!(w.width('\u{1}'), 1);
        assert_eq!(w.width('\u{7f}'), 1);
        assert_eq!(w.width('\u{e000}'), 1);
        assert_eq!(w.width('\u{17d8}'), 2);
    }

    #[test]
    fn option_entries_override_and_parse_like_strtonum() {
        let mut w = Widths::new();
        let entries: [&[u8]; 14] = [
            b"U+1F1E6=2",
            b"U+4E00-U+4E02=1",
            "é=2".as_bytes(),
            b"a= +1",
            b"U+0x41=0",
            b"U+=1",
            b"U+0=1",
            b"U+42=3",
            b"U+50-U+40=1",
            b"U+43x=1",
            b"U+44-U+45x=1",
            b"ab=1",
            b"\xff=1",
            b"nonsense",
        ];
        w.rebuild(entries);
        assert_eq!(w.width('\u{1F1E6}'), 2);
        assert_eq!(w.width('\u{4E01}'), 1);
        assert_eq!(w.width('\u{4E03}'), 2);
        assert_eq!(w.width('é'), 2);
        assert_eq!(w.width('a'), 1);
        assert_eq!(w.width('A'), 0);
        assert_eq!(w.width('B'), 1);
        assert_eq!(w.width('C'), 1);
        assert_eq!(w.width('D'), 1);
        assert_eq!(w.width('b'), 1);
        w.rebuild([]);
        assert_eq!(w.width('\u{1F1E6}'), 1);
        assert_eq!(w.width('A'), 1);
    }
}

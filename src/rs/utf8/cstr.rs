use super::decode::{Decode, Utf8Char};
use super::width::Widths;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Item {
    Char(Utf8Char),
    Byte(u8),
}

pub struct Scan<'a> {
    src: &'a [u8],
    widths: Option<&'a Widths>,
}

pub fn scan<'a>(src: &'a [u8], widths: Option<&'a Widths>) -> Scan<'a> {
    Scan { src, widths }
}

impl Iterator for Scan<'_> {
    type Item = Item;

    fn next(&mut self) -> Option<Item> {
        let (&first, rest) = self.src.split_first()?;
        let mut c = Utf8Char::empty();
        if c.open(first) == Decode::More {
            let mut st = Decode::More;
            let mut taken = 0usize;
            for &b in rest {
                if st != Decode::More {
                    break;
                }
                st = c.append(b);
                taken = taken.wrapping_add(1);
            }
            if st == Decode::Done {
                if let Some(widths) = self.widths {
                    c.set_width(widths);
                }
                self.src = &rest[taken..];
                return Some(Item::Char(c));
            }
        }
        self.src = rest;
        Some(Item::Byte(first))
    }
}

pub fn is_printable(b: u8) -> bool {
    (0x20..0x7f).contains(&b)
}

pub fn is_valid(s: &[u8]) -> bool {
    scan(s, None).all(|item| match item {
        Item::Char(_) => true,
        Item::Byte(b) => is_printable(b),
    })
}

pub fn sanitize(s: &[u8], widths: &Widths) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len());
    for item in scan(s, Some(widths)) {
        match item {
            Item::Char(c) => out.extend(core::iter::repeat_n(b'_', usize::from(c.width))),
            Item::Byte(b) if is_printable(b) => out.push(b),
            Item::Byte(_) => out.push(b'_'),
        }
    }
    out
}

pub fn from_cstr(s: &[u8], widths: Option<&Widths>) -> Vec<Utf8Char> {
    scan(s, widths)
        .map(|item| match item {
            Item::Char(c) => c,
            Item::Byte(b) => Utf8Char::ascii(b),
        })
        .collect()
}

pub fn to_cstr(items: &[Utf8Char]) -> Vec<u8> {
    items
        .iter()
        .flat_map(|c| c.bytes().iter().copied())
        .collect()
}

pub fn str_width(items: &[Utf8Char], n: Option<usize>) -> u32 {
    let taken = n.map_or(items.len(), |n| n.min(items.len()));
    items[..taken]
        .iter()
        .fold(0u32, |w, c| w.wrapping_add(u32::from(c.width)))
}

pub fn cstr_width(s: &[u8], widths: &Widths) -> u32 {
    scan(s, Some(widths)).fold(0u32, |w, item| match item {
        Item::Char(c) => w.wrapping_add(u32::from(c.width)),
        Item::Byte(b) if is_printable(b) => w.wrapping_add(1),
        Item::Byte(_) => w,
    })
}

fn padding(s: &[u8], width: u32, widths: &Widths) -> usize {
    usize::try_from(width.saturating_sub(cstr_width(s, widths))).unwrap_or(0)
}

pub fn pad(s: &[u8], width: u32, widths: &Widths) -> Vec<u8> {
    let n = padding(s, width, widths);
    let mut out = Vec::with_capacity(s.len().saturating_add(n));
    out.extend_from_slice(s);
    out.extend(core::iter::repeat_n(b' ', n));
    out
}

pub fn rpad(s: &[u8], width: u32, widths: &Widths) -> Vec<u8> {
    let n = padding(s, width, widths);
    let mut out = Vec::with_capacity(s.len().saturating_add(n));
    out.extend(core::iter::repeat_n(b' ', n));
    out.extend_from_slice(s);
    out
}

pub fn cstr_has(s: &[u8], c: &Utf8Char, widths: &Widths) -> bool {
    from_cstr(s, Some(widths))
        .iter()
        .any(|item| item.size == c.size && item.bytes() == c.bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn items(s: &[u8]) -> Vec<Item> {
        scan(s, Some(&Widths::new())).collect()
    }

    #[test]
    fn scan_yields_chars_and_raw_bytes() {
        let got = items(b"a\xc3\xa9\xc3x\xff\xe4\xb8");
        assert_eq!(got.len(), 7);
        assert_eq!(got[0], Item::Byte(b'a'));
        let Item::Char(c) = got[1] else { panic!() };
        assert_eq!((c.bytes(), c.width), ("é".as_bytes(), 1));
        assert_eq!(got[2], Item::Byte(0xc3));
        assert_eq!(got[3], Item::Byte(b'x'));
        assert_eq!(got[4], Item::Byte(0xff));
        assert_eq!(got[5], Item::Byte(0xe4));
        assert_eq!(got[6], Item::Byte(0xb8));
        assert_eq!(items(b"").len(), 0);
    }

    #[test]
    fn validity_and_sanitizing() {
        assert!(is_valid(b"hello"));
        assert!(is_valid("café 中".as_bytes()));
        assert!(is_valid(b""));
        assert!(!is_valid(b"a\tb"));
        assert!(!is_valid(b"a\x7f"));
        assert!(!is_valid(b"\xc3"));
        assert!(!is_valid(b"\xc3(x"));
        assert!(!is_valid(b"\xff"));
        let w = Widths::new();
        assert_eq!(sanitize("héllo".as_bytes(), &w), b"h_llo");
        assert_eq!(sanitize("中x".as_bytes(), &w), b"__x");
        assert_eq!(sanitize(b"a\x01\x7fb\xffc", &w), b"a__b_c");
        assert_eq!(sanitize("e\u{301}".as_bytes(), &w), b"e");
        assert_eq!(sanitize(b"", &w), b"");
    }

    #[test]
    fn conversions_and_widths() {
        let w = Widths::new();
        let s = "aé中".as_bytes();
        let ud = from_cstr(s, Some(&w));
        assert_eq!(ud.len(), 3);
        assert_eq!(str_width(&ud, None), 4);
        assert_eq!(str_width(&ud, Some(2)), 2);
        assert_eq!(str_width(&ud, Some(9)), 4);
        assert_eq!(to_cstr(&ud), s);
        assert_eq!(cstr_width(s, &w), 4);
        assert_eq!(cstr_width(b"", &w), 0);
        assert_eq!(cstr_width(b"\xc3x", &w), 1);
        assert_eq!(cstr_width(b"\x01\x7f", &w), 0);
        let raw = from_cstr(b"\xff", Some(&w));
        assert_eq!((raw[0].size, raw[0].width, raw[0].data[0]), (1, 1, 0xff));
    }

    #[test]
    fn padding_by_display_width() {
        let w = Widths::new();
        assert_eq!(pad("中".as_bytes(), 5, &w), "中   ".as_bytes());
        assert_eq!(rpad(b"ab", 4, &w), b"  ab");
        assert_eq!(pad(b"toolong", 3, &w), b"toolong");
        assert_eq!(rpad(b"toolong", 7, &w), b"toolong");
    }

    #[test]
    fn has_finds_a_character() {
        let w = Widths::new();
        let c = Utf8Char::from_char('中', &w).unwrap();
        assert!(cstr_has("a中b".as_bytes(), &c, &w));
        assert!(!cstr_has(b"ab", &c, &w));
        assert!(cstr_has(b"ab", &Utf8Char::ascii(b'b'), &w));
        assert!(cstr_has(b"a\xffb", &Utf8Char::ascii(0xff), &w));
    }
}

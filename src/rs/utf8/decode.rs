use super::MAX_BYTES;
use super::width::Widths;

pub const INVALID_WIDTH: u8 = 0xff;

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Utf8Char {
    pub data: [u8; MAX_BYTES],
    pub have: u8,
    pub size: u8,
    pub width: u8,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Decode {
    More,
    Done,
    Error,
}

pub fn sequence_len(lead: u8) -> Option<u8> {
    match lead {
        0xc2..=0xdf => Some(2),
        0xe0..=0xef => Some(3),
        0xf0..=0xf4 => Some(4),
        _ => None,
    }
}

pub fn first_char(bytes: &[u8]) -> Option<char> {
    let lead = *bytes.first()?;
    let n = if lead < 0x80 {
        1
    } else {
        usize::from(sequence_len(lead)?)
    };
    let c = core::str::from_utf8(bytes.get(..n)?).ok()?.chars().next()?;
    (c != '\0').then_some(c)
}

impl Default for Utf8Char {
    fn default() -> Self {
        Self::empty()
    }
}

impl Utf8Char {
    pub const fn empty() -> Self {
        Self {
            data: [0; MAX_BYTES],
            have: 0,
            size: 0,
            width: 0,
        }
    }

    pub fn ascii(ch: u8) -> Self {
        let mut c = Self::empty();
        c.data[0] = ch;
        c.have = 1;
        c.size = 1;
        c.width = 1;
        c
    }

    pub fn bytes(&self) -> &[u8] {
        &self.data[..usize::from(self.size).min(MAX_BYTES)]
    }

    pub fn copy_from(&mut self, from: &Self) {
        *self = *from;
        let n = usize::from(self.size).min(MAX_BYTES);
        self.data[n..].fill(0);
    }

    pub fn open(&mut self, ch: u8) -> Decode {
        *self = Self::empty();
        match sequence_len(ch) {
            Some(n) => {
                self.size = n;
                self.data[0] = ch;
                self.have = 1;
                Decode::More
            }
            None => Decode::Error,
        }
    }

    pub fn append(&mut self, ch: u8) -> Decode {
        if self.have >= self.size || usize::from(self.size) > MAX_BYTES {
            return Decode::Error;
        }
        if self.have != 0 && ch & 0xc0 != 0x80 {
            self.width = INVALID_WIDTH;
        }
        self.data[usize::from(self.have)] = ch;
        self.have = self.have.saturating_add(1);
        if self.have != self.size {
            return Decode::More;
        }
        if self.width == INVALID_WIDTH || self.first_char().is_none() {
            return Decode::Error;
        }
        Decode::Done
    }

    pub fn set_width(&mut self, widths: &Widths) {
        if let Some(c) = self.first_char() {
            self.width = widths.width(c);
        }
    }

    pub fn first_char(&self) -> Option<char> {
        first_char(self.bytes())
    }

    pub fn from_char(c: char, widths: &Widths) -> Option<Self> {
        let mut out = Self::empty();
        let n = c.encode_utf8(&mut out.data).len();
        out.size = u8::try_from(n).ok()?;
        out.have = out.size;
        out.width = widths.width(out.first_char()?);
        Some(out)
    }
}

#[cfg(test)]
pub fn decode(s: &[u8], widths: &Widths) -> (Utf8Char, Decode) {
    let mut c = Utf8Char::empty();
    let mut st = c.open(s[0]);
    for &b in &s[1..] {
        if st != Decode::More {
            break;
        }
        st = c.append(b);
    }
    if st == Decode::Done {
        c.set_width(widths);
    }
    (c, st)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lead_bytes_classify() {
        let mut c = Utf8Char::empty();
        assert_eq!(c.open(b'a'), Decode::Error);
        assert_eq!(c.open(0x80), Decode::Error);
        assert_eq!(c.open(0xc1), Decode::Error);
        assert_eq!(c.open(0xc2), Decode::More);
        assert_eq!((c.size, c.have, c.data[0]), (2, 1, 0xc2));
        assert_eq!(c.open(0xe0), Decode::More);
        assert_eq!(c.size, 3);
        assert_eq!(c.open(0xf4), Decode::More);
        assert_eq!(c.size, 4);
        assert_eq!(c.open(0xf5), Decode::Error);
        assert_eq!(c.open(0xff), Decode::Error);
    }

    #[test]
    fn sequences_decode_with_widths() {
        let w = Widths::new();
        let (c, st) = decode("é".as_bytes(), &w);
        assert_eq!(st, Decode::Done);
        assert_eq!((c.size, c.have, c.width), (2, 2, 1));
        let (c, st) = decode("中".as_bytes(), &w);
        assert_eq!((st, c.width), (Decode::Done, 2));
        let (c, st) = decode("😀".as_bytes(), &w);
        assert_eq!((st, c.size, c.width), (Decode::Done, 4, 2));
        let (c, st) = decode("\u{301}".as_bytes(), &w);
        assert_eq!((st, c.width), (Decode::Done, 0));
        let (c, st) = decode("\u{85}".as_bytes(), &w);
        assert_eq!((st, c.width), (Decode::Done, 0));
    }

    #[test]
    fn bad_continuation_overlongs_and_truncation() {
        let w = Widths::new();
        let (c, st) = decode(&[0xc3, b'a'], &w);
        assert_eq!((st, c.width), (Decode::Error, INVALID_WIDTH));
        assert_eq!(decode(&[0xe4, 0xb8, 0x41], &w).1, Decode::Error);
        assert_eq!(decode(&[0xe0, 0x80, 0x80], &w).1, Decode::Error);
        assert_eq!(decode(&[0xf0, 0x80, 0x80, 0x80], &w).1, Decode::Error);
        assert_eq!(decode(&[0xed, 0xa0, 0x80], &w).1, Decode::Error);
        assert_eq!(decode(&[0xf4, 0x90, 0x80, 0x80], &w).1, Decode::Error);
        let (c, st) = decode(&[0xe4, 0xb8], &w);
        assert_eq!((st, c.have, c.size), (Decode::More, 2, 3));
    }

    #[test]
    fn append_validates_and_leaves_the_width_alone() {
        let mut c = Utf8Char::empty();
        c.open(0xc3);
        assert_eq!(c.append(b'a'), Decode::Error);
        c.open(0xe0);
        c.append(0x80);
        assert_eq!(c.append(0x80), Decode::Error);
        c.open(0xc3);
        assert_eq!((c.append(0xa9), c.width), (Decode::Done, 0));
        c.set_width(&Widths::new());
        assert_eq!(c.width, 1);
    }

    #[test]
    fn overflow_is_an_error_not_an_abort() {
        let mut c = Utf8Char::ascii(b'a');
        assert_eq!(c.append(b'b'), Decode::Error);
        c.size = 40;
        c.have = 0;
        assert_eq!(c.append(b'b'), Decode::Error);
    }

    #[test]
    fn first_char_reads_the_lead_sequence_only() {
        let w = Widths::new();
        let mut c = Utf8Char::from_char('👩', &w).unwrap();
        c.data[4..7].copy_from_slice("\u{200d}".as_bytes());
        c.size = 7;
        c.have = 7;
        assert_eq!(c.first_char(), Some('👩'));
        assert_eq!(first_char(&[0]), None);
        assert_eq!(first_char(&[0xc3]), None);
        assert_eq!(first_char(&[0x80]), None);
        assert_eq!(first_char(b"a"), Some('a'));
    }

    #[test]
    fn from_char_and_copy() {
        let w = Widths::new();
        assert!(Utf8Char::from_char('\0', &w).is_none());
        let c = Utf8Char::from_char('中', &w).unwrap();
        assert_eq!((c.bytes(), c.width), ("中".as_bytes(), 2));
        let mut dirty = Utf8Char {
            data: [0xaa; MAX_BYTES],
            have: 0,
            size: 0,
            width: 0,
        };
        dirty.copy_from(&c);
        assert_eq!(dirty, c);
        assert!(dirty.data[3..].iter().all(|&b| b == 0));
    }
}

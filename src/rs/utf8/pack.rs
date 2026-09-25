use std::collections::HashMap;

use super::MAX_BYTES;
use super::decode::Utf8Char;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Packed(pub u32);

const INDEX_MASK: u32 = 0x00ff_ffff;

impl Packed {
    pub fn parts(size: u8, width: u8, index: u32) -> Self {
        let size = u32::from(size & 0x1f) << 24;
        let width = u32::from(width).saturating_add(1) << 29;
        Self(size | width | (index & INDEX_MASK))
    }

    pub fn size(self) -> u8 {
        u8::try_from((self.0 >> 24) & 0x1f).unwrap_or(0)
    }

    pub fn width(self) -> u8 {
        u8::try_from((self.0 >> 29).wrapping_sub(1)).unwrap_or(0xff)
    }

    pub fn index(self) -> u32 {
        self.0 & INDEX_MASK
    }

    pub fn ascii(ch: u8) -> Self {
        Self::parts(1, 1, u32::from(ch))
    }
}

type Key = ([u8; MAX_BYTES], u8);

fn key(c: &Utf8Char) -> Key {
    let mut data = [0; MAX_BYTES];
    data[..c.bytes().len()].copy_from_slice(c.bytes());
    (data, c.size)
}

#[derive(Default)]
pub struct Intern {
    items: Vec<[u8; MAX_BYTES]>,
    by_key: HashMap<Key, u32>,
}

impl Intern {
    pub fn put(&mut self, c: &Utf8Char) -> Option<u32> {
        let key = key(c);
        if let Some(&i) = self.by_key.get(&key) {
            return Some(i);
        }
        let index = u32::try_from(self.items.len()).ok()?;
        if index > INDEX_MASK {
            return None;
        }
        self.items.push(key.0);
        self.by_key.insert(key, index);
        Some(index)
    }

    pub fn get(&self, index: u32) -> Option<&[u8; MAX_BYTES]> {
        self.items.get(usize::try_from(index).ok()?)
    }
}

pub fn pack(c: &Utf8Char, put: impl FnOnce(&Utf8Char) -> Option<u32>) -> Result<Packed, Packed> {
    let replacement = || match c.width {
        0 => Packed::parts(0, 0, 0),
        1 => Packed::parts(1, 1, 0x20),
        _ => Packed::parts(1, 1, 0x2020),
    };
    if c.width > 2 || usize::from(c.size) > MAX_BYTES {
        return Err(replacement());
    }
    let index = if c.size <= 3 {
        (u32::from(c.data[2]) << 16) | (u32::from(c.data[1]) << 8) | u32::from(c.data[0])
    } else {
        match put(c) {
            Some(i) => i,
            None => return Err(replacement()),
        }
    };
    Ok(Packed::parts(c.size, c.width, index))
}

pub fn unpack(p: Packed, get: impl FnOnce(u32) -> Option<[u8; MAX_BYTES]>) -> Utf8Char {
    let mut c = Utf8Char::empty();
    c.size = p.size();
    c.have = c.size;
    c.width = p.width();
    let n = usize::from(c.size);
    if c.size <= 3 {
        let index = p.index();
        c.data[2] = u8::try_from(index >> 16).unwrap_or(0);
        c.data[1] = u8::try_from((index >> 8) & 0xff).unwrap_or(0);
        c.data[0] = u8::try_from(index & 0xff).unwrap_or(0);
    } else if let Some(stored) = get(p.index()) {
        c.data[..n].copy_from_slice(&stored[..n]);
    } else {
        c.data[..n].fill(b' ');
    }
    c
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::utf8::width::Widths;

    fn ch(s: &str) -> Utf8Char {
        Utf8Char::from_char(s.chars().next().unwrap(), &Widths::new()).unwrap()
    }

    fn roundtrip(c: &Utf8Char, intern: &mut Intern) -> (Packed, Utf8Char) {
        let p = pack(c, |c| intern.put(c)).unwrap();
        (p, unpack(p, |i| intern.get(i).copied()))
    }

    #[test]
    fn packs_every_size() {
        let mut intern = Intern::default();
        for s in ["a", "é", "中", "😀"] {
            let c = ch(s);
            let (p, back) = roundtrip(&c, &mut intern);
            assert_eq!((p.size(), p.width()), (c.size, c.width));
            assert_eq!(
                (back.bytes(), back.have, back.width),
                (c.bytes(), c.size, c.width)
            );
        }
        let one = unpack(Packed::ascii(b'z'), |_| None);
        assert_eq!((one.size, one.width, one.data[0]), (1, 1, b'z'));
        assert_eq!(
            Packed::ascii(b'z').0,
            (1 << 24) | (2 << 29) | u32::from(b'z')
        );
    }

    #[test]
    fn interning_is_stable_and_unknown_indexes_read_as_spaces() {
        let mut intern = Intern::default();
        let (a, _) = roundtrip(&ch("😀"), &mut intern);
        let (b, _) = roundtrip(&ch("😀"), &mut intern);
        let (c, _) = roundtrip(&ch("😁"), &mut intern);
        assert_eq!(a, b);
        assert_eq!((a.index(), c.index()), (0, 1));
        let missing = unpack(Packed::parts(4, 2, 77), |i| intern.get(i).copied());
        assert_eq!(missing.bytes(), b"    ");
        assert_eq!(unpack(Packed(0), |_| None).width, 0xff);
    }

    #[test]
    fn bad_widths_and_sizes_become_replacements() {
        let mut c = ch("中");
        c.width = 3;
        assert_eq!(pack(&c, |_| None).unwrap_err().index(), 0x2020);
        c.width = 1;
        c.size = 40;
        assert_eq!(pack(&c, |_| None).unwrap_err().index(), 0x20);
        c.width = 0;
        assert_eq!(pack(&c, |_| None).unwrap_err().0, 1 << 29);
        c.size = 4;
        c.width = 2;
        assert_eq!(pack(&c, |_| None).unwrap_err().index(), 0x2020);
    }
}

use super::decode::Utf8Char;

const ZWJ: [u8; 3] = [0xe2, 0x80, 0x8d];

pub fn has_zwj(c: &Utf8Char) -> bool {
    c.bytes().ends_with(&ZWJ)
}

pub fn is_zwj(c: &Utf8Char) -> bool {
    c.bytes() == ZWJ
}

pub fn is_vs(c: &Utf8Char) -> bool {
    c.bytes() == [0xef, 0xb8, 0x8f]
}

pub fn is_hangul_filler(c: &Utf8Char) -> bool {
    c.bytes() == [0xe3, 0x85, 0xa4]
}

fn regional_count(c: &Utf8Char) -> usize {
    c.bytes()
        .windows(4)
        .filter(|w| w[0] == 0xf0 && w[1] == 0x9f && w[2] == 0x87 && (0xa6..=0xbf).contains(&w[3]))
        .count()
}

fn takes_skin_tone(a: u32) -> bool {
    matches!(
        a,
        0x1F44B..=0x1F450
            | 0x1F466..=0x1F469
            | 0x1F46E
            | 0x1F470..=0x1F478
            | 0x1F47C
            | 0x1F481..=0x1F483
            | 0x1F485..=0x1F487
            | 0x1F4AA
            | 0x1F575
            | 0x1F57A
            | 0x1F590
            | 0x1F595
            | 0x1F596
            | 0x1F645..=0x1F647
            | 0x1F64B..=0x1F64F
            | 0x1F6B4..=0x1F6B6
            | 0x1F926
            | 0x1F937..=0x1F939
            | 0x1F93D
            | 0x1F93E
            | 0x1F9B5
            | 0x1F9B6
            | 0x1F9B8
            | 0x1F9B9
            | 0x1F9CD..=0x1F9CF
            | 0x1F9D1..=0x1F9DF
    )
}

pub fn should_combine(with: &Utf8Char, add: &Utf8Char) -> bool {
    let (Some(w), Some(a)) = (with.first_char(), add.first_char()) else {
        return false;
    };
    let (w, a) = (u32::from(w), u32::from(a));
    if (0x1F1E6..=0x1F1FF).contains(&a) && (0x1F1E6..=0x1F1FF).contains(&w) {
        return regional_count(with) == 1 && regional_count(add) == 1;
    }
    takes_skin_tone(a) && (0x1F3FB..=0x1F3FF).contains(&w)
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Jamo {
    Choseong,
    Jungseong,
    Jongseong,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum HangulState {
    NotHanguljamo,
    Choseong,
    Composable,
    NotComposable,
}

pub fn jamo_class(s: &[u8]) -> Option<Jamo> {
    match (*s.first()?, *s.get(1)?, *s.get(2)?) {
        (0xE1, 0x84, 0x80..=0xBF) | (0xE1, 0x85, 0x80..=0x9F) | (0xEA, 0xA5, 0xA0..=0xBC) => {
            Some(Jamo::Choseong)
        }
        (0xE1, 0x85, 0xA0..=0xBF)
        | (0xE1, 0x86, 0x80..=0xA7)
        | (0xED, 0x9E, 0xB0..=0xBF)
        | (0xED, 0x9F, 0x80..=0x86) => Some(Jamo::Jungseong),
        (0xE1, 0x86, 0xA8..=0xBF) | (0xE1, 0x87, 0x80..=0xBF) | (0xED, 0x9F, 0x8B..=0xBB) => {
            Some(Jamo::Jongseong)
        }
        _ => None,
    }
}

pub fn hangul_state(prev: &Utf8Char, c: &Utf8Char) -> HangulState {
    if c.size != 3 {
        return HangulState::NotHanguljamo;
    }
    let needs = match jamo_class(c.bytes()) {
        None => return HangulState::NotHanguljamo,
        Some(Jamo::Choseong) => return HangulState::Choseong,
        Some(Jamo::Jungseong) => Jamo::Choseong,
        Some(Jamo::Jongseong) => Jamo::Jungseong,
    };
    let prev = prev.bytes();
    let Some(at) = prev.len().checked_sub(3) else {
        return HangulState::NotComposable;
    };
    if jamo_class(&prev[at..]) == Some(needs) {
        HangulState::Composable
    } else {
        HangulState::NotComposable
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::utf8::width::Widths;

    fn ch(s: &str) -> Utf8Char {
        Utf8Char::from_char(s.chars().next().unwrap(), &Widths::new()).unwrap()
    }

    fn joined(parts: &[&str]) -> Utf8Char {
        let mut c = Utf8Char::empty();
        let mut n = 0;
        for p in parts {
            c.data[n..n + p.len()].copy_from_slice(p.as_bytes());
            n += p.len();
        }
        c.size = u8::try_from(n).unwrap();
        c.have = c.size;
        c.width = 2;
        c
    }

    #[test]
    fn markers() {
        assert!(is_zwj(&ch("\u{200d}")));
        assert!(has_zwj(&ch("\u{200d}")));
        assert!(!is_zwj(&ch("\u{fe0f}")));
        assert!(is_vs(&ch("\u{fe0f}")));
        assert!(!is_vs(&ch("\u{200d}")));
        assert!(is_hangul_filler(&ch("\u{3164}")));
        assert!(!is_hangul_filler(&ch("a")));
        assert!(has_zwj(&joined(&["👩", "\u{200d}"])));
        assert!(!has_zwj(&ch("👩")));
        assert!(!has_zwj(&ch("a")));
    }

    #[test]
    fn regional_and_skin_tones() {
        assert!(should_combine(&ch("🇦"), &ch("🇷")));
        assert!(!should_combine(&joined(&["🇦", "🇷"]), &ch("🇬")));
        assert!(!should_combine(&ch("a"), &ch("🇷")));
        assert!(!should_combine(&ch("🇦"), &ch("a")));
        assert!(should_combine(&ch("\u{1F3FB}"), &ch("👋")));
        assert!(should_combine(&ch("\u{1F3FF}"), &ch("\u{1F9DF}")));
        assert!(!should_combine(&ch("a"), &ch("👋")));
        assert!(!should_combine(&ch("\u{1F3FB}"), &ch("中")));
    }

    #[test]
    fn hangul_jamo() {
        let l = ch("\u{1100}");
        let v = ch("\u{1161}");
        let t = ch("\u{11A8}");
        assert_eq!(hangul_state(&ch("a"), &ch("b")), HangulState::NotHanguljamo);
        assert_eq!(hangul_state(&ch("a"), &l), HangulState::Choseong);
        assert_eq!(
            hangul_state(&ch("a"), &ch("\u{115F}")),
            HangulState::Choseong
        );
        assert_eq!(
            hangul_state(&ch("a"), &ch("\u{A960}")),
            HangulState::Choseong
        );
        assert_eq!(hangul_state(&l, &v), HangulState::Composable);
        assert_eq!(hangul_state(&l, &ch("\u{1160}")), HangulState::Composable);
        assert_eq!(hangul_state(&l, &ch("\u{D7B0}")), HangulState::Composable);
        assert_eq!(hangul_state(&ch("a"), &v), HangulState::NotComposable);
        assert_eq!(
            hangul_state(&joined(&["\u{1100}", "\u{1161}"]), &t),
            HangulState::Composable
        );
        assert_eq!(hangul_state(&v, &ch("\u{D7CB}")), HangulState::Composable);
        assert_eq!(hangul_state(&l, &t), HangulState::NotComposable);
        assert_eq!(hangul_state(&l, &ch("中")), HangulState::NotHanguljamo);
        assert_eq!(hangul_state(&l, &ch("😀")), HangulState::NotHanguljamo);
    }
}

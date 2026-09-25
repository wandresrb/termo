use super::cstr::{Item, scan};

pub fn strvis(
    src: &[u8],
    dollar: bool,
    out: &mut Vec<u8>,
    mut escape: impl FnMut(u8, u8, &mut Vec<u8>),
) {
    let mut rest = src;
    for item in scan(src, None) {
        match item {
            Item::Char(c) => {
                out.extend_from_slice(c.bytes());
                rest = &rest[c.bytes().len()..];
            }
            Item::Byte(b) => {
                let next = rest.get(1).copied();
                rest = &rest[1..];
                match next {
                    Some(n) if dollar && b == b'$' => {
                        if n.is_ascii_alphabetic() || n == b'_' || n == b'{' {
                            out.push(b'\\');
                        }
                        out.push(b'$');
                    }
                    Some(n) => escape(b, n, out),
                    None => escape(b, 0, out),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn octal(c: u8, _next: u8, out: &mut Vec<u8>) {
        if (0x20..0x7f).contains(&c) {
            out.push(c);
        } else {
            out.extend_from_slice(format!("\\{c:03o}").as_bytes());
        }
    }

    fn run(src: &[u8], dollar: bool) -> Vec<u8> {
        let mut out = Vec::new();
        strvis(src, dollar, &mut out, octal);
        out
    }

    #[test]
    fn passes_utf8_and_escapes_the_rest() {
        assert_eq!(run(b"\xc3\xa9\x1bx", false), b"\xc3\xa9\\033x");
        assert_eq!(run(b"\xc3a", false), b"\\303a");
        assert_eq!(run(b"\xe4\xb8", false), b"\\344\\270");
        assert_eq!(run(b"", false), b"");
    }

    #[test]
    fn dollar_is_escaped_before_a_name_only() {
        assert_eq!(run(b"$x $1 ${", true), b"\\$x $1 \\${");
        assert_eq!(run(b"$", true), b"$");
        assert_eq!(run(b"$_", false), b"$_");
    }
}

pub const MAX_BYTES: usize = 32;

#[cfg(test)]
mod tests {
    #[test]
    fn max_bytes_matches_the_c_cell() {
        assert_eq!(super::MAX_BYTES, 32);
    }
}

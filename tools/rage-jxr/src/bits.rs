//! MSB-first bit reader. Reading past the end yields zero bits; callers check `overrun`.

pub struct Bits<'a> {
    data: &'a [u8],
    pub pos: usize,
}

impl<'a> Bits<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Bits { data, pos: 0 }
    }

    pub fn len_bits(&self) -> usize {
        self.data.len() * 8
    }

    /// True once the reader has gone more than 64 bits past the end, which only a misparse does.
    pub fn overrun(&self) -> bool {
        self.pos > self.len_bits() + 64
    }

    pub fn bit(&mut self) -> u32 {
        let byte = self.data.get(self.pos >> 3).copied().unwrap_or(0);
        self.pos += 1;
        ((byte >> (7 - ((self.pos - 1) & 7))) & 1) as u32
    }

    pub fn get(&mut self, n: u32) -> u32 {
        let mut v = 0u32;
        for _ in 0..n {
            v = (v << 1) | self.bit();
        }
        v
    }

    pub fn peek(&mut self, n: u32) -> u32 {
        let pos = self.pos;
        let v = self.get(n);
        self.pos = pos;
        v
    }

    /// Byte at the current position (must be byte aligned).
    pub fn byte_at(&self, bit_pos: usize) -> Option<u8> {
        self.data.get(bit_pos >> 3).copied()
    }
}

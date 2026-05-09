# Variable-length bit stream — `PackedBits8`

Companion to `boofcv_qr/packed_bits.hpp`. Step 3 (a) of the porting plan — the bit-level I/O primitive every QR codec layer above this depends on.

## Summary

`PackedBits8` is a growable, bit-addressable buffer backed by `std::vector<uint8_t>`. Bits are stored little-endian within each byte: bit 0 of byte 0 is logical bit 0 of the stream. Capacity grows in chunks (1024-byte buffer) to avoid quadratic re-allocation when QR encoders/decoders stream bits in. Random access (`get` / `set`), bulk read (`read`), and bulk append (`append` in four overloads) cover everything the QR pipeline does.

The Java original separates the concept into a `PackedBits` interface and three concrete classes (`PackedBits8`, `PackedBits32`, plus internal variants). For QR only `PackedBits8` is used; `PackedBits32` and the abstract interface are intentionally not ported.

## Algorithm description

### Storage layout

```
data[0]  : bits  0..7   (bit 0 in LSB)
data[1]  : bits  8..15
data[2]  : bits 16..23
...
```

`get(which)` translates `which` to `(byte_index, bit_offset_in_byte)` then masks. `set(which, value)` does the same with an XOR-based bit assignment that avoids a branch:

```
data[i] ^= (-value ^ data[i]) & (1 << offset)
```

`-value` is `0` for `value == 0` and `-1` (all bits set) for `value == 1`, so the XOR-and-mask resolves to "clear bit if value=0, set bit if value=1." This is BoofCV's idiom; we keep it verbatim.

### Bit-order conventions in `append` / `read`

Both `append(int bits, int numberOfBits, bool swapOrder)` and `read(int location, int length, bool swapOrder)` take a `swapOrder` flag because QR streams operate in two natural directions:

- **`swapOrder = true`** — input/output bit 0 maps to the low bit of the source/destination integer. Consumed by the codec utilities for length fields and codeword payload.
- **`swapOrder = false`** — input/output bit 0 maps to the high bit. Used by the encoder side for "natural" bit order matching ISO 18004's diagrams.

Both orders are needed because QR's spec is somewhat inconsistent about which end of an integer comes first; preserving both options means the codec layer can match the spec reading direction byte-for-byte. Don't unify these — the `false` path is the one used during encoding to match the spec's pictorial bit-laying.

### Capacity growth

```
growArray(amountBits, saveValue):
    size += amountBits
    N = ceil(size / 8)
    if N > data.size():
        new_capacity = N + min(1024, N + 10)   // chunk-amortised growth
        reallocate, copy if saveValue, drop otherwise
```

The "+min(1024, N+10)" buffer avoids quadratic growth when the QR encoder appends ~3000 bits one at a time (high-version QR with byte mode). For the decoder we usually `wrap()` an existing buffer instead, sidestepping growth altogether.

## Why this approach

- **`uint8_t` storage rather than `int32_t`** matches BoofCV's QR pipeline byte alignment (rawbits and corrected codewords are byte arrays). `PackedBits32` would force shuffling on every byte boundary; we don't have that win to chase.
- **Byte-level append for the bulk byte-array path** avoids a per-bit `set` loop: when appending a full byte, we hand it to the integer overload of `append` which writes 8 bits at once. Saves cycles in the encoder hot path.
- **No virtual interface.** Every consumer in the QR pipeline knows it has a `PackedBits8`. The Java `PackedBits` interface exists because Java needs a common type to compare U8 vs U32; in C++ we just don't need it.
- **No zero-copy `wrap`.** The Java `wrap(byte[], int)` aliases the source array; our C++ `wrap` copies it. Aliasing is brittle when the source is a `std::vector` (reallocation invalidates pointers); the QR decoder wraps small fixed buffers (`qr.corrected`) so the copy cost is irrelevant. If a future profile-driven optimisation needs zero-copy we can add a `wrap_view` overload that takes a `(const uint8_t*, size_t)` pair.

## Failure modes and known limits

- **`read(location, length > 32)`** throws `std::invalid_argument`. The single-int return type makes 32 the hard cap.
- **`read` past the end** throws with a diagnostic. Use `length() - location` to gate.
- **`append(int, numberOfBits > 32, ...)`** throws.
- **`set(which, value)` with `value` outside `{0, 1}`** writes garbage. The XOR trick assumes `-value` is `0` or `-1`. Don't pass `value = 2`.
- **`get` on bits beyond `size`** silently reads from uninitialised storage in the buffer past `size` — not undefined in the C++ sense (the bytes are zero-initialised on construction) but conceptually invalid. Guard at the caller.
- **Reentrancy / thread-safety**: none. Each `PackedBits8` is owned by a single decoder; the public mutable `data` / `size` makes that explicit.

## Tunable parameters

- **`size` reservation in the constructor** sets the initial buffer capacity. Pass an upper bound when known (the QR decoder knows the message size from version info) to skip the realloc path entirely.
- **The `min(1024, N + 10)` growth buffer** — increase if encoding very large messages and profiling shows reallocation overhead; the QR decode path doesn't hit this.

## Integration points for downstream recovery

`PackedBits8` is the format the codec utilities accept, so any custom recovery pipeline (known-prefix RS, multi-frame fusion) writes corrected codewords into a `PackedBits8` and feeds them to `decodeNumeric` / `decodeAlphanumeric` / `decodeByte` / `decodeKanji` directly. There's no abstraction layer to inject — `PackedBits8` IS the contract.

If a consumer wants to splice multiple per-frame corrected buffers, they construct a `PackedBits8` with `wrap(merged_bytes, total_bits)` and pass the resulting object to the codec.

## Cross-references

- **Upstream BoofCV files** (pinned `v1.3.0`):
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/PackedBits.java` (interface; not ported)
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/PackedBits8.java`
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/PackedBits32.java` (not ported — unused by QR)
- **Tests:** `tests/unit/test_packed_bits.cpp` (mirrors `TestPackedBits8.java`, all 6 cases).
- **Spec:** ISO/IEC 18004:2015 §6.4 (data encoding) for the bit-laying conventions.
- **Feeds into:** `QrCodeCodecBitsUtils` (mode-aware decoders/encoders).

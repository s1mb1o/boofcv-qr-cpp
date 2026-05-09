# Galois field arithmetic — `GaliosFieldOps`, `GaliosFieldTableOps`

Companion to `galois.cpp` / `boofcv_qr/galois.hpp`. Step 1 of the porting plan.

## Summary

This stage provides arithmetic over Galois fields of characteristic two — `GF(2)[x]` and `GF(2^k)` — used by every downstream stage of QR decoding. `GaliosFieldOps` is a static utility on raw `GF(2)` polynomials packed into `int32_t` (LSB-first). `GaliosFieldTableOps` builds precomputed `exp` and `log` tables for a specific `GF(2^numBits)` mod a primitive polynomial, turning multiply/divide/power/inverse into table lookups. For QR specifically the relevant field is `GF(2^8)` reduced by `0b100011101` (decimal 285, the QR-spec primitive).

## Algorithm description

### `GF(2)` polynomials packed into integers

Each `int32_t` is a polynomial whose coefficient at `x^i` is bit `i`. `0b1011` = `x^3 + x + 1`. Addition and subtraction collapse to bitwise XOR because `1 + 1 = 0` in `GF(2)`.

### `GaliosFieldOps::multiply(a, b)`

Schoolbook polynomial multiplication. For each set bit `i` of `b`, XOR `a << i` into the accumulator. No reduction — the result can have a higher bit-length than either input. Used to populate the exp table; not appropriate for direct field arithmetic.

```
for i in 0.. while (b >> i) > 0:
    if bit i of b is set:
        z ^= a << i
return z
```

### `GaliosFieldOps::multiply(x, y, primitive, domain)` — Russian-Peasant with reduction

Same multiplication, but reduces the running result whenever it grows past `domain` (= `2^numBits`) by XORing in the primitive polynomial. Stays within the field. This is what `GaliosFieldTableOps` calls in its constructor to walk through `α^0, α^1, α^2, …` (with `α = 2`) and fill `exp[]`.

```
r = 0
while y > 0:
    if (y & 1): r ^= x
    y >>= 1
    x <<= 1
    if x >= domain: x ^= primitive   // reduce back into the field
return r
```

### `GaliosFieldOps::modulus(dividend, divisor)` and `divide(...)`

Bit-aligned long division. Find the bit length of both, align the divisor's MSB with the dividend's MSB, XOR-subtract, shift right by one, repeat. `modulus` returns the remainder, `divide` returns the quotient. Used once at construction time and in `multiplyField` parity tests.

### `GaliosFieldOps::length(value)`

1-based MSB position. `length(0) = 0`, `length(1) = 1`, `length(0b100) = 3`. A loop, not `__builtin_clz` — preserved verbatim.

### `GaliosFieldTableOps` — exp/log tables

Constructor (line-for-line):

```
exp[0..max_value-1] : exp[i] = α^i   (with α = 2, multiplied via the Russian-Peasant variant)
log[exp[i]] = i                       (inverse mapping)
exp[max_value..2*max_value-1] = exp[0..max_value-1]   (doubled to skip a modulus on lookups)
```

With these tables in hand:

```
multiply(x,y) = exp[log[x] + log[y]]                  (x*y == α^(log x + log y))
divide(x,y)   = exp[log[x] + max_value - log[y]]      (avoids negative indices via the doubled exp table)
power(x,k)    = exp[(log[x] * k) mod max_value]
inverse(x)    = exp[max_value - log[x]]
```

`power_n` is the variant that wraps when `(log[x] * power) mod max_value` goes negative (which happens when `power` is negative and `log[x]` is non-zero). Used by Reed-Solomon for negative exponents during error evaluation.

## Why this approach

- **XOR-as-add, integer-packed polynomials.** Standard textbook representation for `GF(2)[x]`. Compact, branchless on the inner kernel, and lets compilers vectorise downstream byte-array routines if needed. The alternative — storing each coefficient as a separate byte — is larger and slower for these short polynomials.

- **Russian-Peasant multiplication with inline reduction.** Constant-time per bit (no table for the `multiply` that builds the table). For a one-shot table fill on a 256-element field this is irrelevant performance-wise; correctness and clarity dominate. BoofCV's comment cites the [Reed-Solomon Codes for Coders](https://en.wikiversity.org/wiki/Reed%E2%80%93Solomon_codes_for_coders) Wikiversity tutorial as the reference.

- **exp/log table doubled to avoid modulus.** `multiply` does `exp[log[x]+log[y]]` where the index can hit `2*max_value - 2`. Storing two copies of `exp` skips the `% max_value` in the hot path. Trades 1 KB extra (for `GF(2^8)`) for one less integer divmod per multiply.

- **Why not `clmul` / PCLMULQDQ / `__builtin_clz`?** Tempting but premature. The hot path during decode is `GaliosFieldTableOps::multiply` which is already a 1-cycle table lookup; the unreduced `GaliosFieldOps::multiply` only runs at table-init time. Hardware-assisted polynomial multiply (Intel's `clmul`, ARM's `pmull`) becomes worthwhile for cryptographic GHASH-style throughput, not for a 256×256 lookup table built once at construction.

## Failure modes and known limits

- **`numBits` outside `[1, 16]`** throws `std::invalid_argument`. The table sizes (`2 * num_values` entries) become impractically large past 16, and the Java original enforces the same bound.
- **`divide(x, 0)`** throws `std::runtime_error("Divide by zero")`. Mirrors the Java `ArithmeticException`.
- **`multiply(x, y)` (no-modulus, `GaliosFieldOps`)** can produce a result outside the field. This is intentional — it's a builder-block, not a field operation. Don't use it for runtime arithmetic.
- **`power(x, k)` with `x == 0`** indexes `log[0]` which is `0` (initialised by `vector::assign`), so `power(0, k) = exp[0] = 1`. **This is wrong mathematically (`0^k = 0` for `k > 0`, undefined for `k = 0`)** but matches the Java original. Callers must pre-check `x != 0` if zero inputs are reachable. Document this in the calling site (RS decoder) when we get there.
- **`power(x, k)` with very large `k`** overflows `int32_t` if `log[x] * k` does. For `GF(2^8)` and `k` up to a few thousand it's safe; for arbitrary `k` add a wider intermediate type or use `power_n`. RS in QR doesn't push past those bounds.
- **No const-correctness on `exp` / `log`.** Fields are public mutable. A consumer poking at `exp[]` post-construction will silently corrupt downstream lookups. Acceptable trade-off for parity testing access; if it bites we'll add a `const std::vector<int32_t>&` accessor.

## Tunable parameters

The constructor takes `(numBits, primitive)`:

| `numBits` | `primitive` | Field        | Use                                      |
|----------:|-------------|--------------|------------------------------------------|
| 2         | `0b111`     | `GF(2^2)`    | toy / test only                          |
| 4         | `0b10011`   | `GF(2^4)`    | toy / test only                          |
| 8         | `0b100011101` | `GF(2^8)`  | **QR Reed-Solomon (this is what matters)** |
| 16        | varies      | `GF(2^16)`   | not used by QR; bound for the constructor |

For QR the primitive is fixed by ISO/IEC 18004 §8.5: `x^8 + x^4 + x^3 + x^2 + 1 = 0b100011101 = 285`. Do not change. Reed-Solomon code parameters live in `ReedSolomonCodes_U8` (step 2) and pass this primitive in.

## Integration points for downstream recovery

This is the lowest layer; no strategy injection here. Downstream consumers reach Galois only transitively through `ReedSolomonCodes_*`. If a recovery pipeline wants to use a different primitive (e.g. Micro-QR's `GF(2^4)`), it constructs its own `GaliosFieldTableOps(4, …)` and passes that into the RS class — no subclassing needed.

The public mutable `exp` / `log` fields are *not* an injection point. They exist for parity testing, not for runtime override.

## Cross-references

- **Upstream BoofCV files** (pinned `v1.3.0` per `UPSTREAM_VERSION`):
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/GaliosFieldOps.java`
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/GaliosFieldTableOps.java`
- **Tests:**
  - `boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestGaliosFieldOps.java`
  - `boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestGaliosFieldTableOps.java`
  - C++ port: `tests/unit/test_galois.cpp` (mirrors all 11 JUnit cases).
- **Spec:** ISO/IEC 18004:2015 §8.5 — Error correction (defines QR's `GF(2^8)` and primitive 285).
- **Reference reading:** "Reed-Solomon Codes for Coders" — <https://en.wikiversity.org/wiki/Reed%E2%80%93Solomon_codes_for_coders>. Both the Java and C++ comments cite this; worth reading once if any algorithmic question reaches this layer.
- **Feeds into:** Step 2 — `ReedSolomonCodes_U8` (`src/reed_solomon/`) plus the `_U8`/`_U16` polynomial-on-byte-array specialisations. Those are deferred to step 2 because their byte-array operations are RS-specific, not pure Galois math.

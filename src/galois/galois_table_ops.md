# Galois polynomial-on-array operations — `GaliosFieldTableOpsT<WordT>`

Companion to `galois_table_ops.cpp` / `boofcv_qr/galois_table_ops.hpp`. Step 2 (a) of the porting plan — bridges the scalar Galois math from Step 1 into the polynomial routines that Reed-Solomon uses.

## Summary

`GaliosFieldTableOpsT<WordT>` extends `GaliosFieldTableOps` (scalar table-driven `multiply`/`divide`/`power`/`inverse` over `GF(2^numBits)`) with polynomial-on-array operations: `polyScale`, `polyAdd`, `polyAdd_S`, `polyAddScaleB`, `polyMult`, `polyMult_flipA`, `polyMult_S`, `polyEval`, `polyEval_S`, `polyEvalContinue`, `polyDivide`, `polyDivide_S`. Each polynomial is a `std::vector<WordT>` of coefficients. Two coefficient orderings are supported:

- **largest-first** (default, no suffix): `2x^3 + 8x^2 + 1 = [2, 8, 0, 1]`
- **smallest-first** (`_S` suffix): same polynomial = `[1, 0, 2, 8]`

Both orderings are needed because BoofCV's RS pipeline mixes them — the generator polynomial uses largest-first while the BM error locator uses smallest-first.

## Why a template, when Java has two classes

Java cannot parameterise generics on primitive types (no `Foo<byte>` / `Foo<short>`), so BoofCV ships `GaliosFieldTableOps_U8` and `GaliosFieldTableOps_U16` as line-by-line copies. C++ has no such restriction; one `template <typename WordT>` class covers both element widths and the public aliases `GaliosFieldTableOps_U8` / `GaliosFieldTableOps_U16` keep naming parity for cross-grepping. A bug fix in the C++ algorithm lands in both Java-equivalent classes at once — that is a feature, not a deviation from "verbatim".

The "verbatim" rule from CLAUDE.md (preserve loop structure, variable names, comments) is honoured: the loops in this file map line-for-line to either `GaliosFieldTableOps_U8.java` or `GaliosFieldTableOps_U16.java`. The only collapsed "duplication" is the type itself; everything algorithmic is identical.

## Algorithm description

All routines call into the table-driven scalar `multiply` / `divide` (inherited from `GaliosFieldTableOps`) for finite-field arithmetic. The polynomial-level patterns:

- **`polyScale`** — element-wise `multiply(input[i], scale)`. Trivial.
- **`polyAdd`** — XOR after aligning the smaller polynomial against the larger (right-aligned because coefficients are largest-first). The `_S` variant left-aligns instead.
- **`polyAddScaleB`** — `polyA + scaleB * polyB`. Combines `polyScale` and `polyAdd` in one pass; used by Berlekamp-Massey for the discrepancy update step.
- **`polyMult`** — schoolbook convolution: `out[i+j] ^= multiply(polyA[i], polyB[j])`. `O(|A|·|B|)`. The `_flipA` variant traverses `polyA` in reverse — used by `findErrorEvaluator` to multiply syndromes (smallest-first) by error locator (largest-first) without an explicit reverse step.
- **`polyEval` / `polyEval_S`** — Horner's method. `_S` walks the array right-to-left to evaluate a smallest-first polynomial.
- **`polyEvalContinue`** — feeds a previous Horner accumulator into a continuation polynomial, used when a long codeword is split across two `std::vector<WordT>` segments.
- **`polyDivide` / `polyDivide_S`** — synthetic division. Modifies the dividend in-place into `[quotient | remainder]`, then splits. The `_S` variant operates on smallest-first polynomials with mirrored indexing.

For QR specifically only `polyDivide`, `polyMult`, `polyAddScaleB`, `polyEval`, `polyEvalContinue`, `polyMult_flipA`, and `polyEval_S` are exercised (by the RS encoder and BM/Forney decoder). The rest are present for parity with the Java surface and Aztec/MicroQR coverage.

## Why this approach

- **Synthetic division for `polyDivide`.** The dividend is reused in-place as the quotient buffer; the remainder is split off at the end. Avoids a temporary buffer and matches the upstream code exactly. The trade-off (mutating the dividend) is invisible to callers because the wrapper signature takes the dividend by value-reference and copies into the quotient before mutation.

- **Doubled `exp` table consumed via inherited scalar `multiply`.** This file does not maintain its own log/exp tables — it leans entirely on the table-driven `multiply` from `GaliosFieldTableOps`. That is why the file looks like "regular polynomial code with a magic `multiply`": the `O(1)` field multiply hides all the work.

- **Two coefficient orderings, both kept.** It is tempting to pick one and reverse on the fly. We don't, because the Java code passes both orderings between RS stages — translating into a single ordering would require flipping arrays at every call boundary and would obscure the parity diff with Java. The `_flipA` variant is the canonical example: it does the flip implicitly during multiply rather than reversing the input ahead of time.

## Failure modes and known limits

- **Empty inputs.** `polyMult` / `polyDivide` assume non-empty inputs; an empty `polyA` or `polyB` gives `polyA.size() + polyB.size() - 1 = SIZE_MAX` (wraparound on `std::size_t`). The Java original has the same bug. Not exercised in the QR pipeline; bridge if Aztec gives us empty polynomials.
- **`polyDivide` with zero leading coefficient.** `divide(...)` will throw on the first `0 / normalizer` when `normalizer == 0`; if a QR generator ever leads with zero we have bigger problems. Java throws `ArithmeticException` here; C++ throws `std::runtime_error`. Same behavioural envelope.
- **`polyDivide_S` returns zero quotient size when `divisor.size() > dividend.size()`** but the remainder still equals the dividend. Mirrors Java exactly.
- **`polyEval` / `polyEval_S` on a length-0 polynomial** dereferences `input[0]` — undefined behaviour in C++, an out-of-bounds in Java. Don't pass empty polynomials. Pre-checked by all upstream callers.
- **`polyEval` returns `int32_t`** even though values fit in `WordT`. Required: `polyEval` is used as the input to `polyEvalContinue`, which combines via XOR and `multiply` returns `int`. Don't truncate the return type.

## Tunable parameters

There are no algorithmic knobs in this layer — it inherits `(numBits, primitive)` from the parent `GaliosFieldTableOps` constructor, which is fixed by the field choice. For QR: `(8, 0b100011101)`.

The `WordT` template parameter is the only "choice":

| `WordT`     | Field bits | Use                                        |
|-------------|-----------:|--------------------------------------------|
| `uint8_t`   |          8 | **QR (this is what matters)** + MicroQR    |
| `uint16_t`  |       8–16 | Aztec, very large QR variants, future codes |

For `uint16_t` with `numBits ≤ 8` the high byte of every word stays zero — wasteful storage but algorithmically identical, useful for testing template instantiation.

## Integration points for downstream recovery

This is internal infrastructure for Reed-Solomon. Downstream consumers do not call into `GaliosFieldTableOpsT` directly; they reach polynomial ops only through `ReedSolomonCodesT` (next file). If a consumer wants to hand-roll a custom RS variant (known-prefix erasures, codeword fusion across frames), they instantiate their own `GaliosFieldTableOpsT<WordT>` and feed it polynomials directly. No subclassing required.

## Cross-references

- **Upstream BoofCV files** (pinned `v1.3.0`):
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/GaliosFieldTableOps_U8.java`
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/GaliosFieldTableOps_U16.java`
- **Tests:**
  - `boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestGaliosFieldTableOps_U8.java`
  - `boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestGaliosFieldTableOps_U16.java`
  - C++ port: `tests/unit/test_galois_table_ops.cpp` (typed for both element widths).
- **Feeds into:** `ReedSolomonCodesT` (`src/reed_solomon/`).
- **Depends on:** `GaliosFieldTableOps` (`src/galois/galois.cpp`) for scalar `multiply`/`divide`/`power`/`inverse`/`power_n`.

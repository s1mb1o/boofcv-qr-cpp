# Reed-Solomon decoder — `ReedSolomonCodesT<WordT>`

Companion to `reed_solomon.cpp` / `boofcv_qr/reed_solomon.hpp`. Step 2 of the porting plan, the workhorse of the decode path.

## Summary

`ReedSolomonCodesT<WordT>` is a Reed-Solomon BCH-form codec over `GF(2^numBits)`. It can:

- **Encode** (`computeECC`): given a message of `K` words, produce `N - K` ECC words such that the concatenation `[message | ecc]` is a valid `RS(N, K)` codeword.
- **Decode and correct** (`correct`): given a possibly-corrupted message + ecc, locate up to `(N - K) / 2` errors and fix them in place. Returns `false` if the corruption exceeds the correction capacity.

This is the layer that lets QR survive smudges, partial occlusion, and printer artefacts. It also exposes the individual stages — syndrome computation, Berlekamp-Massey error-locator search, Forney magnitude evaluation — so downstream recovery pipelines (known-prefix RS, multi-frame codeword fusion, clipped-QR repair) can compose them differently. Per CLAUDE.md "Public API design", every named stage here is a public entry point.

## Algorithm description

The decoder is a textbook BCH-form Reed-Solomon following the Wikiversity tutorial cited inline in BoofCV. The pipeline `correct(input, ecc)` runs:

1. **`computeSyndromes(input, ecc, syndromes)`**
   - Each syndrome `S_i = M(α^(i + base))` where `M(x)` is the polynomial form of `[input | ecc]` and `α = 2`.
   - If the codeword is uncorrupted every `S_i = 0`. Non-zero syndromes carry the signature of the error pattern.
   - We split the polynomial across two arrays (input and ecc) and use `polyEvalContinue` so we don't need to concatenate.

2. **`findErrorLocatorPolynomialBM(syndromes, errorLocator)`** — Berlekamp-Massey
   - Iteratively builds a polynomial `Λ(x)` whose roots are at `α^(-position)` for each error position. The classic shift-register-synthesis recurrence:
     ```
     Δ_n = S_n + Σ_{j=1..L} Λ_j · S_{n-j}     (discrepancy)
     if Δ_n != 0:
         T(x) = Λ(x) - (Δ_n / b) · x · B(x)
         if 2L ≤ n: B(x) = Λ(x), b = Δ_n, L = n+1-L
         Λ(x) = T(x)
     ```
   - The stored `B`/`b`/`m` mirror Java exactly. The commented-out `L` and `m` in the source are kept commented because the upstream did the same — they're load-bearing only for reference, the live algorithm reuses `B.size()` to track shift.
   - If BM returns the trivial degree-zero locator `[1]`, `correct()` clears
     `errorLocations` and returns success immediately. This is the clean
     codeword fast path: syndromes/BM still ran, but Chien search and Forney
     correction would be no-ops.

3. **`findErrorLocations_BruteForce(errorLocator, messageLength, locations)`** — Chien search
   - Evaluate `Λ(α^i)` for `i = 0 .. messageLength`. Each root indicates an error at position `messageLength - i - 1`.
   - Returns false if the number of roots found does not equal the polynomial degree — signals "more errors than RS can correct."

4. **`correctErrors(message, length, syndromes, errorLocator, errorLocations)`** — Forney
   - For each error position, compute the magnitude:
     ```
     X_i = α^(coef_pos)        (error coefficient — the Chien-found root, inverted)
     y = Ω(X_i^-1)             (error evaluator at inverse root)
     err_loc_prime = ∏ (1 - X_i^-1 · X_j) for j ≠ i
     magnitude = X_i · y / err_loc_prime
     if generatorBase != 0: magnitude *= X_i^-1
     message[loc] ^= magnitude
     ```
   - The error evaluator polynomial Ω comes from `findErrorEvaluator` — `polyMult_flipA(syndromes, errorLocator)` truncated to `errorLocator.size()` and reversed in place.
   - The "BoofCV vs reference code" comment is preserved verbatim — Peter notes the simplified `power(2, coef_pos)` form passes all unit tests but isn't strictly equivalent to the textbook form. Don't "fix" this without measuring against the regression set.

5. **Encoding** (`computeECC`)
   - Compute the **generator polynomial** `g(x) = ∏ (x - α^(i + generatorBase))` for `i = 0 .. degree-1`.
     - For QR: `generatorBase = 0`. For Aztec: `generatorBase = 1`. ISO 18004 §7.2.3 lists known-good `g(x)` for `numBits = 4` which we test against directly.
   - Pad the message with `degree` zeros, polynomial-divide by `g`, the remainder is the ECC code.

## Why this approach

- **Berlekamp-Massey vs Peterson-Gorenstein-Zierler.** PGZ requires Gaussian elimination on a `t×t` matrix where `t` is the number of errors — fine when `t` is fixed and known, but RS for QR doesn't know `t` ahead of time and the worst case is `t = (N-K)/2 ≈ 14` (QR version-40, level H). BM is `O(N²)`, branchless, and gracefully truncates when the actual error count is below the budget. Standard choice for software RS decoders.

- **Chien search by brute force.** We loop `i = 0 .. N` evaluating `Λ(α^i)`. Faster alternatives exist (Horner-style update for Chien) but the loop runs at most a few hundred iterations per QR. Clean codewords now skip this stage after BM emits `[1]`; corrupted codewords stay on the same brute-force search for Java parity.

- **Forney over direct inversion.** Once the error locator is known, computing magnitudes via Forney avoids inverting the locator polynomial (which would be expensive and error-prone). The denominator `err_loc_prime` is the formal derivative of the error locator at each `X_i`; Forney pre-computes the contribution of every other `X_j ≠ i`.

- **Strategy injection isn't here yet.** CLAUDE.md "Public API design" requires hooks for known-prefix RS and clipped-QR fallback. We expose every internal step as a public method (`findErrorLocatorPolynomialBM`, `findErrorEvaluator`, `correctErrors`, etc.) so a consumer can drive the pipeline manually with custom syndromes/locators. Constructor-time `std::function` injection is deferred until we've seen the consumer call sites — premature today.

## Failure modes and known limits

- **`correct()` returns false** when:
  - The number of errors exceeds `(N - K) / 2`.
  - Errors line up in a way that produces a degenerate locator polynomial (rare; gives "Chien search wrong root count").
- **`computeSyndromes` throws `std::invalid_argument`** if `input.size() + ecc.size() > math.num_values`. For `GF(2^8)` that's 256 codewords. QR fits this comfortably (max is around 250-codeword blocks) but the bound is checked.
- **`computeECC` mutates `input`** then restores it. Not thread-safe on the same buffer; copy first if calling concurrently.
- **`generatorBase` outside `{0, 1}`** throws at construction. We don't validate the primitive polynomial — passing a reducible one would silently produce a non-field, and tests would fail with garbage.
- **`correct()` does not signal which positions were corrected** through its return — call `getTotalErrors()` afterwards or read `errorLocations` directly. The latter is public for that reason.
- **Clean codewords report zero errors** by clearing `errorLocations` before
  the fast return. This matters because decoder instances reuse workspace
  across blocks.
- **`generator_` member is named with a trailing underscore** to avoid clashing with the `generator(int degree)` member function. The Java original has both as `generator` because Java separates field/method namespaces. Keep this in mind when grepping across languages.
- **Magnitude correction is XOR-applied to message-positions only** — if the error landed in the ECC region the syndrome still cleared it from the locator, but `correctErrors` skips writing it back. Side effect: the *message* is recovered but the ECC bytes you got back may still hold the corrupted values. QR decoding only consumes the message portion so this is harmless.
- **`generatorBase = 1` magnitude path** (`magnitude *= X_i^-1`) is exercised by the `correct_random` Aztec config but not by QR's runtime path. Don't break it.

## Tunable parameters

| Parameter         | QR value          | Notes                                                                |
|-------------------|-------------------|----------------------------------------------------------------------|
| `numBits`         | 8                 | Fixed by ISO 18004 §8.5. MicroQR also 8. Aztec uses 6/8/10/12.       |
| `primitive`       | `0b100011101` (285) | QR's irreducible polynomial. Aztec uses different per field size.   |
| `generatorBase`   | 0                 | 1 for Aztec; affects both generator construction and Forney scaling. |
| `degree` (`generator(degree)`) | 7..30 | Number of ECC codewords per RS block. Set per QR ECC level + version per ISO 18004 Table 9. |
| `WordT`           | `uint8_t`         | `uint16_t` for codes that need a larger field.                       |

## Integration points for downstream recovery

These are the named injection points pricetag-vision (and any future consumer) may want to hook:

- **`computeSyndromes`** — replace to inject erasure positions or known-prefix syndromes before BM runs.
- **`findErrorLocatorPolynomialBM`** — replace to use a different error locator algorithm (e.g. Sugiyama's Euclidean-algorithm variant that handles erasures in one shot).
- **`findErrorLocations_BruteForce`** — replace with a custom locator scanner that knows about clipped-region positions a priori.
- **`correctErrors`** — replace to apply weighted corrections, fuse magnitudes across multiple frames, or skip corrections in known-bad regions.
- **All public mutable workspace fields** (`syndromes`, `errorLocatorPoly`, `errorX`, `err_eval`, etc.) are *not* injection points — they're cleared on every `correct()` call. Don't rely on persistence between calls.

When we wire `std::function` hooks, they will inject at the four named call sites above. Maintain the *invariants* documented per stage (e.g. `errorLocator` must have its leading zero stripped) — alternative implementations that violate those invariants will silently produce miscorrected codewords.

## Cross-references

- **Upstream BoofCV files** (pinned `v1.3.0`):
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/ReedSolomonCodes_U8.java`
  - `boofcv-recognition/src/main/java/boofcv/alg/fiducial/qrcode/ReedSolomonCodes_U16.java`
- **Tests:**
  - `boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestReedSolomonCodes_U8.java`
  - `boofcv-recognition/src/test/java/boofcv/alg/fiducial/qrcode/TestReedSolomonCodes_U16.java`
  - C++ port: `tests/unit/test_reed_solomon.cpp` (typed for both element widths; runs 60 000 random encode-corrupt-correct cycles for `uint8_t`).
- **Spec:** ISO/IEC 18004:2015 §7.2.3 (generator polynomials), §8.5 (RS error correction). The "Reed-Solomon Codes for Coders" Wikiversity tutorial cited inline in BoofCV is the practical reference.
- **Depends on:** `GaliosFieldTableOpsT<WordT>` (poly ops), `GaliosFieldTableOps` (scalar table multiply/divide/power/inverse), `GaliosFieldOps` (`subtract`).
- **Feeds into:** Step 3 (mode decoders consume RS-corrected codewords). The runtime pipeline order is `… raw codewords → RS correction → mode decode`, which is *the inverse of* the porting order in CLAUDE.md.

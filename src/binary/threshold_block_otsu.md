# BoofCV BLOCK_OTSU binarizer — `ThresholdBlockOtsu`

Companion to `boofcv_qr/threshold_block_otsu.hpp`. Step 6.

## Summary

QR's binarization stage. Tile the input into 40×40-pixel blocks, compute a 256-bin histogram per block, then for each block compute Otsu's threshold from the **3×3 neighbourhood of histograms** (i.e. the block's own histogram + its 8 neighbours), apply that threshold to every pixel in the block.

Per CLAUDE.md "Verbatim where algorithmic": Otsu's between-class-variance loop and the texture-penalty extension match BoofCV's `ComputeOtsu` line-for-line; the block-iteration outer loop matches `ThresholdBlock`. We collapse the BoofCV `ThresholdBlock` + `BlockProcessor<T,S>` + `ThresholdBlockOtsu` triad into a single class because the abstraction (a single ThresholdBlock driving multiple algorithm variants) doesn't pay for itself in our QR-only port.

QR's defaults match `ConfigQrCode.java`: `useOtsu2=true`, `scale=1.0`, `down=true`, `tuning=4`, `requestedBlockWidth=40`, `thresholdFromLocalBlocks=true`.

## Algorithm description

### Otsu, classical (`useOtsu2 = false`)

For each candidate threshold `T = 0..L-1` (where `L = 256`), split the histogram into "background" (`< T`) and "foreground" (`>= T`), compute weights `wB / wF`, means `mB / mF`, and the between-class variance `wB · wF · (mB - mF)²`. Pick the `T` that maximises the variance. The classic Otsu (1979).

### Otsu2 (`useOtsu2 = true`) — BoofCV's modified form

Same iteration, but at the maximum-variance `T*` we record the means `mB*, mF*` and return `threshold = L * (mB* + mF*) / 2`. The motivation (preserved in BoofCV's comment): in pathological flat regions where all `T` give the same variance, classical Otsu picks `T = 0` arbitrarily; this form returns the midpoint of the bimodal means, giving a saner fallback. QR uses this.

### Texture penalty (`tuning > 0`)

After picking `T`, compute `adjustment = (tuning · T)² / variance`, then `T -= adjustment` (when `down`) or `T += adjustment` (when `!down`). The intent: suppress threshold values in low-variance regions (which are likely texture-free background) so they don't get arbitrarily classified as half-black-half-white. BoofCV's defaults give `tuning = 4` for QR — we mirror.

### `thresholdFromLocalBlocks` (the "3×3 neighbourhood" form)

Per ISO discussion in BoofCV's comment: if a single block happens to fall entirely inside a black square, classical block-Otsu would treat it as textureless and reject it; the resulting binary loses the edge. Looking at the 3×3 neighbourhood instead means even a single textured neighbour will give the block a non-degenerate threshold. The trade-off is some smoothing across block boundaries; CLAUDE.md notes this is one reason `cv::adaptiveThreshold` isn't equivalent (it uses a different smoothing kernel).

Implementation note: the C++ port stores one 256-bin histogram per block, then computes each local 3×3 histogram with a sliding window. For a block row, it first sums the active three block rows into one vertical histogram per block column. It then slides a three-column horizontal window across those vertical sums, subtracting the column that leaves and adding the column that enters. The final histogram for each block is the same integer sum as the direct BoofCV 3×3 neighbourhood; only the data movement is reduced before the unchanged Otsu loop runs.

### Output convention

Per CLAUDE.md "Binary image convention": output is `CV_8UC1` with `0/1` values. With `down = true` (the QR default), pixels whose intensity ≤ threshold get value `1` (foreground = dark module). With `down = false`, the convention flips. **Don't use 0/255 internally** — multiply by 255 only at the dump boundary.

## Why this approach (and not OpenCV)

CLAUDE.md is explicit: `cv::adaptiveThreshold` is **not** equivalent and measurably degrades QR detection rate. The differences:

1. OpenCV's adaptive threshold uses a Gaussian-weighted local mean (or simple mean) computed at every pixel; BoofCV uses block-tiled Otsu (different per-block thresholds, not per-pixel). Block thresholds are robust to small-scale texture noise; per-pixel adaptive thresholds are jittery on QR module edges.
2. OpenCV's variant has no equivalent of the texture-penalty (`tuning`) term — it'd treat a uniformly-grey region as half-on/half-off.
3. The 3×3 block-neighbourhood smoothing is unique to BoofCV.

So we port verbatim and accept the cost.

## Failure modes

- Non-`CV_8UC1` input → `std::invalid_argument`.
- `requestedBlockWidth` larger than the image dimension → `selectBlockSize` falls back to using the full dimension as the block. Tested.
- Empty histogram in some block (block at the corner with rounding) → `compute` returns `threshold = 0`, `variance = 0`. Texture-penalty division-by-zero is guarded by `variance += 0.001`.
- Multi-thread: not safe. Each `ThresholdBlockOtsu` instance owns its `stats_` buffer; share by copying or constructing per-thread instances.
- **Known parity residual on `monitor` / `glare` categories** — 0.6–1.9% per-pixel binary divergence vs Java BoofCV's `ThresholdBlockOtsu`, **100% within 1px of fg/bg boundary, 0% interior**. Root-caused to IEEE-754 floating-point accumulation order in `ComputeOtsu`'s histogram-sum loops: identical algorithm, bit-different sums → bit-different threshold by 0–1 grayvalue → boundary pixels flip categories. Not a port bug; an inherent property of porting numerically-sensitive code between languages with different FP idioms. Empirical audit + decision-not-to-chase is documented in [ADR 06](../../docs/decisions/06_threshold_block_otsu_audit.md). Compounds downstream into the `monitor -11.76pp` / `glare -3.77pp` accepted residuals (per `tests/accepted_residuals.json`).

## Tunable parameters

| Field | QR default | Effect |
|---|---|---|
| `useOtsu2` | true | Classical Otsu (false) is jumpier on bimodal histograms; otsu2 (true) is smoother. |
| `down` | true | Thresholding direction. true → dark = foreground. |
| `scale` | 1.0 | Multiplier on the computed threshold. < 1 = more aggressive foreground. |
| `tuning` | 4 | Texture-penalty strength. 0 = standard Otsu. |
| `requestedBlockWidth` | 40 | Block side in pixels. Smaller = sharper local response, more blockiness on uniform areas. |
| `thresholdFromLocalBlocks` | true | 3×3 vs single-block histogram. true is more robust at block boundaries. |

## Cross-references

- Upstream BoofCV: `boofcv-ip/src/main/java/boofcv/alg/filter/binary/{ThresholdBlock,ThresholdBlockOtsu,ComputeOtsu}.java`. We collapse the three into one class.
- Spec: Otsu, "A Threshold Selection Method from Gray-Level Histograms" (IEEE Trans. SMC, 1979).
- Tests: `tests/unit/test_threshold_block_otsu.cpp` — synthetic bimodal images (BoofCV's tests for these classes live in `boofcv-ip/src/test/`, which we don't pull in).
- Used by: step-7 polygon detector (consumes the binary output), step-9 orchestrator (kicks off the binarization).

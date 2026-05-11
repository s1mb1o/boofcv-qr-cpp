## Summary

Describe the change and why it is needed.

## Validation

- [ ] `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`
- [ ] `cmake --build build --target boofcv_qr qr_scan boofcv_qr_tests boofcv_qr_python -- -j`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `python3 -m pip wheel . --no-deps -w /tmp/boofcv_qr_wheel`
- [ ] BoofCV regression run, if detection/decoding behavior changed

## Parity Notes

State whether this changes BoofCV parity, performance, packaging, or docs only.

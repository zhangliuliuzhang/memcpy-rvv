# Memset Strategy Notes

Current best C/intrinsics strategy on Spacemit X60:
- Keep the zero-specialized path with `cbo.zero` for `memset(..., 0, n)`.
- Remove the old 8-byte pre-alignment from the non-zero vector path.
- Use a resident `e8m8` full vector (`vlmax_e8m8 = 256`) so `vmv.v.x` is done once outside the main loop.
- Main non-zero hot loop stays as a clean store stream without software prefetch.

What worked:
- `cbo.zero` improved large zeroing and kept `memset(0)` ahead of libc on large sizes.
- Removing non-zero 8-byte pre-alignment improved the large non-zero path.
- Resident full-vector reuse was better than rebuilding the fill vector every iteration.
- Removing `prefetch.w` from the non-zero loop was the key step that brought 4KB/16KB/64KB back to roughly match or beat libc.

What did not work well:
- Rebuilding the fill vector every iteration in a glibc-like intrinsics loop was slower than the resident-vector version.
- Putting `prefetch.w 256(...)` inside the loop hurt large non-zero memset.
- Moving the prefetch decision outside the loop and increasing distance to `1024B` still hurt large non-zero memset.
- Current evidence suggests software prefetch is counterproductive for this RVV memset on Spacemit X60.

Useful machine facts measured remotely:
- L1D cache: 32KB
- L2 cache: 512KB
- Cache line: 64B
- `vlmax_e8m8 = 256`

Takeaway:
- Best current C version: no software prefetch, resident full vector for non-zero, `cbo.zero` for zero.
- If we want another meaningful jump from here, the next likely step is a hand-written `.S` version rather than more C intrinsics micro-tuning.

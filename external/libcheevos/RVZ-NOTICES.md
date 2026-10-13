# GameCube RVZ hash reader

`rvz_hash.cpp` and `rvz_hash.h` are original pixelelated integration code under
MIT, except the padding recurrence/serialization adapted under BSD-3-Clause
from bodgit/rvz `internal/padding/reader.go` at
`bc439051cbedab7fe22a0c73bd2e7f19a310645e`
(<https://github.com/bodgit/rvz/blob/bc439051cbedab7fe22a0c73bd2e7f19a310645e/internal/padding/reader.go>). Preserve `LICENSE.rvz-padding`.
The ES root `LICENSE.md` carries the MIT permission notice.

The exact sources, limits, dependencies, and unrun qualification controls are
recorded in distribution issue #574 and
`docs/qa-logs/2026-10-13-rvz-source/`. No RetroArch GPL reader is imported.
Dolphin's published `docs/WiaAndRvz.md` describes the container format.
Boost SHA-1 uses the existing BSL-1.0 headers; Zstandard uses BSD-3-Clause
(the permissive option of its dual license). They are not copied here.

Scope: GameCube RVZ v0.3 through v1.0, NONE or Zstandard, 32 KiB through 2 MiB
power-of-two chunks, raw data and RVZ packing, including lossless padding.
Wii/WIA, BZIP2/LZMA/LZMA2, larger chunks, incomplete/gapped raw mappings,
and inputs outside the explicit reader limits fail. Limits also include 4096
raw entries, 65536 groups, 4096 packed records per chunk, 2 GiB virtual disc,
4 GiB container, 128 MiB bytes read per hash, and 512 MiB decoded work per
reader. These are rejection bounds, not a measured memory or timing claim. This is a bounded
frontend reader candidate, not a disc verifier or in-game achievement engine.

Build with `ENABLE_RVZ_HASH=ON` and target Boost/Zstandard headers and libzstd.
It defaults on for ROCKNIX. The distribution must declare the direct zstd
build/runtime dependency when integrating. Other builds can opt in; missing
requested dependencies fail configuration rather than silently disable it.

# Third-Party Notices

## AKS-Logic aptx-adaptive-linux

This project is a C-language rewrite based on
[AKS-Logic/aptx-adaptive-linux](https://github.com/AKS-Logic/aptx-adaptive-linux),
primarily referencing the `crates/aptx-adaptive` implementation at revision
`f8a090875ae2d23721f0ed175f77228f190444f1`.

The referenced upstream project is Copyright (c) 2026 Prateek Mittal
(AKS-Logic) and is licensed under the MIT License. The license text that must
be retained is stored in
[`LICENSES/AKS-Logic_LICENSES`](LICENSES/AKS-Logic_LICENSES). The repository's
root [`LICENSE`](LICENSE) also retains the same text.

Based on the upstream implementation, this project rewrites the encoding
algorithms, mode data, coefficients, and coding tables in C, and independently
implements the decoder. It also reorganizes the public C API, build structure,
host memory allocation, driver resource mode, and local validation tools for
this repository. The project neither includes nor loads vendor binary files.

The upstream clean-room and legal statements are retained in
[`UPSTREAM_LEGAL.md`](UPSTREAM_LEGAL.md).
`aptX` and related names are trademarks of Qualcomm Incorporated. This project
is not affiliated with, sponsored by, or endorsed by Qualcomm.

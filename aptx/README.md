# aptX Adaptive Codec Components

This directory contains the codec components used by the release:

- [`common`](common): shared types, tables, prediction, and runtime adapters;
- [`encoder`](encoder): encoder core and queued stream control;
- [`decoder`](decoder): packet decoding, reconstruction, and synthesis; and
- [`platform`](platform): host-specific integration that is not part of the
  portable codec core.

Components can be selected independently through the top-level CMake options.
Mutable state is instance-owned and processing performs no dynamic allocation
after successful initialization.

[Chinese documentation](README.zh-CN.md)

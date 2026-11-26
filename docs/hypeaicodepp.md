# HypeAICode++

HypeAICode++ (`hcpp`) is the freestanding C++ runtime and library layer for HypeAI OS. It is not a hosted copy of `std::`; it is a small kernel-safe API designed for an environment without Linux, libc, exceptions, RTTI or dynamic memory during early boot.

## Design rules

- namespace: `hcpp`;
- no dependency on `std::` or platform libc;
- fixed-width integer types are defined directly;
- exceptions and RTTI are disabled;
- allocation APIs are explicit and fail predictably;
- kernel code must remain usable before a heap exists;
- algorithms and containers are added only when memory management is ready;
- headers declare interfaces only; function implementations live in `.cc` files rather than inline header code.

## Initial modules

- `hcpp/types.hpp`: fixed-width integer types, `size_t`, byte order assumptions.
- `hcpp/memory.hpp`: `copy`, `move`, `set`, `compare`, zeroing primitives.
- `hcpp/string_view.hpp`: non-owning immutable string view.
- `hcpp/result.hpp`: lightweight status/error reporting type.
- `hcpp/text_writer.hpp`: fixed-capacity kernel text formatter.
- `hcpp/fonts/font.hpp`: HypeAIFont registry built on `FontFace` (name, height, baseline, advance, glyph_count, `glyph_for` codepoint lookup) and `FontGlyph` metrics (bitmap, row_stride, width, height, advance, bearing_x/y), with runtime registration and default font selection; the built-in boot font is an Arial-compatible bitmap fallback rather than Microsoft's proprietary vector Arial file.
- `hcpp/fonts/hpf.hpp`: loader for the HPF binary font format (see `docs/hypeai-font-format.md`); the embedded `hypeai-8x16` face is registered at boot through `load_embedded_fonts`.
- `hcpp/utf8.hpp`: UTF-8 decoding — `next_codepoint` walks a `StringView` and yields codepoints (with U+FFFD replacement on malformed input) that are passed to `FontFace::glyph_for`.
- `hcpp/graphics/framebuffer.hpp`: framebuffer description and drawing primitives.
- `hcpp/display/*`: EDID, CTA colorimetry, HDR metadata and color-space model.
- `hcpp/atomic.h`, `hcpp/bitset.h`, `hcpp/crc32.h`, `hcpp/guid.h`: low-level kernel-safe primitives inspired by Linux kernel atomics and Windows GUID/Interlocked APIs.

## Relationship to C++ stdlib

The public C++ standard library assumes host services: files, threads, locale, dynamic allocation, process termination and exception support. A kernel does not have those services initially. OSDev guidance confirms that using `std::vector`, `std::string` and similar facilities requires either porting an existing implementation or writing kernel-specific equivalents.

HypeAICode++ therefore starts as a freestanding replacement layer. Later it may expose compatible subsets where semantics are safe, but binary compatibility with `std::` is not a goal.

Source: https://wiki.osdev.org/C%2B%2B

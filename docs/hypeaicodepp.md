# HypeAICode++

HypeAICode++ (`hcpp`) is the freestanding C++ runtime and library layer for HypeAI OS. It is not a hosted copy of `std::`; it is a small kernel-safe API designed for an environment without Linux, libc, exceptions, RTTI or dynamic memory during early boot.

## Design rules

- namespace: `hcpp`;
- no dependency on `std::` or platform libc;
- fixed-width integer types are defined directly;
- exceptions and RTTI are disabled;
- allocation APIs are explicit and fail predictably;
- kernel code must remain usable before a heap exists;
- algorithms and containers are added only when memory management is ready.

## Initial modules

- `hcpp/types.hpp`: fixed-width integer types, `size_t`, byte order assumptions.
- `hcpp/memory.hpp`: `copy`, `move`, `set`, `compare`, zeroing primitives.
- `hcpp/string_view.hpp`: non-owning immutable string view.
- `hcpp/result.hpp`: lightweight value/error result type.
- `hcpp/graphics/framebuffer.hpp`: framebuffer description and drawing primitives.

## Relationship to C++ stdlib

The public C++ standard library assumes host services: files, threads, locale, dynamic allocation, process termination and exception support. A kernel does not have those services initially. OSDev guidance confirms that using `std::vector`, `std::string` and similar facilities requires either porting an existing implementation or writing kernel-specific equivalents.

HypeAICode++ therefore starts as a freestanding replacement layer. Later it may expose compatible subsets where semantics are safe, but binary compatibility with `std::` is not a goal.

Source: https://wiki.osdev.org/C%2B%2B

// Host-side harness for kernel/codecapps/image/jpeg.cc. Build (msys):
//   g++ -std=c++20 -I. tools/jpeg_host_test.cc kernel/codecapps/image/jpeg.cc -o build/jpeg_host_test
//   ./build/jpeg_host_test [file.jpg] [out.ppm]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "kernel/codecapps/arena.h"
#include "kernel/codecapps/image/jpeg.h"
#include "kernel/codecapps/image/jpeg_selftest_blob.h"

namespace hype::codec {

    void *arena_alloc(Arena &arena, hcpp::size_t size, hcpp::size_t align) noexcept {
        if (arena.base == nullptr || align == 0 || (align & (align - 1)) != 0)
            return nullptr;
        const hcpp::size_t mask = align - 1;
        const hcpp::size_t aligned = (arena.used + mask) & ~mask;
        if (aligned < arena.used || size > arena.cap || aligned > arena.cap - size)
            return nullptr;
        arena.used = aligned + size;
        return arena.base + aligned;
    }

    void arena_reset(Arena &arena) noexcept { arena.used = 0; }

}  // namespace hype::codec

static hcpp::u32 fnv1a(const hcpp::u8 *data, hcpp::size_t size) {
    hcpp::u32 hash = 2166136261u;
    for (hcpp::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

int main(int argc, char **argv) {
    std::vector<hcpp::u8> input(jpeg_selftest_blob,
                                jpeg_selftest_blob + sizeof(jpeg_selftest_blob));
    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        if (!f) {
            fprintf(stderr, "cannot open %s\n", argv[1]);
            return 1;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        input.resize(n);
        if (fread(input.data(), 1, n, f) != (size_t)n) {
            fclose(f);
            return 1;
        }
        fclose(f);
    }
    std::vector<hcpp::u8> backing(64 * 1024 * 1024);
    hype::codec::Arena arena{backing.data(), backing.size(), 0};
    std::vector<hcpp::u8> pixels(64 * 1024 * 1024);
    hype::codec::FrameBuf fb = {};
    fb.data = pixels.data();
    fb.capacity = pixels.size();
    hype::codec::CodecStatus st =
        hype::codec::image::jpeg_decode(input.data(), input.size(), &fb, arena);
    printf("status=%d width=%u height=%u pitch=%u size=%zu\n", (int)st, fb.width,
           fb.height, fb.pitch, fb.size);
    if (st != hype::codec::CodecStatus::Ok) return 2;
    printf("checksum=0x%08X\n", fnv1a(fb.data, fb.size));
    hcpp::u32 distinct = 0;
    for (hcpp::size_t i = 0; i + 1 < fb.size; ++i)
        if (fb.data[i] != fb.data[i + 1]) ++distinct;
    printf("distinct=%u arena_used=%zu\n", distinct, arena.used);
    if (argc > 2) {
        FILE *f = fopen(argv[2], "wb");
        if (f) {
            fprintf(f, "P6\n%u %u\n255\n", fb.width, fb.height);
            for (hcpp::size_t i = 0; i + 3 < fb.size; i += 4)
                fwrite(fb.data + i, 1, 3, f);
            fclose(f);
        }
    }
    printf("selftest=%d\n", hype::codec::image::jpeg_selftest() ? 1 : 0);
    return 0;
}

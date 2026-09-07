//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "__bench.hpp"

namespace
{

constexpr usize n = 20000;
usize sizes[n];
byte *ptrs[n];

u64
next(u64 &s)
{
  u64 z = (s += 0x9E37'79B9'7F4A'7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EBull;
  return z ^ (z >> 31);
}

}

int
main()
{
  if( !bb::init() )
    return 0;
  bbbench::header(
      "interleaved: sizes span both tiers (1 B .. 16 KiB) to defeat tier-dispatch prediction; alloc/free interleaved");
  u64 seed = 0xB01CA11ull;
  for( usize i = 0; i < n; ++i ) {
    const u64 k = next(seed) % 100;
    sizes[i] = k < 50 ? 1 + next(seed) % 256 : (k < 85 ? 257 + next(seed) % 768 : 1025 + next(seed) % 15360);
  }
  for( usize i = 0; i < n; ++i )
    ptrs[i] = nullptr;
  auto s = bbbench::measure(
      n * 2,
      [&]() {
        for( usize i = 0; i < n; ++i )
          ptrs[i] = nullptr;
      },
      [&]() {
        for( usize i = 0; i < n; ++i ) {
          ptrs[i] = bb::alloc(sizes[i]);
          bbbench::clobber(ptrs[i]);
          if( i >= 64 ) {
            bb::dealloc(ptrs[i - 64]);
            ptrs[i - 64] = nullptr;
          }
        }
        for( usize i = 0; i < n; ++i )
          if( ptrs[i] )
            bb::dealloc(ptrs[i]);
      },
      []() {});
  bbbench::report("[interleaved] alloc+free window 64", 16384, s);
  return 0;
}

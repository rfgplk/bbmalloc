//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "__bench.hpp"

namespace
{

struct bracket {
  const char *name;
  usize lo;
  usize hi;
};

constexpr bracket brackets[] = { { "1-32", 1, 32 },
                                 { "33-256", 33, 256 },
                                 { "257-1024", 257, 1024 },
                                 { "1025-4096", 1025, 4096 },
                                 { "4097-32768", 4097, 32768 } };
constexpr usize counts[] = { 1000, 10000, 100000 };
usize sizes[100000];
byte *ptrs[100000];

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
      "hot path: random sizes within one bracket, alloc then free, sizes prefilled outside the timed window");
  for( const bracket &b : brackets ) {
    for( usize n : counts ) {
      usize live = 0;
      for( usize i = 0; i < n; ++i )
        live += b.hi;
      if( live > bb::capacity() / 2 )
        continue;
      u64 seed = 0x5040A11ull;
      for( usize i = 0; i < n; ++i )
        sizes[i] = b.lo + next(seed) % (b.hi - b.lo + 1);
      auto s = bbbench::measure(
          n * 2, []() {},
          [&]() {
            for( usize i = 0; i < n; ++i ) {
              ptrs[i] = bb::alloc(sizes[i]);
              bbbench::clobber(ptrs[i]);
            }
            for( usize i = 0; i < n; ++i )
              bb::dealloc(ptrs[i]);
          },
          []() {});
      micron::print("[hot ", b.name, " n=", n, "] ");
      bbbench::report("alloc+free", b.hi, s);
    }
  }
  return 0;
}

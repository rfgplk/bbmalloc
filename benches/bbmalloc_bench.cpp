//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "__bench.hpp"

namespace
{

constexpr usize sizes[] = { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 16384, 65536, 262144, 1048576 };
constexpr usize reps = 4096;
byte *held[reps];

}

int
main()
{
  if( !bb::init() )
    return 0;
  bbbench::header("round-trip alloc+dealloc, pool alloc-all-then-free-all, realloc small-big-small, queries");
  for( usize sz : sizes ) {
    usize n = reps;
    while( n * sz > bb::capacity() / 2 && n > 8 )
      n >>= 1;
    auto s = bbbench::measure(
        n, []() {},
        [&]() {
          for( usize i = 0; i < n; ++i ) {
            byte *p = bb::alloc(sz);
            bbbench::clobber(p);
            bb::dealloc(p);
          }
        },
        []() {});
    bbbench::report("[round-trip] alloc+dealloc", sz, s);
  }
  for( usize sz : sizes ) {
    usize n = reps;
    while( n * sz > bb::capacity() / 2 && n > 8 )
      n >>= 1;
    auto s = bbbench::measure(
        n, []() {},
        [&]() {
          for( usize i = 0; i < n; ++i ) {
            held[i] = bb::alloc(sz);
            bbbench::clobber(held[i]);
          }
          for( usize i = 0; i < n; ++i )
            bb::dealloc(held[i]);
        },
        []() {});
    bbbench::report("[pool] alloc-all,free-all", sz, s);
  }
  for( usize sz : { usize{ 64 }, usize{ 1024 }, usize{ 8192 } } ) {
    auto s = bbbench::measure(
        1024, []() {},
        [&]() {
          for( usize i = 0; i < 1024; ++i ) {
            void *p = bb::malloc(sz);
            p = bb::realloc(p, sz * 8);
            p = bb::realloc(p, sz);
            bbbench::clobber(p);
            bb::free(p);
          }
        },
        []() {});
    bbbench::report("[realloc] small-big-small", sz, s);
  }
  {
    byte *p = bb::alloc(300);
    volatile usize sink = 0;
    auto s = bbbench::measure(
        4096, []() {},
        [&]() {
          for( usize i = 0; i < 4096; ++i )
            sink += bb::query_size(p) + (bb::is_present(p) ? 1 : 0) + (bb::within(p) ? 1 : 0);
        },
        []() {});
    bbbench::report("[queries] query_size+is_present+within", 300, s);
    bb::dealloc(p);
  }
  return 0;
}

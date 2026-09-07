//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_rigor.hpp"

using namespace snowball;

int
main()
{
  sb::require(bbtest::pool());
  const usize cap = bb::capacity();
  const usize avail0 = bb::available();
  const usize top = bb::largest_free();

  sb::test_case("exhausting the sheet budget does not fail a small request: the buddy serves it");
  {
    static byte *small[65536];
    usize n = 0;
    const u64 f0 = bb::stats().fallbacks;
    while( n < 65536 ) {
      small[n] = bb::alloc(1000);
      if( !small[n] )
        break;
      ++n;
    }
    sb::require(n > 0);
    sb::require(bb::stats().enabled && bb::stats().fallbacks > f0);
    bool any_pow2 = false;
    for( usize i = 0; i < n; ++i )
      if( bb::query_size(small[i]) == 1024 )
        any_pow2 = true;
    sb::require(any_pow2);
    for( usize i = 0; i < n; ++i )
      bb::dealloc(small[i]);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("filling the pool with large blocks ends in a null, not a crash; freeing restores the top block");
  {
    static byte *big[4096];
    usize n = 0;
    while( n < 4096 ) {
      big[n] = bb::alloc(64 << 10);
      if( !big[n] )
        break;
      ++n;
    }
    sb::require(n > 0 && n <= cap / (64 << 10));
    sb::require(bb::alloc(64 << 10) == nullptr);
    byte *tiny = bb::alloc(1);
    sb::require(tiny != nullptr || bb::available() < 64);
    bb::dealloc(tiny);
    for( usize i = 0; i < n; ++i )
      bb::dealloc(big[i]);
    byte *last = bb::alloc(1);
    bb::dealloc(last);
    sb::require(bb::available() == avail0);
    sb::require(bb::largest_free() == top);
    bb::reset();
    sb::require(bb::largest_free() == top && bb::available() == avail0);
    sb::require(bb::fragmentation_permille() == 1000 - (top * 1000) / avail0);
  }
  sb::end_test_case();

  sb::test_case("a request larger than the pool, or of zero bytes, is refused without touching state");
  {
    sb::require(bb::alloc(cap + 1) == nullptr);
    sb::require(bb::alloc(0) == nullptr);
    sb::require(bb::balloc(static_cast<usize>(-1)).ptr == nullptr);
    sb::require(bb::aligned_balloc(4096, static_cast<usize>(-1)).ptr == nullptr);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_exhaust PASSED ===");
  return 1;
}

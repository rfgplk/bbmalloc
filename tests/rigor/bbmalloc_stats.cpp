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

  sb::test_case("counters follow allocations, frees, sheets and fallbacks");
  {
    bb::reset_stats();
    auto s0 = bb::stats();
    sb::require(s0.enabled && s0.alloc_requests == 0);
    byte *a = bb::alloc(100);
    byte *b = bb::alloc(70000);
    auto s1 = bb::stats();
    sb::require(s1.alloc_requests == 2 && s1.bytes_requested == 70100 && s1.bytes_granted >= 70100
                && s1.current_usage == s1.bytes_granted);
    sb::require(s1.sheets_acquired >= 1);
    bb::dealloc(a);
    bb::dealloc(b);
    auto s2 = bb::stats();
    sb::require(s2.dealloc_requests == 2 && s2.current_usage == 0 && s2.bytes_freed == s1.bytes_granted);
    sb::require(bb::alloc(cap * 4) == nullptr && bb::stats().failures == 1);
  }
  sb::end_test_case();

  sb::test_case("fragmentation is zero on an empty heap, rises under a checkerboard, and largest_free follows");
  {
    sb::require(bb::fragmentation_permille() == 0);
    static byte *p[512];
    for( int i = 0; i < 512; ++i )
      p[i] = bb::alloc(4096);
    for( int i = 0; i < 512; i += 2 )
      bb::dealloc(p[i]);
    const u32 f = bb::fragmentation_permille();
    const usize lf = bb::largest_free();
    sb::require(f > 0 && f <= 1000);
    sb::require(lf >= 4096 && lf < bb::available());
    for( int i = 1; i < 512; i += 2 )
      bb::dealloc(p[i]);
    sb::require(bb::fragmentation_permille() < f);
    bb::reset();
    sb::require(bb::fragmentation_permille() == 0 && bb::largest_free() == cap);
  }
  sb::end_test_case();

  sb::test_case("which() prints without touching the heap");
  {
    const usize before = bb::available();
    bb::which();
    sb::require(bb::available() == before);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_stats PASSED ===");
  return 1;
}

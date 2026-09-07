//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../../src/bbmalloc.hpp"
#include "../snowball/snowball.hpp"

using namespace snowball;

namespace
{

alignas(4096) byte bank0[256 << 10];
alignas(4096) byte bank1[512 << 10];
byte bank2[(64 << 10) + 24];

};

int
main()
{
  sb::require(bb::__max_regions >= 3);
  bb::attach(bank0, sizeof(bank0));
  bb::attach(bank1, sizeof(bank1));
  bb::attach(bank2 + 8, sizeof(bank2) - 8);
  sb::require(bb::__the_heap.count == 3);
  const usize cap = bb::capacity();
  const usize avail0 = bb::available();

  sb::test_case("every bank is within, nothing else is, and each bank fills in order");
  {
    sb::require(bb::within(bank0 + 100000) && bb::within(bank1 + 100000) && bb::within(bank2 + 16384));
    u64 on_stack = 0;
    sb::require(!bb::within(&on_stack));
    static byte *p[1024];
    usize n = 0;
    while( n < 1024 ) {
      p[n] = bb::alloc(8192);
      if( !p[n] )
        break;
      ++n;
    }
    sb::require(n * 8192 > (256u << 10) && n * 8192 <= cap);
    bool in0 = false;
    bool in1 = false;
    bool in2 = false;
    for( usize i = 0; i < n; ++i ) {
      if( p[i] >= bank0 && p[i] < bank0 + sizeof(bank0) )
        in0 = true;
      if( p[i] >= bank1 && p[i] < bank1 + sizeof(bank1) )
        in1 = true;
      if( p[i] >= bank2 && p[i] < bank2 + sizeof(bank2) )
        in2 = true;
    }
    sb::require(in0 && in1 && in2);
    for( usize i = 0; i < n; i += 2 )
      bb::dealloc(p[i]);
    for( usize i = 1; i < n; i += 2 )
      bb::dealloc(p[i]);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("a block that no longer fits the first bank is served from a later one");
  {
    static byte *fill[64];
    usize n = 0;
    while( n < 64 ) {
      fill[n] = bb::alloc(128 << 10);
      if( !fill[n] || !(fill[n] >= bank0 && fill[n] < bank0 + sizeof(bank0)) )
        break;
      ++n;
    }
    byte *q = bb::alloc(200 << 10);
    sb::require(q != nullptr && q >= bank1 && q < bank1 + sizeof(bank1));
    byte *s = bb::alloc(100);
    sb::require(s != nullptr && bb::is_present(s));
    bb::dealloc(s);
    bb::dealloc(q);
    for( usize i = 0; i < n; ++i )
      bb::dealloc(fill[i]);
    if( n < 64 && fill[n] )
      bb::dealloc(fill[n]);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("small blocks in every bank are located through their own sheet index");
  {
    static byte *s[3000];
    usize n = 0;
    while( n < 3000 ) {
      s[n] = bb::alloc(500);
      if( !s[n] )
        break;
      ++n;
    }
    bool ok = n > 100;
    for( usize i = 0; i < n && ok; ++i )
      if( !bb::is_present(s[i]) || bb::query_size(s[i]) < 500 )
        ok = false;
    for( usize i = 0; i < n; ++i )
      if( !bb::dealloc(static_cast<void *>(s[i])) )
        ok = false;
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("reset covers every bank");
  {
    for( int i = 0; i < 100; ++i )
      (void)bb::alloc(3000 + static_cast<usize>(i) * 100);
    bb::reset();
    sb::require(bb::available() == cap);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_regions PASSED ===");
  return 1;
}

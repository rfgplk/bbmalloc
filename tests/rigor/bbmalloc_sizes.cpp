//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_rigor.hpp"

using namespace snowball;

namespace
{

bool
spot(byte *p, usize n, byte v)
{
  p[0] = v;
  p[n / 2] = v;
  p[n - 1] = v;
  return p[0] == v && p[n / 2] == v && p[n - 1] == v;
}

};

int
main()
{
  sb::require(bbtest::pool());
  const usize avail0 = bb::available();

  sb::test_case("every size from 1 to 16384 round-trips with head, middle and tail writes");
  {
    bool ok = true;
    for( usize n = 1; n <= 16384 && ok; ++n ) {
      byte *p = bb::alloc(n);
      if( !p || bb::query_size(p) < n || !spot(p, n, static_cast<byte>(n)) )
        ok = false;
      bb::dealloc(p);
    }
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("every size from 1 to 4096 with 64 blocks live at a time");
  {
    static byte *ring[64];
    for( auto &r : ring )
      r = nullptr;
    bool ok = true;
    for( usize n = 1; n <= 4096 && ok; ++n ) {
      const usize k = n % 64;
      if( ring[k] )
        bb::dealloc(ring[k]);
      ring[k] = bb::alloc(n);
      if( !ring[k] || !spot(ring[k], n, static_cast<byte>(n * 3)) )
        ok = false;
    }
    for( auto &r : ring )
      if( r )
        bb::dealloc(r);
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("powers of two from 16 bytes to a quarter of the pool, and one past the pool");
  {
    bool ok = true;
    for( usize n = 16; n <= bb::capacity() / 4 && ok; n <<= 1 ) {
      byte *p = bb::alloc(n);
      if( !p || bb::query_size(p) < n || !spot(p, n, 0x77) )
        ok = false;
      bb::dealloc(p);
    }
    sb::require(ok);
    sb::require(bb::alloc(bb::capacity() * 2) == nullptr);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_sizes PASSED ===");
  return 1;
}

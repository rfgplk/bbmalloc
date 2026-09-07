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
  const usize avail0 = bb::available();

  sb::test_case("query_size is the isolated usable extent: filling it never tramples a neighbour");
  {
    static byte *p[2048];
    static usize n[2048];
    bbtest::rng r(0x5EA11ull);
    bool ok = true;
    for( usize i = 0; i < 2048; ++i ) {
      n[i] = 1 + r.next() % 2000;
      p[i] = bb::alloc(n[i]);
      if( !p[i] ) {
        ok = false;
        break;
      }
      bbtest::fp_write(p[i], bb::query_size(p[i]), i, 1);
    }
    for( usize i = 0; i < 2048 && ok; ++i )
      if( !bbtest::fp_check(p[i], bb::query_size(p[i]), i, 1) )
        ok = false;
    for( usize i = 0; i < 2048; ++i )
      if( p[i] )
        bb::dealloc(p[i]);
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("20000 realloc rounds keep the surviving prefix and report an honest size");
  {
    bbtest::rng r(0x12340002ull);
    bool ok = true;
    byte *p = bb::alloc(64);
    usize have = bb::query_size(p);
    bbtest::fp_write(p, have, 7, 1);
    usize gen = 1;
    for( u32 i = 0; i < 20000 && ok; ++i ) {
      const usize nn = 1 + r.next() % 6000;
      byte *q = static_cast<byte *>(bb::realloc(p, nn));
      if( !q ) {
        ok = false;
        break;
      }
      const usize now = bb::query_size(q);
      const usize keep = have < now ? have : now;
      if( now < nn || !bbtest::fp_check(q, keep, 7, gen) )
        ok = false;
      ++gen;
      bbtest::fp_write(q, now, 7, gen);
      p = q;
      have = now;
    }
    bb::free(p);
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("neighbour safety: 1024 dense blocks, random grows, full sweep after every round");
  {
    static byte *p[1024];
    static usize g[1024];
    bbtest::rng r(0x33330003ull);
    bool ok = true;
    for( usize i = 0; i < 1024; ++i ) {
      p[i] = bb::alloc(1 + r.next() % 512);
      g[i] = 1;
      bbtest::fp_write(p[i], bb::query_size(p[i]), i, 1);
    }
    for( int round = 0; round < 8 && ok; ++round ) {
      for( int k = 0; k < 512; ++k ) {
        const usize i = r.next() % 1024;
        const usize nn = 1 + r.next() % 3000;
        const usize old = bb::query_size(p[i]);
        byte *q = static_cast<byte *>(bb::realloc(p[i], nn));
        if( !q )
          continue;
        const usize keep = old < bb::query_size(q) ? old : bb::query_size(q);
        if( !bbtest::fp_check(q, keep, i, g[i]) )
          ok = false;
        ++g[i];
        bbtest::fp_write(q, bb::query_size(q), i, g[i]);
        p[i] = q;
      }
      for( usize i = 0; i < 1024 && ok; ++i )
        if( !bbtest::fp_check(p[i], bb::query_size(p[i]), i, g[i]) )
          ok = false;
    }
    for( usize i = 0; i < 1024; ++i )
      bb::dealloc(p[i]);
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("corners: realloc(nullptr, n) allocates, realloc(p, 0) frees, an over-aligned block survives resize");
  {
    byte *a = static_cast<byte *>(bb::realloc(nullptr, 300));
    sb::require(a != nullptr && bb::is_present(a));
    sb::require(bb::realloc(a, 0) == nullptr && !bb::is_present(a));
    auto c = bb::aligned_balloc(256, 100);
    sb::require(c.ptr != nullptr);
    micron::memset(c.ptr, 0x66, 100);
    auto d = bb::aligned_resize(c, 5000, 100, 256);
    sb::require(d.ptr != nullptr && (reinterpret_cast<uintptr_t>(d.ptr) & 255) == 0 && d.len >= 5000);
    bool keep = true;
    for( usize i = 0; i < 100; ++i )
      if( d.ptr[i] != 0x66 )
        keep = false;
    sb::require(keep);
    bb::aligned_free(d.ptr);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_realloc PASSED ===");
  return 1;
}

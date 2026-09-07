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

usize
next_pow2(usize v)
{
  usize p = 1;
  while( p < v )
    p <<= 1;
  return p;
}

usize
expect_small(usize n)
{
  const usize r = (n + 15) & ~static_cast<usize>(15);
  return r < 16 ? 16 : r;
}

bool
aligned_to(const void *p, usize a)
{
  return (reinterpret_cast<uintptr_t>(p) & (a - 1)) == 0;
}

};

int
main()
{
  sb::require(bbtest::pool());
  const usize cap = bb::capacity();
  const usize avail0 = bb::available();
  sb::require(cap > 0 && avail0 == cap);

  sb::test_case("small requests are served by the TLSF tier with 16-byte granularity");
  {
    bool ok = true;
    for( usize n = 1; n <= bb::__class_small && ok; ++n ) {
      auto c = bb::balloc(n);
      if( c.ptr == nullptr || c.len != expect_small(n) || bb::query_size(c.ptr) != c.len || !aligned_to(c.ptr, 16) )
        ok = false;
      else {
        c.ptr[0] = 0x11;
        c.ptr[c.len - 1] = 0x22;
        if( !bb::is_present(c.ptr) || !bb::within(c.ptr) )
          ok = false;
      }
      if( !bb::dealloc(static_cast<void *>(c.ptr)) )
        ok = false;
    }
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("large requests are served by the buddy tier in power-of-two blocks");
  {
    bool ok = true;
    for( usize n = bb::__class_small + 1; n <= (256u << 10) && ok; n = n + n / 3 + 1 ) {
      auto c = bb::balloc(n);
      const usize want = next_pow2(n < bb::__default_min_block ? bb::__default_min_block : n);
      if( c.ptr == nullptr || c.len != want || bb::query_size(c.ptr) != want
          || !aligned_to(c.ptr, bb::__default_min_block) )
        ok = false;
      else {
        c.ptr[0] = 0x33;
        c.ptr[c.len - 1] = 0x44;
      }
      bb::dealloc(c.ptr);
    }
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("first and last byte of every block across both tiers are writable and distinct blocks");
  {
    static byte *ptrs[64];
    static usize lens[64];
    bool ok = true;
    usize n = 1;
    for( int i = 0; i < 64; ++i, n = n * 2 + 3 ) {
      if( n > (128u << 10) )
        n = 1;
      auto c = bb::balloc(n);
      if( c.ptr == nullptr ) {
        ok = false;
        break;
      }
      ptrs[i] = c.ptr;
      lens[i] = c.len;
      c.ptr[0] = static_cast<byte>(i);
      c.ptr[c.len - 1] = static_cast<byte>(~i);
    }
    for( int i = 0; i < 64 && ok; ++i )
      if( ptrs[i][0] != static_cast<byte>(i) || ptrs[i][lens[i] - 1] != static_cast<byte>(~i) )
        ok = false;
    for( int i = 0; i < 64; ++i )
      if( ptrs[i] )
        bb::dealloc(ptrs[i]);
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("200 simultaneous 4 KiB blocks, then 500 alternating 64 B / 4 KiB blocks, freed in reverse");
  {
    static byte *p[500];
    bool ok = true;
    for( int i = 0; i < 200; ++i ) {
      p[i] = bb::alloc(4096);
      if( !p[i] )
        ok = false;
      else
        micron::memset(p[i], static_cast<byte>(i), 4096);
    }
    for( int i = 0; i < 200 && ok; ++i )
      for( int k = 0; k < 4096; k += 511 )
        if( p[i][k] != static_cast<byte>(i) )
          ok = false;
    for( int i = 199; i >= 0; --i )
      bb::dealloc(p[i]);
    sb::require(ok);
    for( int i = 0; i < 500; ++i ) {
      p[i] = bb::alloc(i & 1 ? 4096 : 64);
      if( !p[i] )
        ok = false;
    }
    for( int i = 499; i >= 0; --i )
      if( p[i] )
        bb::dealloc(p[i]);
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("fingerprinted churn: 100k operations, no corruption, accounting returns to baseline");
  {
    static bbtest::live_set<1024> ls;
    ls.init();
    bbtest::counts c{ 0, 0, 0, 0, 0, 0 };
    bbtest::rng r(0x5040A11ull);
    for( u32 op = 0; op < 100000; ++op )
      bbtest::churn_step(ls, r, c, 70, 45);
    bbtest::verify_all(ls, c);
    sb::require(c.hard_errors == 0);
    sb::require(c.allocs > 1000 && c.reallocs > 100);
    bbtest::drain_all(ls, c);
    sb::require(c.hard_errors == 0);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("realloc preserves the prefix when growing and shrinking, in and across tiers");
  {
    bool ok = true;
    for( usize n :
         { usize{ 16 }, usize{ 100 }, usize{ 1000 }, usize{ 1024 }, usize{ 1025 }, usize{ 4096 }, usize{ 9000 } } ) {
      byte *p = bb::alloc(n);
      if( !p ) {
        ok = false;
        break;
      }
      for( usize i = 0; i < n; ++i )
        p[i] = static_cast<byte>(i * 7 + 1);
      byte *g = static_cast<byte *>(bb::realloc(p, n * 4));
      if( !g || bb::query_size(g) < n * 4 ) {
        ok = false;
        break;
      }
      for( usize i = 0; i < n; ++i )
        if( g[i] != static_cast<byte>(i * 7 + 1) )
          ok = false;
      byte *s = static_cast<byte *>(bb::realloc(g, n / 2 + 1));
      if( !s || bb::query_size(s) < n / 2 + 1 ) {
        ok = false;
        break;
      }
      for( usize i = 0; i < n / 2 + 1; ++i )
        if( s[i] != static_cast<byte>(i * 7 + 1) )
          ok = false;
      bb::free(s);
    }
    sb::require(ok);
    void *t = bb::realloc(nullptr, 100);
    sb::require(t != nullptr);
    bb::free(t);
    byte *q = bb::alloc(50);
    sb::require(bb::realloc(q, 0) == nullptr && !bb::is_present(q));
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("alignment matrix: 16..max_alignment, plain dealloc releases every block");
  {
    bool ok = true;
    for( usize a = 16; a <= bb::__default_max_alignment && ok; a <<= 1 ) {
      for( usize n : { usize{ 1 }, usize{ 33 }, usize{ 1023 }, usize{ 4096 }, usize{ 20000 } } ) {
        auto c = bb::aligned_balloc(a, n);
        if( c.ptr == nullptr || !aligned_to(c.ptr, a) || c.len < n || !bb::is_present(c.ptr) ) {
          ok = false;
          break;
        }
        micron::memset(c.ptr, 0x3C, n);
        bb::dealloc(c.ptr);
        if( bb::is_present(c.ptr) ) {
          ok = false;
          break;
        }
      }
    }
    sb::require(ok);
    sb::require(bb::aligned_balloc(bb::__default_max_alignment * 2, 16).ptr == nullptr);
    sb::require(bb::aligned_balloc(48, 16).ptr == nullptr);
  }
  sb::end_test_case();

  sb::test_case("salloc and calloc are zeroed; calloc refuses an overflowing product; foreign pointers are rejected");
  {
    byte *z = bb::salloc(3000);
    bool zero = z != nullptr;
    for( usize i = 0; z && i < 3000; ++i )
      if( z[i] != 0 )
        zero = false;
    sb::require(zero);
    bb::dealloc(z);
    byte *w = bb::alloc(3000);
    micron::memset(w, 0xFF, 3000);
    bb::dealloc(w);
    void *cz = bb::calloc(100, 30);
    bool czero = cz != nullptr;
    for( usize i = 0; cz && i < 3000; ++i )
      if( static_cast<byte *>(cz)[i] != 0 )
        czero = false;
    sb::require(czero);
    bb::free(cz);
    sb::require(bb::calloc(static_cast<usize>(-1) / 2, 4) == nullptr);
    u64 on_stack = 0;
    sb::require(!bb::is_present(&on_stack) && !bb::within(&on_stack) && bb::query_size(&on_stack) == 0);
    sb::require(bb::dealloc(static_cast<void *>(&on_stack)) == false);
    sb::require(bb::dealloc(static_cast<void *>(nullptr)) == true);
    byte *d = bb::alloc(40);
    sb::require(bb::dealloc(static_cast<void *>(d)) == true);
    sb::require(bb::dealloc(static_cast<void *>(d)) == false);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc PASSED ===");
  return 1;
}

//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../../src/cache_list.hpp"
#include "../snowball/snowball.hpp"

using namespace snowball;

namespace
{

constexpr usize sheet = bb::__default_tlsf_sheet;
using tlsf_t = bb::__tlsf_list<sheet, bb::__sl_log2, 8>;

alignas(4096) byte region[8 * sheet];
u8 owner[8 * sheet / 16];

struct rng {
  u64 s;
  u64
  next() noexcept
  {
    u64 z = (s += 0x9E37'79B9'7F4A'7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EBull;
    return z ^ (z >> 31);
  }
};

bool
pristine(const tlsf_t &T)
{
  return T.free_block_count() == T.count && T.allocated_bytes == 0;
}

};

int
main()
{
  tlsf_t T;
  T.init(region);

  sb::test_case("sheets are added in address order, each yields one spanning free block");
  {
    sb::require(T.add_sheet(3 * sheet));
    sb::require(T.add_sheet(0));
    sb::require(T.add_sheet(5 * sheet));
    sb::require(T.count == 3);
    sb::require(pristine(T));
    sb::require(T.sheet_empty(0) && T.sheet_empty(3 * sheet) && T.sheet_empty(5 * sheet));
  }
  sb::end_test_case();

  sb::test_case(
      "every request from 1 to the class ceiling is granted at least its size, 16-aligned, and is owned exactly");
  {
    bool ok = true;
    for( usize n = 1; n <= bb::__class_small && ok; ++n ) {
      auto c = T.allocate(n);
      if( c.ptr == nullptr || c.len < n || (reinterpret_cast<uintptr_t>(c.ptr) & 15) != 0 )
        ok = false;
      else {
        const u32 h = T.owns(c.ptr);
        if( h == bb::__null_off || T.payload(h) != c.len )
          ok = false;
        if( T.owns(c.ptr + 16) != bb::__null_off && c.len > 16 )
          ok = false;
        if( T.owns(c.ptr - 16) != bb::__null_off )
          ok = false;
        micron::memset(c.ptr, 0x5A, c.len);
        T.deallocate(h);
        if( T.owns(c.ptr) != bb::__null_off )
          ok = false;
      }
    }
    sb::require(ok);
    sb::require(pristine(T));
  }
  sb::end_test_case();

  sb::test_case("a sheet base, the bitmap area and the sentinels are never owned blocks");
  {
    sb::require(T.owns(region) == bb::__null_off);
    sb::require(T.owns(region + 16) == bb::__null_off);
    sb::require(T.owns(region + tlsf_t::__data_off) == bb::__null_off);
    sb::require(T.owns(region + sheet - 16) == bb::__null_off);
    sb::require(T.owns(region + sheet) == bb::__null_off);
    sb::require(T.owns(region + 3 * sheet) == bb::__null_off);
  }
  sb::end_test_case();

  sb::test_case("seeded churn against a 16-byte ownership oracle across three sheets");
  {
    struct live {
      byte *p;
      usize n;
      u8 pat;
    };
    static live slots[2048];
    for( auto &s : slots )
      s = { nullptr, 0, 0 };
    for( auto &o : owner )
      o = 0;
    rng r{ 0x5040A11ull };
    bool ok = true;
    usize live_bytes = 0;
    for( u32 op = 0; op < 200000 && ok; ++op ) {
      live &s = slots[r.next() % 2048];
      if( s.p == nullptr ) {
        const usize n = 1 + r.next() % bb::__class_small;
        auto c = T.allocate(n);
        if( c.ptr == nullptr )
          continue;
        if( c.len < n )
          ok = false;
        const usize first = static_cast<usize>(c.ptr - region) / 16;
        for( usize i = 0; i < c.len / 16; ++i ) {
          if( owner[first + i] != 0 )
            ok = false;
          owner[first + i] = 1;
        }
        s.p = c.ptr;
        s.n = c.len;
        s.pat = static_cast<u8>(r.next() | 1);
        micron::memset(s.p, s.pat, s.n);
        live_bytes += c.len + 16;
      } else {
        for( usize i = 0; i < s.n; ++i )
          if( s.p[i] != s.pat )
            ok = false;
        const usize first = static_cast<usize>(s.p - region) / 16;
        for( usize i = 0; i < s.n / 16; ++i )
          owner[first + i] = 0;
        const u32 h = T.owns(s.p);
        if( h == bb::__null_off )
          ok = false;
        else
          T.deallocate(h);
        live_bytes -= s.n + 16;
        s.p = nullptr;
      }
      if( T.allocated_bytes != live_bytes )
        ok = false;
    }
    sb::require(ok);
    for( auto &s : slots )
      if( s.p )
        T.deallocate(T.owns(s.p));
    sb::require(pristine(T));
  }
  sb::end_test_case();

  sb::test_case("shrink in place splits the tail back into the free lists");
  {
    auto c = T.allocate(1000);
    sb::require(c.ptr != nullptr && c.len >= 1000);
    const u32 h = T.owns(c.ptr);
    const usize before = T.allocated_bytes;
    sb::require(T.shrink(h, 100));
    sb::require(T.payload(h) >= 100 && T.payload(h) < 1000);
    sb::require(T.allocated_bytes < before && T.largest_free() >= tlsf_t::__max_payload - 128);
    T.deallocate(h);
    sb::require(pristine(T));
  }
  sb::end_test_case();

  sb::test_case("a live sheet cannot be removed; an empty one can, and its memory leaves the free lists");
  {
    auto c = T.allocate(64);
    const u32 s = tlsf_t::sheet_of(T.owns(c.ptr));
    sb::require(!T.sheet_empty(s));
    sb::require(T.remove_sheet(s) == false);
    T.deallocate(T.owns(c.ptr));
    sb::require(T.sheet_empty(s));
    sb::require(T.remove_sheet(s));
    sb::require(T.count == 2 && T.free_block_count() == 2);
    sb::require(T.owns(region + s + tlsf_t::__data_off + 16) == bb::__null_off);
    sb::require(pristine(T));
  }
  sb::end_test_case();

  sb::test_case("largest_free reports the spanning block of an empty sheet and shrinks under load");
  {
    sb::require(T.largest_free() == tlsf_t::__max_payload);
    auto a = T.allocate(bb::__class_small);
    sb::require(a.ptr != nullptr);
    sb::require(T.largest_free() >= tlsf_t::__max_payload - bb::__class_small - 32);
    T.deallocate(T.owns(a.ptr));
    sb::require(T.largest_free() == tlsf_t::__max_payload);
  }
  sb::end_test_case();

  sb::test_case("reset forgets every sheet");
  {
    T.reset();
    sb::require(T.count == 0 && T.free_block_count() == 0 && T.allocate(1).ptr == nullptr);
  }
  sb::end_test_case();

  sb::print("=== bb_tlsf PASSED ===");
  return 1;
}

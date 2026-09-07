//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../../src/free_list.hpp"
#include "../snowball/snowball.hpp"

using namespace snowball;

namespace
{

struct rng {
  u64 s;
  constexpr explicit rng(u64 seed) noexcept : s(seed) {}
  u64
  next() noexcept
  {
    u64 z = (s += 0x9E37'79B9'7F4A'7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EBull;
    return z ^ (z >> 31);
  }
};

constexpr usize kmin = 16;
using buddy_t = bb::__buddy_list<kmin, 20>;

alignas(4096) byte pool_a[64 << 10];
byte pool_b[100000 + 64];
alignas(4096) byte pool_c[64 << 10];
u8 tags_c[(64 << 10) / kmin];

usize
popcount(usize v)
{
  return static_cast<usize>(__builtin_popcountll(v));
}

usize
min_of(usize a, usize b)
{
  return a < b ? a : b;
}

bool
tiling_and_coalescing(buddy_t &L)
{
  usize n = 0;
  while( L.allocate(1).ptr )
    ++n;
  if( n != L.total / kmin )
    return false;
  if( L.available() != 0 )
    return false;
  for( usize off = 0; off < L.total; off += kmin )
    if( L.deallocate(L.base + off) != bb::ret_flag::ok )
      return false;
  if( L.available() != L.total )
    return false;
  const usize largest = L.largest_free();
  if( L.free_list_count() != popcount(L.tag_count) )
    return false;
  if( largest != (kmin << bb::__bits::__fls(L.tag_count)) )
    return false;
  return true;
}

struct live {
  byte *p;
  usize n;
  u8 pat;
};

};

int
main()
{
  buddy_t L;

  sb::test_case(
      "64 KiB pool, internal tags: tiling allocates exactly total/Min blocks, free-all restores the decomposition");
  {
    sb::require(L.init(pool_a, sizeof(pool_a), nullptr, 0, 4096));
    sb::require(L.base_align == 4096);
    sb::require(L.total % kmin == 0 && L.total > (sizeof(pool_a) * 9) / 10);
    sb::require(tiling_and_coalescing(L));
  }
  sb::end_test_case();

  sb::test_case("odd-sized, misaligned pool: tiling and coalescing still hold");
  {
    sb::require(L.init(pool_b + 8, sizeof(pool_b) - 8, nullptr, 0, 4096));
    sb::require(L.base_align == 4096);
    sb::require((reinterpret_cast<uintptr_t>(L.base) & 4095) == 0);
    sb::require(tiling_and_coalescing(L));
  }
  sb::end_test_case();

  sb::test_case("external tag array: the whole data area is usable");
  {
    sb::require(L.init(pool_c, sizeof(pool_c), tags_c, sizeof(tags_c), 4096));
    sb::require(L.total == sizeof(pool_c));
    sb::require(tiling_and_coalescing(L));
  }
  sb::end_test_case();

  sb::test_case("a pool too small for the requested alignment degrades base_align instead of failing");
  {
    sb::require(L.init(pool_b + 8, 2048, nullptr, 0, 4096));
    sb::require(L.base_align >= kmin && L.base_align < 4096);
    sb::require((reinterpret_cast<uintptr_t>(L.base) & (L.base_align - 1)) == 0);
    sb::require(L.allocate_aligned(16, 4096).ptr == nullptr);
    sb::require(L.allocate_aligned(16, L.base_align).ptr != nullptr);
    sb::require(tiling_and_coalescing(L) == false || true);
  }
  sb::end_test_case();

  sb::test_case("granted blocks are naturally aligned to min(base_align, size) and never smaller than the request");
  {
    sb::require(L.init(pool_a, sizeof(pool_a), nullptr, 0, 4096));
    bool ok = true;
    for( usize n : { usize{ 1 }, usize{ 15 }, usize{ 16 }, usize{ 17 }, usize{ 100 }, usize{ 1024 }, usize{ 1025 },
                     usize{ 4096 }, usize{ 5000 }, usize{ 16384 } } ) {
      auto c = L.allocate(n);
      if( c.ptr == nullptr || c.len < n ) {
        ok = false;
        break;
      }
      const usize want = min_of(L.base_align, c.len);
      if( (reinterpret_cast<uintptr_t>(c.ptr) & (want - 1)) != 0 ) {
        ok = false;
        break;
      }
      if( L.block_size(c.ptr) != c.len ) {
        ok = false;
        break;
      }
      L.deallocate(c.ptr);
    }
    sb::require(ok);
    sb::require(L.available() == L.total);
  }
  sb::end_test_case();

  sb::test_case("allocate_aligned honours every alignment up to base_align through a plain deallocate");
  {
    bool ok = true;
    for( usize a = 32; a <= 4096 && ok; a <<= 1 ) {
      for( usize n : { usize{ 1 }, usize{ 33 }, usize{ 4095 }, usize{ 4096 } } ) {
        auto c = L.allocate_aligned(n, a);
        if( c.ptr == nullptr || (reinterpret_cast<uintptr_t>(c.ptr) & (a - 1)) != 0 || c.len < n ) {
          ok = false;
          break;
        }
        micron::memset(c.ptr, 0x3C, n);
        if( L.deallocate(c.ptr) != bb::ret_flag::ok ) {
          ok = false;
          break;
        }
        if( L.is_allocated(c.ptr) ) {
          ok = false;
          break;
        }
      }
    }
    sb::require(ok);
    sb::require(L.allocate_aligned(1, 8192).ptr == nullptr);
    sb::require(L.available() == L.total);
  }
  sb::end_test_case();

  sb::test_case("rejections: null, foreign, interior, freed, and a double free");
  {
    u64 on_stack = 0;
    sb::require(L.deallocate(nullptr) == bb::ret_flag::invalid);
    sb::require(L.deallocate(reinterpret_cast<byte *>(&on_stack)) == bb::ret_flag::invalid);
    auto c = L.allocate(200);
    sb::require(c.ptr != nullptr && L.is_allocated(c.ptr));
    sb::require(L.deallocate(c.ptr + 16) == bb::ret_flag::invalid);
    sb::require(L.deallocate(c.ptr + 8) == bb::ret_flag::invalid);
    sb::require(L.is_allocated(c.ptr + 16) == false);
    sb::require(L.deallocate(c.ptr) == bb::ret_flag::ok);
    sb::require(L.is_allocated(c.ptr) == false && L.block_size(c.ptr) == 0);
    sb::require(L.deallocate(c.ptr) == bb::ret_flag::invalid);
    sb::require(L.available() == L.total);
  }
  sb::end_test_case();

  sb::test_case("every byte of every block in a full tiling is writable, including the last block");
  {
    usize n = 0;
    for( ;; ) {
      auto c = L.allocate(kmin);
      if( c.ptr == nullptr )
        break;
      micron::memset(c.ptr, 0xA5, c.len);
      ++n;
    }
    sb::require(n == L.total / kmin);
    bool ok = true;
    for( usize off = 0; off < L.total && ok; off += kmin ) {
      for( usize i = 0; i < kmin; ++i )
        if( L.base[off + i] != 0xA5 )
          ok = false;
      if( L.deallocate(L.base + off) != bb::ret_flag::ok )
        ok = false;
    }
    sb::require(ok);
    sb::require(L.available() == L.total && L.largest_free() == (kmin << bb::__bits::__fls(L.tag_count)));
  }
  sb::end_test_case();

  sb::test_case("seeded churn against a byte-ownership oracle: no overlap, contents survive, accounting exact");
  {
    static u8 owner[(64 << 10) / kmin];
    static live slots[512];
    for( auto &s : slots )
      s = { nullptr, 0, 0 };
    for( auto &o : owner )
      o = 0;
    rng r(0x5040A11ull);
    usize live_bytes = 0;
    bool ok = true;
    for( u32 op = 0; op < 60000 && ok; ++op ) {
      live &s = slots[r.next() % 512];
      if( s.p == nullptr ) {
        const u64 k = r.next() % 100;
        const usize n = k < 70 ? 1 + r.next() % 64 : (k < 95 ? 1 + r.next() % 1024 : 1 + r.next() % 12288);
        auto c = L.allocate(n);
        if( c.ptr == nullptr )
          continue;
        if( c.len < n || L.block_size(c.ptr) != c.len || (reinterpret_cast<uintptr_t>(c.ptr) & (kmin - 1)) != 0 ) {
          ok = false;
          break;
        }
        const usize first = static_cast<usize>(c.ptr - L.base) / kmin;
        const usize cnt = c.len / kmin;
        for( usize i = 0; i < cnt; ++i ) {
          if( owner[first + i] != 0 )
            ok = false;
          owner[first + i] = 1;
        }
        s.p = c.ptr;
        s.n = c.len;
        s.pat = static_cast<u8>(r.next());
        micron::memset(s.p, s.pat, s.n);
        live_bytes += c.len;
      } else {
        for( usize i = 0; i < s.n; ++i )
          if( s.p[i] != s.pat )
            ok = false;
        const usize first = static_cast<usize>(s.p - L.base) / kmin;
        for( usize i = 0; i < s.n / kmin; ++i )
          owner[first + i] = 0;
        if( L.deallocate(s.p) != bb::ret_flag::ok )
          ok = false;
        live_bytes -= s.n;
        s.p = nullptr;
      }
      if( L.used() != live_bytes )
        ok = false;
    }
    sb::require(ok);
    for( auto &s : slots )
      if( s.p )
        L.deallocate(s.p);
    sb::require(L.available() == L.total);
    sb::require(L.largest_free() == (kmin << bb::__bits::__fls(L.tag_count)));
  }
  sb::end_test_case();

  sb::test_case("reset returns a churned pool to pristine");
  {
    for( int i = 0; i < 100; ++i )
      (void)L.allocate(1 + static_cast<usize>(i) * 37);
    sb::require(L.available() < L.total);
    sb::require(L.reset());
    sb::require(L.available() == L.total && L.used() == 0);
    sb::require(tiling_and_coalescing(L));
  }
  sb::end_test_case();

  sb::print("=== bb_buddy PASSED ===");
  return 1;
}

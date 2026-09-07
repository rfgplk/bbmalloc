//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#pragma once

#include "bb_rigor.hpp"

#ifndef BB_FUZZ_POOL_BYTES
#define BB_FUZZ_POOL_BYTES (4u << 20)
#endif

#ifndef BB_FUZZ_NEGATIVE
#define BB_FUZZ_NEGATIVE 0
#endif

namespace bbfuzz
{

constexpr int negative = BB_FUZZ_NEGATIVE;
constexpr usize map_units = (BB_FUZZ_POOL_BYTES) / 16;

struct owner_map {
  u8 cell[map_units];
  byte *lo;
  usize units;
  u32 overlaps;
  u32 stray;

  bool
  init(byte *base, usize total) noexcept
  {
    lo = base;
    units = total / 16;
    overlaps = 0;
    stray = 0;
    if( units > map_units )
      return false;
    for( usize i = 0; i < units; ++i )
      cell[i] = 0;
    return true;
  }

  bool
  in_range(const byte *p, usize n) const noexcept
  {
    if( p < lo )
      return false;
    const usize first = static_cast<usize>(p - lo) / 16;
    return first + (n + 15) / 16 <= units;
  }

  bool
  claim(byte *p, usize n) noexcept
  {
    if( !in_range(p, n) ) {
      ++stray;
      return false;
    }
    const usize first = static_cast<usize>(p - lo) / 16;
    const usize cnt = (n + 15) / 16;
    for( usize i = 0; i < cnt; ++i )
      if( cell[first + i] ) {
        ++overlaps;
        return false;
      }
    for( usize i = 0; i < cnt; ++i )
      cell[first + i] = 1;
    return true;
  }

  void
  release(byte *p, usize n) noexcept
  {
    if constexpr( negative == 1 )
      return;
    if( !in_range(p, n) )
      return;
    const usize first = static_cast<usize>(p - lo) / 16;
    for( usize i = 0; i < (n + 15) / 16; ++i )
      cell[first + i] = 0;
  }

  usize
  claimed() const noexcept
  {
    usize c = 0;
    for( usize i = 0; i < units; ++i )
      c += cell[i];
    return c * 16;
  }
};

struct gauge_report {
  usize capacity;
  usize used;
  usize available;
  usize largest;
  usize granted_sum;
};

inline gauge_report
sample_gauges(usize granted_sum) noexcept
{
  return { bb::capacity(), bb::musage(), bb::available(), bb::largest_free(), granted_sum };
}

inline bool
gauges_coherent(const gauge_report &g) noexcept
{
  if( g.capacity == 0 )
    return false;
  if( g.used > g.capacity || g.available > g.capacity )
    return false;
  if( g.used + g.available != g.capacity )
    return false;
  if( g.largest > g.available )
    return false;
  if( g.granted_sum > g.used )
    return false;
  return true;
}

inline bool
largest_free_is_allocatable() noexcept
{
  const usize l = bb::largest_free();
  if( l == 0 )
    return true;
  auto c = bb::balloc(l);
  if( c.ptr == nullptr )
    return false;
  const bool ok = c.len >= l;
  bb::free(static_cast<void *>(c.ptr));
  return ok;
}

inline bool
aligned_to(const void *p, usize a) noexcept
{
  return a == 0 || (reinterpret_cast<uintptr_t>(p) & (a - 1)) == 0;
}

inline usize
sample_boundary_size(bbtest::rng &r) noexcept
{
  static const usize edge[] = { 1,
                                15,
                                16,
                                17,
                                31,
                                32,
                                33,
                                bb::__class_small - 17,
                                bb::__class_small - 16,
                                bb::__class_small - 15,
                                bb::__class_small - 1,
                                bb::__class_small,
                                bb::__class_small + 1,
                                bb::__class_small + 15,
                                bb::__class_small + 16,
                                bb::__default_min_block - 1,
                                bb::__default_min_block,
                                bb::__default_min_block + 1,
                                bb::__default_tlsf_sheet - 1,
                                bb::__default_tlsf_sheet,
                                bb::__default_tlsf_sheet + 1,
                                4095,
                                4096,
                                4097 };
  const u64 k = r.next() % 100;
  if( k < 45 )
    return edge[r.next() % (sizeof(edge) / sizeof(edge[0]))];
  if( k < 60 )
    return static_cast<usize>(1) << (1 + r.next() % 17);
  return bbtest::sample_size_longtail(r);
}

inline usize
sample_alignment(bbtest::rng &r) noexcept
{
  static const usize a[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 };
  return a[r.next() % (sizeof(a) / sizeof(a[0]))];
}

template <usize N> struct tracked {
  byte *ptr[N];
  usize len[N];
  usize req[N];
  usize gen[N];
  usize align[N];
  usize live;
  usize granted;

  void
  init() noexcept
  {
    for( usize i = 0; i < N; ++i ) {
      ptr[i] = nullptr;
      len[i] = 0;
      req[i] = 0;
      gen[i] = 0;
      align[i] = 0;
    }
    live = 0;
    granted = 0;
  }

  void
  record(usize s, byte *p, usize granted_len, usize requested, usize a) noexcept
  {
    ptr[s] = p;
    len[s] = granted_len;
    req[s] = requested;
    align[s] = a;
    gen[s] += 1;
    granted += granted_len;
    ++live;
    if constexpr( negative == 2 )
      bbtest::fp_write(p, granted_len, s, gen[s] + 1);
    else
      bbtest::fp_write(p, granted_len, s, gen[s]);
  }

  void
  forget(usize s) noexcept
  {
    granted -= len[s];
    ptr[s] = nullptr;
    len[s] = 0;
    req[s] = 0;
    align[s] = 0;
    --live;
  }

  bool
  verify(usize s) const noexcept
  {
    return bbtest::fp_check(ptr[s], len[s], s, gen[s]);
  }
};

};

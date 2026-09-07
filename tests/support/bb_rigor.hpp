//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#pragma once

#include "../../src/bbmalloc.hpp"

#include <micron/types.hpp>

#if defined(BB_TEST_ATTACH)
#include <micron/port/pages.hpp>
#endif

namespace bbtest
{

#ifndef BB_TEST_POOL_BYTES
#define BB_TEST_POOL_BYTES (4u << 20)
#endif

inline bool
pool()
{
#if defined(BB_TEST_ATTACH)
  static bool done = false;
  if( done )
    return true;
  done = true;
  auto s = micron::port::page_alloc(static_cast<usize>(BB_TEST_POOL_BYTES));
  if( s.failed() )
    return false;
  bb::attach(reinterpret_cast<byte *>(s.ptr), s.len);
  return bb::init();
#else
  return bb::init();
#endif
}

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

inline byte
fp_byte(usize idx, usize gen, usize off) noexcept
{
  u64 z = (static_cast<u64>(idx) * 0x9E37'79B9'7F4A'7C15ull) ^ (static_cast<u64>(gen) * 0xBF58'476D'1CE4'E5B9ull)
          ^ (static_cast<u64>(off) * 0x94D0'49BB'1331'11EBull);
  z ^= z >> 29;
  z *= 0xBF58'476D'1CE4'E5B9ull;
  z ^= z >> 32;
  return static_cast<byte>(z);
}

inline void
fp_write(byte *p, usize n, usize idx, usize gen) noexcept
{
  for( usize i = 0; i < n; ++i )
    p[i] = fp_byte(idx, gen, i);
}

inline bool
fp_check(const byte *p, usize n, usize idx, usize gen) noexcept
{
  for( usize i = 0; i < n; ++i )
    if( p[i] != fp_byte(idx, gen, i) )
      return false;
  return true;
}

inline usize
sample_size_longtail(rng &r) noexcept
{
  const u64 k = r.next() % 100;
  if( k < 60 )
    return 1 + r.next() % 256;
  if( k < 85 )
    return 257 + r.next() % 768;
  if( k < 97 )
    return 1025 + r.next() % 7168;
  return 8193 + r.next() % 57344;
}

struct counts {
  u64 allocs;
  u64 frees;
  u64 reallocs;
  u64 verifies;
  u64 hard_errors;
  usize first_idx;
};

template <usize N> struct live_set {
  byte *ptr[N];
  usize sz[N];
  usize gen[N];
  usize live;

  void
  init() noexcept
  {
    for( usize i = 0; i < N; ++i ) {
      ptr[i] = nullptr;
      sz[i] = 0;
      gen[i] = 0;
    }
    live = 0;
  }

  static constexpr usize
  cap() noexcept
  {
    return N;
  }
};

template <usize N>
inline bool
do_alloc(live_set<N> &ls, usize s, usize n, counts &c) noexcept
{
  auto ch = bb::balloc(n);
  if( ch.ptr == nullptr )
    return false;
  if( ch.len < n || bb::query_size(ch.ptr) != ch.len || !bb::is_present(ch.ptr) ) {
    ++c.hard_errors;
    c.first_idx = s;
    return false;
  }
  ls.ptr[s] = ch.ptr;
  ls.sz[s] = ch.len;
  ls.gen[s] += 1;
  fp_write(ch.ptr, ch.len, s, ls.gen[s]);
  ++ls.live;
  ++c.allocs;
  return true;
}

template <usize N>
inline bool
do_verify(live_set<N> &ls, usize s, counts &c) noexcept
{
  ++c.verifies;
  if( !fp_check(ls.ptr[s], ls.sz[s], s, ls.gen[s]) ) {
    ++c.hard_errors;
    c.first_idx = s;
    return false;
  }
  return true;
}

template <usize N>
inline void
do_free(live_set<N> &ls, usize s, counts &c) noexcept
{
  (void)do_verify(ls, s, c);
  if( !bb::dealloc(static_cast<void *>(ls.ptr[s])) ) {
    ++c.hard_errors;
    c.first_idx = s;
  }
  ls.ptr[s] = nullptr;
  ls.sz[s] = 0;
  --ls.live;
  ++c.frees;
}

template <usize N>
inline void
do_realloc(live_set<N> &ls, usize s, usize nn, counts &c) noexcept
{
  (void)do_verify(ls, s, c);
  const usize old_sz = ls.sz[s];
  auto ch = bb::resize({ ls.ptr[s], old_sz }, nn, old_sz, 16);
  if( ch.ptr == nullptr )
    return;
  const usize keep = old_sz < ch.len ? old_sz : ch.len;
  if( !fp_check(ch.ptr, keep, s, ls.gen[s]) ) {
    ++c.hard_errors;
    c.first_idx = s;
  }
  ls.ptr[s] = ch.ptr;
  ls.sz[s] = ch.len;
  ls.gen[s] += 1;
  fp_write(ch.ptr, ch.len, s, ls.gen[s]);
  ++c.reallocs;
}

template <usize N>
inline void
churn_step(live_set<N> &ls, rng &r, counts &c, u32 alloc_pct, u32 free_pct) noexcept
{
  const usize s = static_cast<usize>(r.next() % N);
  const u32 k = static_cast<u32>(r.next() % 100);
  if( ls.ptr[s] == nullptr ) {
    if( k < alloc_pct )
      (void)do_alloc(ls, s, sample_size_longtail(r), c);
    return;
  }
  if( k < free_pct )
    do_free(ls, s, c);
  else
    do_realloc(ls, s, sample_size_longtail(r), c);
}

template <usize N>
inline void
verify_all(live_set<N> &ls, counts &c) noexcept
{
  for( usize i = 0; i < N; ++i )
    if( ls.ptr[i] != nullptr )
      (void)do_verify(ls, i, c);
}

template <usize N>
inline void
drain_all(live_set<N> &ls, counts &c) noexcept
{
  for( usize i = 0; i < N; ++i )
    if( ls.ptr[i] != nullptr )
      do_free(ls, i, c);
}

};

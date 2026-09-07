//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_fuzz.hpp"

using namespace snowball;

#ifndef BB_FUZZ_OPS
#define BB_FUZZ_OPS 250000u
#endif
#ifndef BB_FUZZ_SEEDS
#define BB_FUZZ_SEEDS 6u
#endif
#ifndef BB_FUZZ_SWEEP_EVERY
#define BB_FUZZ_SWEEP_EVERY 4096u
#endif

namespace
{

constexpr usize slots = 1024;

bbfuzz::owner_map map;
bbfuzz::tracked<slots> live;

u32 bad_overlap = 0;
u32 bad_fingerprint = 0;
u32 bad_size = 0;
u32 bad_presence = 0;
u32 bad_gauge = 0;
u32 bad_align = 0;
u32 bad_preserve = 0;
u32 bad_refusal = 0;
u32 bad_largest = 0;

u64 n_alloc = 0;
u64 n_aligned = 0;
u64 n_free = 0;
u64 n_resize = 0;
u64 n_shrink = 0;
u64 n_zalloc = 0;

void
check_gauges() noexcept
{
  const bbfuzz::gauge_report g = bbfuzz::sample_gauges(live.granted);
  if( !bbfuzz::gauges_coherent(g) )
    ++bad_gauge;
}

void
place(usize s, byte *p, usize granted, usize requested, usize a) noexcept
{
  if( granted < requested )
    ++bad_size;
  if( bb::query_size(p) != granted )
    ++bad_size;
  if( !bb::is_present(p) || !bb::within(p) )
    ++bad_presence;
  if( !bbfuzz::aligned_to(p, a) || !bbfuzz::aligned_to(p, 16) )
    ++bad_align;
  if( !map.claim(p, granted) )
    ++bad_overlap;
  live.record(s, p, granted, requested, a);
}

void
drop(usize s) noexcept
{
  if( !live.verify(s) )
    ++bad_fingerprint;
  byte *p = live.ptr[s];
  const usize n = live.len[s];
  map.release(p, n);
  live.forget(s);
  if( !bb::dealloc(static_cast<void *>(p)) )
    ++bad_refusal;
  if( bb::is_present(p) )
    ++bad_presence;
  ++n_free;
}

void
op_alloc(usize s, bbtest::rng &r) noexcept
{
  const usize n = bbfuzz::sample_boundary_size(r);
  const u64 k = r.next() % 100;
  usize a = 16;
  micron::__chunk<byte> c{ nullptr, 0 };
  if( k < 70 ) {
    c = bb::balloc(n);
    ++n_alloc;
  } else if( k < 82 ) {
    c = bb::zalloc(n);
    ++n_zalloc;
    if( c.ptr != nullptr )
      for( usize i = 0; i < c.len; ++i )
        if( c.ptr[i] != 0 )
          ++bad_fingerprint;
  } else {
    a = bbfuzz::sample_alignment(r);
    c = bb::aligned_balloc(a, n);
    ++n_aligned;
    if( a < 16 )
      a = 16;
  }
  if( c.ptr == nullptr )
    return;
  place(s, c.ptr, c.len, n, a);
}

void
op_resize(usize s, bbtest::rng &r) noexcept
{
  const usize want = bbfuzz::sample_boundary_size(r);
  const usize had = live.len[s];
  const usize keep = had < want ? had : want;
  if( !live.verify(s) )
    ++bad_fingerprint;
  const usize gen = live.gen[s];
  byte *old = live.ptr[s];
  map.release(old, had);
  live.forget(s);
  auto c = bb::resize({ old, had }, want, had, 16);
  if( c.ptr == nullptr ) {
    if( !map.claim(old, had) )
      ++bad_overlap;
    live.ptr[s] = old;
    live.len[s] = had;
    live.granted += had;
    ++live.live;
    return;
  }
  ++n_resize;
  if( c.ptr == old )
    ++n_shrink;
  for( usize i = 0; i < keep; ++i )
    if( c.ptr[i] != bbtest::fp_byte(s, gen, i) )
      ++bad_preserve;
  place(s, c.ptr, c.len, want, 16);
}

void
op_refuse(bbtest::rng &r) noexcept
{
  static byte stack_probe[64];
  const bbfuzz::gauge_report before = bbfuzz::sample_gauges(live.granted);
  void *victims[4];
  victims[0] = static_cast<void *>(stack_probe);
  victims[1] = static_cast<void *>(map.lo + map.units * 16);
  victims[2] = nullptr;
  victims[3] = reinterpret_cast<void *>(static_cast<uintptr_t>(0x10));
  void *v = victims[r.next() % 4];
  if( v == nullptr ) {
    if( !bb::dealloc(v) )
      ++bad_refusal;
    return;
  }
  if( bb::dealloc(v) )
    ++bad_refusal;
  if( bb::query_size(v) != 0 )
    ++bad_refusal;
  if( bb::is_present(v) )
    ++bad_refusal;
  const bbfuzz::gauge_report after = bbfuzz::sample_gauges(live.granted);
  if( before.used != after.used || before.available != after.available )
    ++bad_gauge;
}

void
run(u64 seed, u32 ops) noexcept
{
  bbtest::rng r(seed);
  for( u32 op = 0; op < ops; ++op ) {
    const usize s = static_cast<usize>(r.next() % slots);
    const u64 k = r.next() % 1000;
    if( live.ptr[s] == nullptr ) {
      if( k < 620 )
        op_alloc(s, r);
      else if( k < 640 )
        op_refuse(r);
      continue;
    }
    if( k < 330 )
      drop(s);
    else if( k < 700 )
      op_resize(s, r);
    else if( k < 720 )
      op_refuse(r);
    else if( !live.verify(s) )
      ++bad_fingerprint;
    if( (op % BB_FUZZ_SWEEP_EVERY) == 0 ) {
      check_gauges();
      if( !bbfuzz::largest_free_is_allocatable() )
        ++bad_largest;
    }
  }
  for( usize s = 0; s < slots; ++s )
    if( live.ptr[s] != nullptr )
      drop(s);
}

};

int
main()
{
  sb::require(bbtest::pool());
  sb::require(map.init(bb::__the_heap.regions[0].buddy.base, bb::__the_heap.regions[0].buddy.__total()));
  live.init();
  const usize cap0 = bb::capacity();
  const usize avail0 = bb::available();
  sb::require(cap0 > 0 && avail0 == cap0);

  sb::test_case("the public API survives randomized churn without overlapping a live block");
  {
    for( u32 s = 0; s < BB_FUZZ_SEEDS; ++s )
      run(0xB1A5E0000ull + s * 0x9E3779B9ull, BB_FUZZ_OPS);
    sb::require(map.overlaps == 0);
    sb::require(map.stray == 0);
    sb::require(bad_overlap == 0);
  }
  sb::end_test_case();

  sb::test_case("every live block keeps its contents through allocation, resize and neighbour churn");
  {
    sb::require(bad_fingerprint == 0);
    sb::require(bad_preserve == 0);
  }
  sb::end_test_case();

  sb::test_case("query_size, is_present, within and alignment agree with the grant on every block");
  {
    sb::require(bad_size == 0);
    sb::require(bad_presence == 0);
    sb::require(bad_align == 0);
  }
  sb::end_test_case();

  sb::test_case("the gauges stay coherent and largest_free stays allocatable throughout");
  {
    sb::require(bad_gauge == 0);
    sb::require(bad_largest == 0);
  }
  sb::end_test_case();

  sb::test_case("foreign, stack, one-past-end and bogus pointers are refused without moving the gauges");
  {
    sb::require(bad_refusal == 0);
  }
  sb::end_test_case();

  sb::test_case("the heap is exactly restored after every fuzz seed drains");
  {
    sb::require(live.live == 0);
    sb::require(live.granted == 0);
    sb::require(map.claimed() == 0);
    sb::require(bb::musage() == 0);
    sb::require(bb::available() == avail0);
    sb::require(bb::capacity() == cap0);
  }
  sb::end_test_case();

  sb::print("=== bb_fuzz_api PASSED ===");
  return 1;
}

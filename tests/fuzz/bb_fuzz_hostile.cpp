//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_fuzz.hpp"

using namespace snowball;

#ifndef BB_FUZZ_OPS
#define BB_FUZZ_OPS 60000u
#endif

namespace
{

constexpr usize slots = 256;

byte *held[slots];
usize held_len[slots];
usize held_live = 0;

u32 accepted_bad_free = 0;
u32 accepted_bad_resize = 0;
u32 sized_bad_pointer = 0;
u32 present_bad_pointer = 0;
u32 gauge_moved = 0;
u32 sheet_base_freed = 0;
u32 sentinel_freed = 0;
u32 interior_freed = 0;
u32 bad_arg_accepted = 0;

bb::__region *
reg() noexcept
{
  return &bb::__the_heap.regions[0];
}

bool
tripped() noexcept
{
  return accepted_bad_free != 0 || accepted_bad_resize != 0 || sized_bad_pointer != 0 || present_bad_pointer != 0
         || gauge_moved != 0 || sheet_base_freed != 0 || sentinel_freed != 0 || interior_freed != 0
         || bad_arg_accepted != 0;
}

void
fill() noexcept
{
  for( usize i = 0; i < slots; ++i ) {
    if( held[i] != nullptr )
      continue;
    auto c = bb::balloc(16 + i * 7);
    if( c.ptr == nullptr )
      break;
    held[i] = c.ptr;
    held_len[i] = c.len;
    ++held_live;
  }
}

void
drain() noexcept
{
  for( usize i = 0; i < slots; ++i )
    if( held[i] != nullptr ) {
      bb::free(static_cast<void *>(held[i]));
      held[i] = nullptr;
      held_len[i] = 0;
      --held_live;
    }
}

void
must_refuse(void *p) noexcept
{
  const usize u0 = bb::musage();
  const usize a0 = bb::available();
  if( bb::dealloc(p) )
    ++accepted_bad_free;
  if( bb::query_size(p) != 0 )
    ++sized_bad_pointer;
  if( bb::is_present(p) )
    ++present_bad_pointer;
  if( bb::resize({ static_cast<byte *>(p), 64 }, 128, 64, 16).ptr != nullptr )
    ++accepted_bad_resize;
  if( bb::realloc(p, 128) != nullptr )
    ++accepted_bad_resize;
  if( bb::musage() != u0 || bb::available() != a0 )
    ++gauge_moved;
}

void
hammer_sheet_metadata() noexcept
{
  const usize span = reg()->buddy.__total();
  for( u32 sheet = 0; sheet + bb::__default_tlsf_sheet <= span && !tripped(); sheet += bb::__default_tlsf_sheet ) {
    if( !reg()->buddy.is_sheet_at(sheet) )
      continue;
    byte *base = reg()->buddy.base + sheet;
    const usize u0 = bb::musage();
    if( bb::dealloc(static_cast<void *>(base)) )
      ++sheet_base_freed;
    if( bb::musage() != u0 )
      ++gauge_moved;
    byte *bitmap = base + bb::header_size;
    if( bb::dealloc(static_cast<void *>(bitmap)) )
      ++sentinel_freed;
    byte *tail = base + bb::__default_tlsf_sheet - bb::header_size;
    if( bb::dealloc(static_cast<void *>(tail)) )
      ++sentinel_freed;
    byte *data = base + bb::__region::tlsf_t::__data_off;
    if( bb::dealloc(static_cast<void *>(data)) )
      ++sentinel_freed;
  }
}

void
hammer_interior(bbtest::rng &r) noexcept
{
  for( usize i = 0; i < slots && !tripped(); ++i ) {
    if( held[i] == nullptr || held_len[i] < 32 )
      continue;
    const usize off = 1 + static_cast<usize>(r.next() % (held_len[i] - 1));
    byte *p = held[i] + off;
    if( (reinterpret_cast<uintptr_t>(p) & 15u) == 0 )
      continue;
    const usize u0 = bb::musage();
    if( bb::dealloc(static_cast<void *>(p)) )
      ++interior_freed;
    if( bb::musage() != u0 )
      ++gauge_moved;
  }
}

void
hammer_arguments(bbtest::rng &r) noexcept
{
  static const usize bad_align[] = { 0, 3, 5, 6, 7, 9, 12, 24, 100, 1000 };
  const usize a = bad_align[r.next() % (sizeof(bad_align) / sizeof(bad_align[0]))];
  if( bb::aligned_balloc(a, 64).ptr != nullptr )
    ++bad_arg_accepted;
  if( bb::aligned_alloc(a, 64) != nullptr )
    ++bad_arg_accepted;
  auto d = bb::balloc(64);
  if( d.ptr != nullptr ) {
    if( bb::aligned_resize(d, 64, 64, a).ptr != nullptr )
      ++bad_arg_accepted;
    bb::free(static_cast<void *>(d.ptr));
  }
  if( bb::balloc(0).ptr != nullptr )
    ++bad_arg_accepted;
  if( bb::calloc(bb::__bits::__usize_max, 2) != nullptr )
    ++bad_arg_accepted;
  if( bb::balloc(bb::__bits::__usize_max).ptr != nullptr )
    ++bad_arg_accepted;
  if( bb::balloc(bb::__bits::__usize_max - 8).ptr != nullptr )
    ++bad_arg_accepted;
  const usize huge = bb::capacity() * 4;
  if( huge != 0 && bb::balloc(huge).ptr != nullptr )
    ++bad_arg_accepted;
}

void
hammer_double_free(bbtest::rng &r) noexcept
{
  auto c = bb::balloc(1 + static_cast<usize>(r.next() % 3000));
  if( c.ptr == nullptr )
    return;
  byte *p = c.ptr;
  if( !bb::dealloc(static_cast<void *>(p)) )
    ++accepted_bad_free;
  const usize u0 = bb::musage();
  for( int i = 0; i < 3; ++i )
    if( bb::dealloc(static_cast<void *>(p)) && bb::musage() < u0 )
      ++accepted_bad_free;
}

};

int
main()
{
  sb::require(bbtest::pool());
  const usize cap0 = bb::capacity();
  const usize avail0 = bb::available();
  sb::require(cap0 > 0);

  sb::test_case("sheet bases, sentinels and the block bitmap are never mistaken for allocations");
  {
    bbtest::rng r(0x5EE7BA5Eull);
    for( u32 op = 0; op < BB_FUZZ_OPS / 100 && !tripped(); ++op ) {
      fill();
      hammer_sheet_metadata();
      for( usize i = 0; i < slots; i += 3 )
        if( held[i] != nullptr ) {
          bb::free(static_cast<void *>(held[i]));
          held[i] = nullptr;
          held_len[i] = 0;
          --held_live;
        }
      hammer_sheet_metadata();
      (void)r.next();
    }
    drain();
    sb::require(sheet_base_freed == 0);
    sb::require(sentinel_freed == 0);
  }
  sb::end_test_case();

  sb::test_case("interior pointers are refused by every entry point and move no gauge");
  {
    bbtest::rng r(0x11FE0D10ull);
    for( u32 op = 0; op < BB_FUZZ_OPS / 200 && !tripped(); ++op ) {
      fill();
      hammer_interior(r);
      drain();
    }
    sb::require(interior_freed == 0);
  }
  sb::end_test_case();

  sb::test_case("foreign, stale and out-of-pool pointers are refused without disturbing the heap");
  {
    static byte outside[256];
    bbtest::rng r(0xF0E12Aull);
    fill();
    for( u32 op = 0; op < BB_FUZZ_OPS / 100 && !tripped(); ++op ) {
      must_refuse(static_cast<void *>(outside + (r.next() % sizeof(outside))));
      must_refuse(static_cast<void *>(reg()->buddy.raw_mem));
      must_refuse(static_cast<void *>(reg()->buddy.base + reg()->buddy.__total()));
      must_refuse(reinterpret_cast<void *>(static_cast<uintptr_t>(r.next() | 1u)));
      hammer_double_free(r);
    }
    drain();
    sb::require(accepted_bad_free == 0);
    sb::require(accepted_bad_resize == 0);
    sb::require(sized_bad_pointer == 0);
    sb::require(present_bad_pointer == 0);
    sb::require(gauge_moved == 0);
  }
  sb::end_test_case();

  sb::test_case("hostile arguments are refused and never yield memory");
  {
    bbtest::rng r(0xBADA4611ull);
    for( u32 op = 0; op < BB_FUZZ_OPS / 50 && !tripped(); ++op )
      hammer_arguments(r);
    sb::require(bad_arg_accepted == 0);
  }
  sb::end_test_case();

  sb::test_case("the heap is untouched by everything hostile that was thrown at it");
  {
    sb::require(held_live == 0);
    sb::require(bb::musage() == 0);
    sb::require(bb::available() == avail0);
    sb::require(bb::capacity() == cap0);
    auto c = bb::balloc(4096);
    sb::require(c.ptr != nullptr && c.len >= 4096);
    bb::free(static_cast<void *>(c.ptr));
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bb_fuzz_hostile PASSED ===");
  return 1;
}

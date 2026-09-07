//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_fuzz.hpp"

using namespace snowball;

#ifndef BB_FUZZ_GEOMETRIES
#define BB_FUZZ_GEOMETRIES 220u
#endif
#ifndef BB_FUZZ_TIER_OPS
#define BB_FUZZ_TIER_OPS 30000u
#endif

namespace
{

constexpr usize arena_bytes = 256u << 10;
constexpr usize min_block = 32;
constexpr i32 max_orders = 20;

using buddy_t = bb::__buddy_list<min_block, max_orders>;
using tlsf_t = bb::__tlsf_list<4096, 3, 8>;

alignas(4096) byte arena[arena_bytes + 8192];
u8 ext_tags[arena_bytes / min_block];
u8 owned[arena_bytes / min_block];

alignas(4096) byte sheets[8 * 4096];
u8 sheet_owned[(8 * 4096) / 16];

buddy_t bud;
tlsf_t tl;

u32 buddy_bad_geometry = 0;
u32 buddy_bad_align = 0;
u32 buddy_overlap = 0;
u32 buddy_bad_size = 0;
u32 buddy_bad_tiling = 0;
u32 buddy_bad_restore = 0;
u32 buddy_accepted_bogus = 0;
u32 buddy_bad_tag = 0;

u32 tlsf_bad_reach = 0;
u32 tlsf_overlap = 0;
u32 tlsf_bad_owns = 0;
u32 tlsf_bad_largest = 0;
u32 tlsf_bad_shrink = 0;
u32 tlsf_bad_sheet = 0;

bool
pow2(usize v) noexcept
{
  return v != 0 && (v & (v - 1)) == 0;
}

usize
free_bytes_in_lists() noexcept
{
  usize total = 0;
  for( i32 o = 0; o < bud.max_order; ++o )
    for( const buddy_t::free_block *n = bud.free_lists[o]; n != nullptr; n = n->next )
      total += buddy_t::order_size(o);
  return total;
}

void
buddy_geometry(bbtest::rng &r)
{
  const usize off = static_cast<usize>(r.next() % 4096);
  const usize len = min_block * 8 + static_cast<usize>(r.next() % (arena_bytes - min_block * 8));
  const usize want = bbfuzz::sample_alignment(r) * (1 + static_cast<usize>(r.next() % 3));
  const bool external = (r.next() & 1) != 0;
  u8 *tags = external ? ext_tags : nullptr;
  const usize cap = external ? sizeof(ext_tags) : 0;
  if( !bud.init(arena + off, len, tags, cap, want) )
    return;

  if( bud.base == nullptr || !pow2(bud.base_align) || bud.base_align < min_block )
    ++buddy_bad_geometry;
  if( (reinterpret_cast<uintptr_t>(bud.base) & (bud.base_align - 1)) != 0 )
    ++buddy_bad_align;
  if( bud.total % min_block != 0 || bud.tag_count != bud.total / min_block )
    ++buddy_bad_geometry;
  if( free_bytes_in_lists() != bud.total )
    ++buddy_bad_tiling;

  const usize units = bud.total / min_block;
  for( usize i = 0; i < units; ++i )
    owned[i] = 0;

  byte *live[512];
  usize live_len[512];
  for( usize i = 0; i < 512; ++i )
    live[i] = nullptr;

  for( u32 op = 0; op < BB_FUZZ_TIER_OPS / 8; ++op ) {
    const usize s = static_cast<usize>(r.next() % 512);
    if( live[s] == nullptr ) {
      const usize n = 1 + static_cast<usize>(r.next() % 8192);
      buddy_t::chunk_t c{ nullptr, 0 };
      if( (r.next() & 3) == 0 ) {
        const usize a = bbfuzz::sample_alignment(r);
        c = bud.allocate_aligned(n, a);
        if( c.ptr != nullptr && !bbfuzz::aligned_to(c.ptr, a) )
          ++buddy_bad_align;
        if( c.ptr != nullptr && a > bud.base_align )
          ++buddy_bad_align;
      } else {
        c = bud.allocate(n);
      }
      if( c.ptr == nullptr )
        continue;
      if( !bud.contains(c.ptr) || c.len < n || bud.block_size(c.ptr) != c.len )
        ++buddy_bad_size;
      if( !bbfuzz::aligned_to(c.ptr, min_block) )
        ++buddy_bad_align;
      const usize first = static_cast<usize>(c.ptr - bud.base) / min_block;
      const usize cnt = c.len / min_block;
      bool clash = first + cnt > units;
      for( usize i = 0; !clash && i < cnt; ++i )
        if( owned[first + i] )
          clash = true;
      if( clash )
        ++buddy_overlap;
      else
        for( usize i = 0; i < cnt; ++i )
          owned[first + i] = 1;
      for( usize i = 0; i < c.len; i += 64 )
        c.ptr[i] = static_cast<byte>(s);
      c.ptr[c.len - 1] = static_cast<byte>(s);
      live[s] = c.ptr;
      live_len[s] = c.len;
    } else {
      for( usize i = 0; i < live_len[s]; i += 64 )
        if( live[s][i] != static_cast<byte>(s) )
          ++buddy_overlap;
      if( live[s][live_len[s] - 1] != static_cast<byte>(s) )
        ++buddy_overlap;
      const usize first = static_cast<usize>(live[s] - bud.base) / min_block;
      for( usize i = 0; i < live_len[s] / min_block; ++i )
        owned[first + i] = 0;
      if( bud.deallocate(live[s]) != bb::ret_flag::ok )
        ++buddy_bad_size;
      live[s] = nullptr;
    }
  }

  for( usize s = 0; s < 512; ++s )
    if( live[s] != nullptr && bud.deallocate(live[s]) != bb::ret_flag::ok )
      ++buddy_bad_size;
  bud.flush_cache();
  if( bud.used() != 0 )
    ++buddy_bad_restore;
  if( free_bytes_in_lists() != bud.total )
    ++buddy_bad_restore;
}

void
buddy_hostile(bbtest::rng &r)
{
  if( !bud.init(arena, arena_bytes, ext_tags, sizeof(ext_tags), 4096) )
    return;
  auto c = bud.allocate(1024);
  if( c.ptr == nullptr )
    return;
  if( bud.deallocate(c.ptr + 16) == bb::ret_flag::ok )
    ++buddy_accepted_bogus;
  if( bud.deallocate(c.ptr + 1) == bb::ret_flag::ok )
    ++buddy_accepted_bogus;
  if( bud.deallocate(bud.base + bud.total) == bb::ret_flag::ok )
    ++buddy_accepted_bogus;
  if( bud.block_size(c.ptr + 16) != 0 || bud.block_size(bud.base + bud.total) != 0 )
    ++buddy_accepted_bogus;
  auto d = bud.allocate(1024);
  if( d.ptr != nullptr ) {
    if( bud.deallocate(d.ptr) != bb::ret_flag::ok )
      ++buddy_accepted_bogus;
    if( bud.deallocate(d.ptr) == bb::ret_flag::ok )
      ++buddy_accepted_bogus;
    if( bud.block_size(d.ptr) != 0 || bud.is_allocated(d.ptr) )
      ++buddy_accepted_bogus;
  }

  const usize idx = static_cast<usize>(c.ptr - bud.base) / min_block;
  const u8 saved = bud.block_tags[idx];
  for( u32 t = 0; t < 256; ++t ) {
    bud.block_tags[idx] = static_cast<u8>(t);
    const usize sz = bud.block_size(c.ptr);
    if( sz > bud.total )
      ++buddy_bad_tag;
    if( sz != 0 && !bud.is_allocated(c.ptr) )
      ++buddy_bad_tag;
    if( bud.is_allocated(c.ptr)
        && ((t & bb::__tag_alloc) == 0 || static_cast<i32>(t & bb::__tag_order) >= bud.max_order) )
      ++buddy_bad_tag;
  }
  bud.block_tags[idx] = saved;
  if( bud.deallocate(c.ptr) != bb::ret_flag::ok )
    ++buddy_bad_tag;
  (void)r;
}

void
tlsf_geometry(bbtest::rng &r)
{
  tl.init(sheets);
  const u32 nsheets = 1 + static_cast<u32>(r.next() % 8);
  for( u32 i = 0; i < nsheets; ++i )
    if( !tl.add_sheet(i * 4096) )
      ++tlsf_bad_sheet;

  for( usize i = 0; i < sizeof(sheet_owned); ++i )
    sheet_owned[i] = 0;

  const usize adv = tl.largest_free();
  auto probe = tl.allocate(adv);
  if( adv != 0 && probe.ptr == nullptr )
    ++tlsf_bad_largest;
  if( probe.ptr != nullptr )
    tl.deallocate(static_cast<u32>(probe.ptr - sheets) - 16);

  auto top = tl.allocate(tlsf_t::__max_payload);
  if( top.ptr == nullptr )
    ++tlsf_bad_reach;
  else
    tl.deallocate(static_cast<u32>(top.ptr - sheets) - 16);

  byte *live[512];
  usize live_len[512];
  u32 live_off[512];
  for( usize i = 0; i < 512; ++i )
    live[i] = nullptr;

  for( u32 op = 0; op < BB_FUZZ_TIER_OPS / 8; ++op ) {
    const usize s = static_cast<usize>(r.next() % 512);
    if( live[s] == nullptr ) {
      const usize n = 1 + static_cast<usize>(r.next() % tlsf_t::__max_payload);
      auto c = tl.allocate(n);
      if( c.ptr == nullptr )
        continue;
      if( c.len < n )
        ++tlsf_bad_reach;
      const u32 off = static_cast<u32>(c.ptr - sheets) - 16;
      if( tl.owns(c.ptr) != off )
        ++tlsf_bad_owns;
      if( tl.owns(sheets + tlsf_t::sheet_of(off)) != bb::__null_off )
        ++tlsf_bad_owns;
      if( tl.owns(c.ptr - 1) != bb::__null_off )
        ++tlsf_bad_owns;
      if( tl.owns(c.ptr + c.len + 64) == off )
        ++tlsf_bad_owns;
      const usize first = static_cast<usize>(c.ptr - sheets) / 16;
      const usize cnt = (c.len + 15) / 16;
      bool clash = false;
      for( usize i = 0; !clash && i < cnt; ++i )
        if( sheet_owned[first + i] )
          clash = true;
      if( clash )
        ++tlsf_overlap;
      else
        for( usize i = 0; i < cnt; ++i )
          sheet_owned[first + i] = 1;
      for( usize i = 0; i < c.len; i += 32 )
        c.ptr[i] = static_cast<byte>(s);
      c.ptr[c.len - 1] = static_cast<byte>(s);
      live[s] = c.ptr;
      live_len[s] = c.len;
      live_off[s] = off;
    } else if( (r.next() & 7) == 0 ) {
      const usize half = live_len[s] / 2;
      if( half >= 32 && tl.shrink(live_off[s], half) ) {
        for( usize i = 0; i < half; i += 32 )
          if( live[s][i] != static_cast<byte>(s) )
            ++tlsf_bad_shrink;
        const usize first = static_cast<usize>(live[s] - sheets) / 16;
        for( usize i = (half + 15) / 16; i < (live_len[s] + 15) / 16; ++i )
          sheet_owned[first + i] = 0;
        live_len[s] = tl.payload(live_off[s]);
        for( usize i = 0; i < live_len[s]; i += 32 )
          live[s][i] = static_cast<byte>(s);
        live[s][live_len[s] - 1] = static_cast<byte>(s);
      }
    } else {
      for( usize i = 0; i < live_len[s]; i += 32 )
        if( live[s][i] != static_cast<byte>(s) )
          ++tlsf_overlap;
      if( live[s][live_len[s] - 1] != static_cast<byte>(s) )
        ++tlsf_overlap;
      const usize first = static_cast<usize>(live[s] - sheets) / 16;
      for( usize i = 0; i < (live_len[s] + 15) / 16; ++i )
        sheet_owned[first + i] = 0;
      tl.deallocate(live_off[s]);
      if( tl.owns(live[s]) != bb::__null_off )
        ++tlsf_bad_owns;
      live[s] = nullptr;
    }
    if( (op % 512) == 0 ) {
      const usize l = tl.largest_free();
      if( l != 0 ) {
        auto q = tl.allocate(l);
        if( q.ptr == nullptr )
          ++tlsf_bad_largest;
        else
          tl.deallocate(static_cast<u32>(q.ptr - sheets) - 16);
      }
    }
  }

  for( usize s = 0; s < 512; ++s )
    if( live[s] != nullptr ) {
      if( nsheets > 1 && tl.remove_sheet(tlsf_t::sheet_of(live_off[s])) )
        ++tlsf_bad_sheet;
      tl.deallocate(live_off[s]);
      live[s] = nullptr;
    }
  for( u32 i = 0; i < nsheets; ++i )
    if( !tl.remove_sheet(i * 4096) )
      ++tlsf_bad_sheet;
  if( tl.count != 0 || tl.allocated_bytes != 0 )
    ++tlsf_bad_sheet;
}

};

int
main()
{
  sb::test_case("the buddy survives randomized geometry: base, length, alignment and tag placement");
  {
    bbtest::rng r(0xB0DDE7A11ull);
    for( u32 g = 0; g < BB_FUZZ_GEOMETRIES; ++g )
      buddy_geometry(r);
    sb::require(buddy_bad_geometry == 0);
    sb::require(buddy_bad_align == 0);
    sb::require(buddy_bad_tiling == 0);
  }
  sb::end_test_case();

  sb::test_case("the buddy never hands out a block that overlaps a live one, at any geometry");
  {
    sb::require(buddy_overlap == 0);
    sb::require(buddy_bad_size == 0);
  }
  sb::end_test_case();

  sb::test_case("freeing everything restores the buddy's exact binary decomposition");
  {
    sb::require(buddy_bad_restore == 0);
  }
  sb::end_test_case();

  sb::test_case("no tag byte can make the buddy report a block larger than the region");
  {
    bbtest::rng r(0x7A6FD2AEull);
    for( u32 g = 0; g < 64; ++g )
      buddy_hostile(r);
    sb::require(buddy_bad_tag == 0);
    sb::require(buddy_accepted_bogus == 0);
  }
  sb::end_test_case();

  sb::test_case("every byte of a TLSF sheet is reachable, including the very top block");
  {
    bbtest::rng r(0x71535F00ull);
    for( u32 g = 0; g < BB_FUZZ_GEOMETRIES / 4; ++g )
      tlsf_geometry(r);
    sb::require(tlsf_bad_reach == 0);
    sb::require(tlsf_bad_largest == 0);
  }
  sb::end_test_case();

  sb::test_case("TLSF blocks never overlap and owns() accepts exactly the payload pointers");
  {
    sb::require(tlsf_overlap == 0);
    sb::require(tlsf_bad_owns == 0);
    sb::require(tlsf_bad_shrink == 0);
  }
  sb::end_test_case();

  sb::test_case("sheets are refused while live and released cleanly when empty");
  {
    sb::require(tlsf_bad_sheet == 0);
  }
  sb::end_test_case();

  sb::print("=== bb_fuzz_tiers PASSED ===");
  return 1;
}

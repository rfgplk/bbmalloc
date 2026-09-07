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

constexpr u32 ops = 400000;
constexpr usize slots = 2048;

byte *ptr[slots];
usize len[slots];
u8 *owner;
usize owner_units;
byte *lo;

bool
claim(byte *p, usize n)
{
  const usize first = static_cast<usize>(p - lo) / 16;
  const usize cnt = (n + 15) / 16;
  if( first + cnt > owner_units )
    return false;
  for( usize i = 0; i < cnt; ++i )
    if( owner[first + i] )
      return false;
  for( usize i = 0; i < cnt; ++i )
    owner[first + i] = 1;
  return true;
}

void
release(byte *p, usize n)
{
  const usize first = static_cast<usize>(p - lo) / 16;
  for( usize i = 0; i < (n + 15) / 16; ++i )
    owner[first + i] = 0;
}

};

int
main()
{
  sb::require(bbtest::pool());
  lo = bb::__the_heap.regions[0].buddy.base;
  owner_units = bb::__the_heap.regions[0].buddy.__total() / 16;
  static u8 owner_store[(BB_TEST_POOL_BYTES) / 16];
  owner = owner_store;
  sb::require(owner_units <= sizeof(owner_store));
  const usize avail0 = bb::available();

  sb::test_case("no block ever overlaps a live block, across 400k alloc/free/realloc operations");
  {
    bbtest::rng r(0x5040A11ull);
    bool ok = true;
    u32 overlaps = 0;
    for( u32 op = 0; op < ops && ok; ++op ) {
      const usize s = static_cast<usize>(r.next() % slots);
      const u64 k = r.next() % 100;
      if( ptr[s] == nullptr ) {
        if( k >= 55 )
          continue;
        const usize n = bbtest::sample_size_longtail(r);
        auto c = bb::balloc(n);
        if( c.ptr == nullptr )
          continue;
        if( !bb::within(c.ptr) || c.len < n )
          ok = false;
        if( !claim(c.ptr, c.len) )
          ++overlaps;
        ptr[s] = c.ptr;
        len[s] = c.len;
      } else if( k < 40 ) {
        release(ptr[s], len[s]);
        if( !bb::dealloc(static_cast<void *>(ptr[s])) )
          ok = false;
        ptr[s] = nullptr;
      } else {
        const usize n = bbtest::sample_size_longtail(r);
        release(ptr[s], len[s]);
        auto c = bb::resize({ ptr[s], len[s] }, n, len[s], 16);
        if( c.ptr == nullptr ) {
          if( !claim(ptr[s], len[s]) )
            ++overlaps;
          continue;
        }
        if( c.len < n )
          ok = false;
        if( !claim(c.ptr, c.len) )
          ++overlaps;
        ptr[s] = c.ptr;
        len[s] = c.len;
      }
    }
    sb::require(ok);
    sb::require(overlaps == 0);
    for( usize s = 0; s < slots; ++s )
      if( ptr[s] )
        bb::dealloc(ptr[s]);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_overlap_probe PASSED ===");
  return 1;
}

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

#if defined(BB_GROW_NEGATIVE)
constexpr bool __expect_growth = !bb::__growable;
#else
constexpr bool __expect_growth = bb::__growable;
#endif

constexpr usize __plain_n = 4000;
constexpr usize __aligned_n = 60000;
constexpr usize __aligned_a = 4096;

byte *__plain[4096];
byte *__aligned[256];

usize
fill_plain() noexcept
{
  usize n = 0;
  while( n < 4096 ) {
    __plain[n] = bb::alloc(__plain_n);
    if( __plain[n] == nullptr )
      break;
    bbtest::fp_write(__plain[n], __plain_n, n, 0);
    ++n;
  }
  return n;
}

usize
fill_aligned() noexcept
{
  usize m = 0;
  while( m < 256 ) {
    const auto c = bb::aligned_balloc(__aligned_a, __aligned_n);
    if( c.ptr == nullptr )
      break;
    __aligned[m] = c.ptr;
    ++m;
  }
  return m;
}

}

int
main()
{
  sb::require(bbtest::pool());
  const u32 regions0 = bb::__the_heap.count;
  const usize cap0 = bb::capacity();
  sb::require(regions0 == 1 && cap0 != 0);

  sb::test_case("a plain request past the initial pool attaches another region, or fails cleanly");
  {
    const usize n = fill_plain();
    const u32 regions = bb::__the_heap.count;
    const usize cap = bb::capacity();
    sb::require(n != 0);
    if constexpr( __expect_growth ) {
      sb::require(regions > regions0 && cap > cap0);
      sb::require(n * __plain_n > cap0);
    } else {
      sb::require(regions == regions0 && cap == cap0);
      sb::require(n * __plain_n <= cap0);
    }
    bool ok = true;
    for( usize i = 0; i < n && ok; ++i )
      if( !bb::is_present(__plain[i]) || !bbtest::fp_check(__plain[i], __plain_n, i, 0) )
        ok = false;
    sb::require(ok);
    for( usize i = 0; i < n; ++i )
      if( !bb::dealloc(static_cast<void *>(__plain[i])) )
        ok = false;
    sb::require(ok);
    sb::require(bb::available() == bb::capacity());
  }
  sb::end_test_case();

  sb::test_case("the over-aligned path grows too, and every grant is aligned");
  {
    const usize m = fill_aligned();
    sb::require(m != 0);
    bool ok = true;
    for( usize i = 0; i < m && ok; ++i )
      if( (reinterpret_cast<uintptr_t>(__aligned[i]) & (__aligned_a - 1)) != 0 )
        ok = false;
    sb::require(ok);
    if constexpr( __expect_growth )
      sb::require(m * __aligned_n > cap0);
    else
      sb::require(m * __aligned_n <= cap0);
    for( usize i = 0; i < m; ++i )
      if( !bb::dealloc(static_cast<void *>(__aligned[i])) )
        ok = false;
    sb::require(ok);
    sb::require(bb::available() == bb::capacity());
  }
  sb::end_test_case();

  sb::test_case("growth stops at __max_regions and every grown region is owned");
  {
    (void)fill_plain();
    sb::require(bb::__the_heap.count <= bb::__max_regions);
    bool ok = true;
    for( u32 i = 0; i < bb::__the_heap.count; ++i ) {
      const auto &r = bb::__the_heap.regions[i];
      if( !r.live || !bb::within(r.buddy.base) )
        ok = false;
    }
    sb::require(ok);
    u64 on_stack = 0;
    sb::require(!bb::within(&on_stack));
    bb::reset();
    sb::require(bb::available() == bb::capacity());
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_grow PASSED ===");
  return 1;
}

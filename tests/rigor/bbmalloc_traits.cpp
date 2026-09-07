//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_rigor.hpp"

#include <micron/allocator.hpp>

using namespace snowball;

namespace
{

using A = bb::bb_allocator;
using traits_t = micron::allocator_traits<A>;

usize
E(usize b, usize a)
{
  return A::allocation_extent(b, a);
}

};

int
main()
{
  sb::require(bbtest::pool());
  const usize avail0 = bb::available();
  const usize S = bb::__class_small;
  const usize R = bb::__rz;
  const usize last_small = S - R;

  sb::test_case("allocation_extent is idempotent: E(E(x)) == E(x) for every size up to 1 MiB and every alignment");
  {
    bool ok = true;
    for( usize a = 1; a <= 4096 && ok; a <<= 1 )
      for( usize b = 0; b <= (1u << 20) && ok; b = b < 4 * S ? b + 1 : b + b / 7 + 1 )
        if( E(E(b, a), a) != E(b, a) )
          ok = false;
    sb::require(ok);
  }
  sb::end_test_case();

  sb::test_case("allocation_extent is monotone, never under-reports, and is pure");
  {
    bool ok = true;
    usize prev = 0;
    for( usize b = 0; b <= 4 * S && ok; ++b ) {
      const usize e = E(b, 16);
      if( e < b || e < prev || e != E(b, 16) )
        ok = false;
      prev = e;
    }
    sb::require(ok);
    sb::require(E(last_small - 15, 16) == last_small && E(last_small, 16) == last_small);
    sb::require(E(last_small + 1, 16) == 2 * S - R);
    sb::require(E(8, 32) == 32 - R && E(8, 4096) == 4096 - R && E(0, 4096) == 0);
  }
  sb::end_test_case();

  sb::test_case("the (len / sizeof T) * sizeof T re-derivation is a fixed point for awkward element sizes");
  {
    bool ok = true;
    for( usize s :
         { usize{ 3 }, usize{ 5 }, usize{ 7 }, usize{ 12 }, usize{ 24 }, usize{ 48 }, usize{ 96 }, usize{ 1000 } } )
      for( usize k = 1; k <= 3000 && ok; ++k ) {
        const usize b = k * s;
        const usize len = E(b, 16);
        const usize usable = (len / s) * s;
        if( E(usable, 16) != len )
          ok = false;
      }
    sb::require(ok);
  }
  sb::end_test_case();

  sb::test_case("create satisfies the three postconditions and reports the rule's length, even on a fallback");
  {
    bool ok = true;
    bbtest::rng r(0x5040A11ull);
    for( int i = 0; i < 3000 && ok; ++i ) {
      const usize b = 1 + r.next() % 5000;
      const usize a = usize{ 16 } << (r.next() % 6);
      auto c = traits_t::template allocate<64>(b);
      if( c.ptr == nullptr || (reinterpret_cast<uintptr_t>(c.ptr) & 63) != 0 || c.len < b || c.len != E(b, 64) )
        ok = false;
      if( bb::query_size(c.ptr) < c.len )
        ok = false;
      traits_t::template deallocate<64>(c);
      auto d = A::create(b, a);
      if( d.ptr == nullptr || (reinterpret_cast<uintptr_t>(d.ptr) & (a - 1)) != 0 || d.len != E(b, a) )
        ok = false;
      A::destroy(d, a);
    }
    sb::require(ok);
    auto z = A::create(0, 16);
    sb::require(z.ptr == nullptr && z.len == 0);
    A::destroy(z, 16);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("the unsized destroy path exists, and destroy of the just-returned chunk is legal mid-allocate");
  {
    static_assert(traits_t::template has_unsized_deallocate<64>);
    auto c = A::create(300, 16);
    sb::require(c.ptr != nullptr);
    A::template destroy<16>(c.ptr);
    sb::require(!bb::is_present(c.ptr));
    auto d = A::create(300, 16);
    sb::require(d.ptr == c.ptr);
    A::destroy(d.ptr, 16);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("resize preserves the prefix and reports the rule's length; recommend grows from auto_size");
  {
    auto c = A::create(100, 16);
    bbtest::fp_write(c.ptr, 100, 9, 1);
    auto g = A::template resize<16>(c, 5000, 100);
    sb::require(g.ptr != nullptr && g.len == E(5000, 16) && bbtest::fp_check(g.ptr, 100, 9, 1));
    auto s = A::template resize<16>(g, 40, 5000);
    sb::require(s.ptr != nullptr && s.len == E(40, 16) && bbtest::fp_check(s.ptr, 40, 9, 1));
    A::destroy(s, 16);
    sb::require(A::recommend(0, 0) == 64 && A::recommend(64, 0) == 128 && A::recommend(64, 1000) == 1000);
    sb::require(A::auto_size() == 64);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_traits PASSED ===");
  return 1;
}

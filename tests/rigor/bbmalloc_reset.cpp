//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_rigor.hpp"

using namespace snowball;

int
main()
{
  sb::require(bbtest::pool());
  const usize cap = bb::capacity();
  const usize top = bb::largest_free();

  sb::test_case("reset returns a churned heap to pristine and it allocates again");
  {
    static bbtest::live_set<512> ls;
    ls.init();
    bbtest::counts c{ 0, 0, 0, 0, 0, 0 };
    bbtest::rng r(0x5040A11ull);
    for( u32 op = 0; op < 30000; ++op )
      bbtest::churn_step(ls, r, c, 80, 30);
    sb::require(c.hard_errors == 0 && bb::available() < cap);
    bb::reset();
    sb::require(bb::available() == cap && bb::musage() == 0 && bb::largest_free() == top);
    byte *p = bb::alloc(100);
    sb::require(p != nullptr && bb::is_present(p));
    bb::dealloc(p);
    sb::require(bb::available() == cap);
    ls.init();
    for( u32 op = 0; op < 30000; ++op )
      bbtest::churn_step(ls, r, c, 80, 30);
    bbtest::verify_all(ls, c);
    bbtest::drain_all(ls, c);
    sb::require(c.hard_errors == 0);
    sb::require(bb::available() == cap);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_reset PASSED ===");
  return 1;
}

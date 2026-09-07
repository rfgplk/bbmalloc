//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_rigor.hpp"

using namespace snowball;

#ifndef SOAK_OPS
#define SOAK_OPS 10000000u
#endif
#ifndef SOAK_SWEEP_EVERY
#define SOAK_SWEEP_EVERY 200000u
#endif
#ifndef SOAK_FRAG_EVERY
#define SOAK_FRAG_EVERY 50000u
#endif

namespace
{

bbtest::live_set<4096> g_churn;
bbtest::live_set<128> g_pin;

void
frag_cycle(bbtest::rng &r, bbtest::counts &c, u32 cycle)
{
  for( usize i = 0; i < g_churn.cap(); ++i )
    if( g_churn.ptr[i] == nullptr )
      (void)bbtest::do_alloc(g_churn, i, 1 + r.next() % 2048, c);
  const usize stride = 2 + cycle % 4;
  for( usize i = 0; i < g_churn.cap(); i += stride )
    if( g_churn.ptr[i] != nullptr )
      bbtest::do_free(g_churn, i, c);
  for( usize i = 0; i < g_churn.cap(); ++i )
    if( g_churn.ptr[i] == nullptr )
      (void)bbtest::do_alloc(g_churn, i, 1 + r.next() % 1024, c);
}

};

int
main()
{
  sb::require(bbtest::pool());
  g_churn.init();
  g_pin.init();
  bbtest::counts c{ 0, 0, 0, 0, 0, 0 };
  bbtest::rng r(0x5040A11ull);

  for( usize i = 0; i < g_pin.cap(); ++i )
    sb::require(bbtest::do_alloc(g_pin, i, 512 + r.next() % 7680, c));
  const usize warm = bb::musage();
  const usize avail_warm = bb::available();

  sb::test_case("soak: churn, pinned cohort, fragmentation cycling, periodic full verification");
  {
    bool ok = true;
    u32 cycle = 0;
    for( u32 op = 1; op <= SOAK_OPS && ok; ++op ) {
      bbtest::churn_step(g_churn, r, c, 65, 40);
      if( op % SOAK_FRAG_EVERY == 0 )
        frag_cycle(r, c, cycle++);
      if( op % SOAK_SWEEP_EVERY == 0 ) {
        bbtest::verify_all(g_pin, c);
        bbtest::verify_all(g_churn, c);
        if( c.hard_errors != 0 )
          ok = false;
        if( bb::musage() < warm )
          ok = false;
      }
    }
    sb::require(ok);
    sb::require(c.hard_errors == 0);
    bbtest::verify_all(g_pin, c);
    bbtest::drain_all(g_churn, c);
    sb::require(c.hard_errors == 0);
    sb::require(bb::musage() == warm && bb::available() == avail_warm);
    bbtest::drain_all(g_pin, c);
    sb::require(c.hard_errors == 0 && c.allocs == c.frees);
    sb::require(bb::musage() == 0 && bb::available() == bb::capacity());
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_soak PASSED ===");
  return 1;
}

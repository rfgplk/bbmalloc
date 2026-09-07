//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../support/bb_rigor.hpp"

int
main()
{
  if( !bbtest::pool() )
    return 0;
  byte *a = bb::alloc(100);
  byte *b = bb::alloc(5000);
  byte *c = bb::alloc(70000);
  if( !a || !b || !c )
    return 0;
  bb::which();
  const usize used = bb::musage();
  if( used < 100 + 5000 + 70000 )
    return 0;
  bb::dealloc(a);
  bb::dealloc(b);
  bb::dealloc(c);
  bb::which();
  return bb::musage() == 0 ? 1 : 0;
}

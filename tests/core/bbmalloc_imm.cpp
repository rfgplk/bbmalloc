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
  byte *a = bb::alloc(200);
  if( !a )
    return 0;
  bb::dealloc(a);
  byte *b = bb::alloc(200);
  if( b != a )
    return 0;
  bb::dealloc(b);
  byte *c = bb::alloc(40000);
  if( !c )
    return 0;
  bb::dealloc(c);
  byte *d = bb::alloc(40000);
  if( d != c )
    return 0;
  bb::dealloc(d);
  return 1;
}

//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../support/bb_rigor.hpp"

void *volatile escaped;

int
main()
{
  if( !bbtest::pool() )
    return 0;
  for( int i = 0; i < 1000; ++i ) {
    void *p = bb::malloc(1024);
    if( p == nullptr )
      return 0;
    escaped = p;
    bb::free(p);
  }
  return 1;
}

//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../src/bbmalloc.hpp"

extern "C" int
metal_main(void)
{
  if( !bb::init() )
    return 1;
  byte *a = bb::alloc(100);
  byte *b = bb::alloc(20000);
  if( a == nullptr || b == nullptr )
    return 2;
  bb::dealloc(a);
  bb::dealloc(b);
  return bb::available() == bb::capacity() ? 0 : 3;
}

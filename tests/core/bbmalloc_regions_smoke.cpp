//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../../src/bbmalloc.hpp"

namespace
{
alignas(4096) byte bank0[64 << 10];
alignas(4096) byte bank1[64 << 10];
};

int
main()
{
  if( bb::__max_regions < 2 )
    return 1;
  bb::attach(bank0, sizeof(bank0));
  bb::attach(bank1, sizeof(bank1));
  byte *a = bb::alloc(24000);
  byte *b = bb::alloc(24000);
  if( !a || !b )
    return 0;
  const bool split = (a >= bank0 && a < bank0 + sizeof(bank0)) && (b >= bank1 && b < bank1 + sizeof(bank1));
  bb::dealloc(a);
  bb::dealloc(b);
  return split && bb::available() == bb::capacity() ? 1 : 0;
}

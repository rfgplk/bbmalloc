//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../../src/bbmalloc.hpp"

extern "C" void bb_shim_anchor(void);

void
bb_shim_anchor(void)
{
  void *p = bb::malloc(16);
  bb::free(p);
}

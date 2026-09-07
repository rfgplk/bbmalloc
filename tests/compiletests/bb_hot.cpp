//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../../src/malloc.hpp"

extern "C" byte *
bb_hot_alloc(usize n)
{
  return bb::alloc(n);
}

extern "C" bool
bb_hot_free(void *p)
{
  return bb::dealloc(p);
}

extern "C" usize
bb_hot_query(const void *p)
{
  return bb::query_size(p);
}

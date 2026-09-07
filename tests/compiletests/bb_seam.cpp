//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../../src/bbmalloc.hpp"

namespace
{

alignas(4096) byte pool[256 << 10];

}

extern "C" usize
bb_seam_probe(usize n, usize a)
{
  bb::attach(pool, sizeof(pool));
  using A = bb::bb_allocator;
  auto c = A::create(n, a);
  auto d = A::template create<64>(n);
  auto e = A::template resize<64>(d, n * 2, n);
  usize s = c.len + e.len + A::allocation_extent(n, a) + A::recommend(n, a) + A::auto_size();
  A::destroy(c, a);
  A::template destroy<64>(e.ptr);
  s += bb::__bb_allocator<byte>::calloc(n).len;
  return s;
}

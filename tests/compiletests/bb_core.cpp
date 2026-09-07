//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../../src/malloc.hpp"

namespace
{

alignas(4096) byte pool[256 << 10];

struct payload {
  u64 a;
  u32 b;
};

}

extern "C" usize
bb_core_probe(usize n, usize a)
{
  bb::attach(pool, sizeof(pool));
  byte *p = bb::alloc(n);
  auto c = bb::aligned_balloc(a, n);
  void *z = bb::calloc(4, n);
  payload *t = bb::fetch<payload>();
  auto g = bb::resize(c, n * 2, n, a);
  usize s = bb::query_size(p) + bb::query_size(g.ptr) + (bb::is_present(z) ? 1 : 0) + (bb::within(t) ? 1 : 0);
  s += bb::available() + bb::largest_free() + bb::fragmentation_permille() + bb::musage() + bb::capacity();
  bb::dealloc(p);
  bb::free(z);
  bb::dealloc(t);
  bb::aligned_free(g.ptr);
  bb::reset();
  return s;
}

//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../src/bbmalloc.hpp"

struct sensor_sample {
  u32 t;
  i32 v[4];
};

int
main()
{
  if( !bb::init() )
    return 1;
  sensor_sample *ring = reinterpret_cast<sensor_sample *>(bb::alloc(64 * sizeof(sensor_sample)));
  if( ring == nullptr )
    return 2;
  for( u32 i = 0; i < 64; ++i )
    ring[i] = { i, { 0, 0, 0, 0 } };
  auto frame = bb::aligned_balloc(64, 4096);
  if( frame.ptr == nullptr )
    return 3;
  bb::dealloc(frame.ptr);
  bb::dealloc(ring);
  return bb::available() == bb::capacity() ? 0 : 4;
}

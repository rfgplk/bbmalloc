//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include <micron/__special/initializer_list>

#include "../../src/bbmalloc.hpp"

namespace
{

bool
aligned_to(const void *p, usize a)
{
  return (reinterpret_cast<uintptr_t>(p) & (a - 1)) == 0;
}

int
fail(int line)
{
  bb::__bits::__write("bbmalloc_c_aligned: failed at line ");
  bb::__bits::__print_unsigned(static_cast<u64>(line));
  bb::__bits::__write("\n");
  return 0;
}

};

int
main()
{
  if( !bb::init() )
    return fail(__LINE__);
  const usize avail0 = bb::available();

  for( usize a = 16; a <= 4096; a <<= 1 ) {
    for( usize n : { usize{ 1 }, usize{ 33 }, usize{ 4095 }, usize{ 4096 } } ) {
      void *p = nullptr;
      if( posix_memalign(&p, a, n) != 0 || p == nullptr || !aligned_to(p, a) )
        return fail(__LINE__);
      for( usize i = 0; i < n; ++i )
        static_cast<byte *>(p)[i] = 0x3C;
      free(p);
      void *m = memalign(a, n);
      if( m == nullptr || !aligned_to(m, a) )
        return fail(__LINE__);
      free(m);
      void *c = aligned_alloc(a, ((n + a - 1) / a) * a);
      if( c == nullptr || !aligned_to(c, a) )
        return fail(__LINE__);
      free(c);
    }
  }
  if( bb::available() != avail0 )
    return fail(__LINE__);

  constexpr usize pg = micron::page_size;
  constexpr bool pg_reachable = pg <= bb::__default_max_alignment;
  void *v = valloc(100);
  if constexpr( pg_reachable ) {
    if( v == nullptr || !aligned_to(v, pg) || bb::query_size(v) < 100 )
      return fail(__LINE__);
  } else if( v != nullptr )
    return fail(__LINE__);
  free(v);
  void *pv = pvalloc(100);
  if constexpr( pg_reachable ) {
    if( pv == nullptr || !aligned_to(pv, pg) || bb::query_size(pv) < pg )
      return fail(__LINE__);
  } else if( pv != nullptr )
    return fail(__LINE__);
  free(pv);

  void *q = nullptr;
  if( posix_memalign(&q, 4096, 500) != 0 )
    return fail(__LINE__);
  for( usize i = 0; i < 500; ++i )
    static_cast<byte *>(q)[i] = static_cast<byte>(i);
  byte *r = static_cast<byte *>(realloc(q, 9000));
  if( r == nullptr )
    return fail(__LINE__);
  for( usize i = 0; i < 500; ++i )
    if( r[i] != static_cast<byte>(i) )
      return fail(__LINE__);
  free(r);

  for( int i = 0; i < 10000; ++i ) {
    void *p = nullptr;
    if( posix_memalign(&p, 512, 1 + static_cast<usize>(i % 700)) != 0 )
      return fail(__LINE__);
    free(p);
  }
  if( bb::available() != avail0 )
    return fail(__LINE__);

  void *bad = reinterpret_cast<void *>(static_cast<uintptr_t>(1));
  if( posix_memalign(&bad, 24, 64) != 22 || posix_memalign(&bad, 2, 64) != 22 )
    return fail(__LINE__);
  if( aligned_alloc(64, 100) != nullptr )
    return fail(__LINE__);
  void *z = calloc(10, 10);
  if( z == nullptr )
    return fail(__LINE__);
  for( usize i = 0; i < 100; ++i )
    if( static_cast<byte *>(z)[i] != 0 )
      return fail(__LINE__);
  free(z);
  free(nullptr);
  if( bb::available() != avail0 )
    return fail(__LINE__);
  return 1;
}

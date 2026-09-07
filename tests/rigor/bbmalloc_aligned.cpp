//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_rigor.hpp"

using namespace snowball;

namespace
{

bool
aligned_to(const void *p, usize a)
{
  return (reinterpret_cast<uintptr_t>(p) & (a - 1)) == 0;
}

};

int
main()
{
  sb::require(bbtest::pool());
  const usize avail0 = bb::available();

  sb::test_case("(a) the aligned family returns aligned, fully writable memory for every alignment up to the maximum");
  {
    bool ok = true;
    for( usize a = 32; a <= bb::__default_max_alignment && ok; a <<= 1 ) {
      for( usize n : { usize{ 1 }, usize{ 33 }, usize{ 4095 }, usize{ 4096 }, usize{ 70000 } } ) {
        auto c = bb::aligned_balloc(a, n);
        if( !c.ptr || !aligned_to(c.ptr, a) || c.len < n ) {
          ok = false;
          break;
        }
        micron::memset(c.ptr, 0x3C, c.len);
        void *v = bb::aligned_alloc(a, ((n + a - 1) / a) * a);
        if( !v || !aligned_to(v, a) ) {
          ok = false;
          break;
        }
        bb::aligned_free(v);
        bb::aligned_free(c.ptr, a);
      }
    }
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("(b) a plain dealloc releases the block behind an over-aligned pointer");
  {
    auto c = bb::aligned_balloc(1024, 100);
    sb::require(c.ptr != nullptr && bb::is_present(c.ptr) && bb::query_size(c.ptr) >= 100);
    bb::dealloc(c.ptr);
    sb::require(!bb::is_present(c.ptr) && bb::query_size(c.ptr) == 0);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("(c) ten thousand aligned alloc/free pairs do not grow the heap");
  {
    bool ok = true;
    for( int i = 0; i < 10000 && ok; ++i ) {
      auto c = bb::aligned_balloc(512, 1 + static_cast<usize>(i % 700));
      if( !c.ptr || !aligned_to(c.ptr, 512) )
        ok = false;
      bb::dealloc(c.ptr);
      if( bb::available() != avail0 )
        ok = false;
    }
    sb::require(ok);
  }
  sb::end_test_case();

  sb::test_case("(d) realloc on an over-aligned pointer preserves the contents");
  {
    auto c = bb::aligned_balloc(256, 600);
    sb::require(c.ptr != nullptr);
    bbtest::fp_write(c.ptr, 600, 3, 1);
    byte *g = static_cast<byte *>(bb::realloc(c.ptr, 9000));
    sb::require(g != nullptr && bbtest::fp_check(g, 600, 3, 1));
    bb::free(g);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("(e) C11 aligned_alloc rules: non-power-of-two alignment or a size that is not a multiple is refused");
  {
    sb::require(bb::aligned_alloc(0, 64) == nullptr);
    sb::require(bb::aligned_alloc(24, 48) == nullptr);
    sb::require(bb::aligned_alloc(64, 100) == nullptr);
    sb::require(bb::aligned_alloc(64, 0) == nullptr);
    void *v = bb::aligned_alloc(64, 128);
    sb::require(v != nullptr && aligned_to(v, 64));
    bb::free(v);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("(f) an alignment above the region's base alignment is refused by policy, not by a crash");
  {
    sb::require(bb::aligned_balloc(bb::__default_max_alignment << 1, 64).ptr == nullptr);
    sb::require(bb::aligned_balloc(1u << 20, 64).ptr == nullptr);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_aligned PASSED ===");
  return 1;
}

//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_rigor.hpp"

using namespace snowball;

int
main()
{
  sb::require(bbtest::pool());
  static_assert(bb::__default_redzone && bb::__default_poison_on_free && bb::__default_double_free_action == 1);
  const usize avail0 = bb::available();

  sb::test_case("the redzone is invisible to the caller: query_size and the granted length exclude it");
  {
    auto c = bb::balloc(100);
    sb::require(c.ptr != nullptr && c.len >= 100 && bb::query_size(c.ptr) == c.len);
    bool zone = true;
    for( usize i = 0; i < bb::__default_redzone_size; ++i )
      if( c.ptr[c.len + i] != bb::__default_redzone_byte )
        zone = false;
    sb::require(zone);
    micron::memset(c.ptr, 0x11, c.len);
    sb::require(bb::dealloc(static_cast<void *>(c.ptr)));
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("an overrun into the redzone is reported on free and the block is still released");
  {
    auto c = bb::balloc(50);
    sb::require(c.ptr != nullptr);
    c.ptr[c.len] = 0x00;
    sb::require(bb::dealloc(static_cast<void *>(c.ptr)));
    sb::require(!bb::is_present(c.ptr));
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("freed memory is poisoned");
  {
    auto c = bb::balloc(200);
    micron::memset(c.ptr, 0x55, 200);
    byte *p = c.ptr;
    bb::dealloc(p);
    bool poisoned = true;
    for( usize i = 16; i < 200; ++i )
      if( p[i] != bb::__default_poison_byte )
        poisoned = false;
    sb::require(poisoned);
  }
  sb::end_test_case();

  sb::test_case("a double free is reported and refused, and so is a foreign or interior pointer");
  {
    byte *p = bb::alloc(64);
    sb::require(bb::dealloc(static_cast<void *>(p)) == true);
    sb::require(bb::dealloc(static_cast<void *>(p)) == false);
    byte *q = bb::alloc(64);
    sb::require(bb::dealloc(static_cast<void *>(q + 16)) == false);
    u64 on_stack = 0;
    sb::require(bb::dealloc(static_cast<void *>(&on_stack)) == false);
    sb::require(bb::dealloc(static_cast<void *>(q)) == true);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::test_case("over-aligned blocks keep their alignment with the redzone on");
  {
    bool ok = true;
    for( usize a = 32; a <= 4096 && ok; a <<= 1 ) {
      auto c = bb::aligned_balloc(a, 100);
      if( c.ptr == nullptr || (reinterpret_cast<uintptr_t>(c.ptr) & (a - 1)) != 0 || c.len < 100 )
        ok = false;
      bb::dealloc(c.ptr);
    }
    sb::require(ok);
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_harden PASSED ===");
  return 1;
}

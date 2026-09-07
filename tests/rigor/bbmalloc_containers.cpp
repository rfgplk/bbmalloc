//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "../snowball/snowball.hpp"
#include "../support/bb_rigor.hpp"

#include <micron/maps.hpp>
#include <micron/string/strings.hpp>
#include <micron/vector.hpp>

using namespace snowball;

namespace
{

struct payload {
  u64 a;
  u32 b;
  char c;
  payload() : a(0), b(0), c('x') {}
  explicit payload(u64 v) : a(v), b(static_cast<u32>(v)), c('y') {}
};

template <typename T>
bool
round_trip()
{
  micron::vector<T, bb::bb_allocator> v;
  for( u64 i = 0; i < 700; ++i )
    v.push_back(T{});
  if( v.size() != 700 )
    return false;
  v.clear();
  return true;
}

};

int
main()
{
  sb::require(bbtest::pool());
  const usize avail0 = bb::available();

  sb::test_case("mutable_resource round trip across element sizes");
  {
    sb::require(round_trip<u8>());
    sb::require(round_trip<u16>());
    sb::require(round_trip<u32>());
    sb::require(round_trip<u64>());
    sb::require(round_trip<payload>());
  }
  sb::end_test_case();

  sb::test_case("a vector grows through both tiers, holds its values, and survives churn");
  {
    micron::vector<u64, bb::bb_allocator> v;
    bool ok = true;
    for( u64 i = 0; i < 20000; ++i )
      v.push_back(i * 3);
    for( u64 i = 0; i < 20000 && ok; ++i )
      if( v[i] != i * 3 )
        ok = false;
    for( int r = 0; r < 20 && ok; ++r ) {
      for( int k = 0; k < 500; ++k )
        v.pop_back();
      for( int k = 0; k < 500; ++k )
        v.push_back(static_cast<u64>(k));
    }
    sb::require(ok && v.size() == 20000);
  }
  sb::end_test_case();

  sb::test_case("a map and a string over the barebones allocator");
  {
    micron::hopscotch_map<u64, u64, 32, micron::hopscotch_node<u64, u64>, bb::bb_allocator> m;
    for( u64 i = 0; i < 4000; ++i )
      m.insert(i, i * 3);
    bool consistent = m.size() >= 3900 && m.size() <= 4000;
    for( u64 i = 0; i < 4000 && consistent; ++i ) {
      const u64 v = m[i];
      if( v % 3 != 0 || v > 3999 * 3 )
        consistent = false;
    }
    sb::require(consistent);
    micron::hstring<char, true, bb::bb_allocator> s("ab");
    for( int i = 0; i < 5000; ++i )
      s.push_back('x');
    sb::require(s.size() == 2 + 5000);
  }
  sb::end_test_case();

  sb::test_case("everything the containers took is back after they die");
  {
    sb::require(bb::available() == avail0);
  }
  sb::end_test_case();

  sb::print("=== bbmalloc_containers PASSED ===");
  return 1;
}

//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#pragma once

#include "../src/bbmalloc.hpp"

#include <micron/port/clock.hpp>
#include <micron/print.hpp>
#include <micron/types.hpp>

namespace bbbench
{

[[gnu::always_inline]] inline u64
cycles() noexcept
{
#if defined(__x86_64__) || defined(__i386__)
  u32 lo = 0;
  u32 hi = 0;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return (static_cast<u64>(hi) << 32) | lo;
#elif defined(__aarch64__)
  u64 v = 0;
  __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(v));
  return v;
#else
  return static_cast<u64>(micron::port::mono_ticks());
#endif
}

template <typename T>
[[gnu::always_inline]] inline void
clobber(T *p) noexcept
{
  __asm__ __volatile__("" : : "g"(p) : "memory");
}

inline void
barrier() noexcept
{
  __asm__ __volatile__("" : : : "memory");
}

constexpr int k_measurements = 7;
constexpr int k_warmup = 2;

inline u64
median(u64 *v, int n) noexcept
{
  for( int i = 1; i < n; ++i ) {
    const u64 x = v[i];
    int j = i - 1;
    while( j >= 0 && v[j] > x ) {
      v[j + 1] = v[j];
      --j;
    }
    v[j + 1] = x;
  }
  return v[n / 2];
}

struct sample {
  u64 cycles_per_op_x100;
  u64 ns_per_op_x100;
};

template <typename Setup, typename Kernel, typename Cleanup>
inline sample
measure(usize ops_per_rep, Setup &&setup, Kernel &&kernel, Cleanup &&cleanup) noexcept
{
  u64 cyc[k_measurements];
  u64 ns[k_measurements];
  for( int i = 0; i < k_warmup; ++i ) {
    setup();
    kernel();
    cleanup();
  }
  for( int i = 0; i < k_measurements; ++i ) {
    setup();
    barrier();
    const u64 t0 = static_cast<u64>(micron::port::mono_ticks());
    const u64 c0 = cycles();
    barrier();
    kernel();
    barrier();
    const u64 c1 = cycles();
    const u64 t1 = static_cast<u64>(micron::port::mono_ticks());
    barrier();
    cleanup();
    cyc[i] = ((c1 - c0) * 100) / ops_per_rep;
    ns[i] = ((t1 - t0) * 100) / ops_per_rep;
  }
  return { median(cyc, k_measurements), median(ns, k_measurements) };
}

inline void
print_x100(u64 v)
{
  micron::print(v / 100, ".", (v % 100) < 10 ? "0" : "", v % 100);
}

inline void
report(const char *name, usize size, sample s)
{
  micron::print(name, " size=", size, " cycles/op=");
  print_x100(s.cycles_per_op_x100);
  micron::print(" ns/op=");
  print_x100(s.ns_per_op_x100);
  micron::print("\n");
}

inline void
header(const char *bench)
{
  micron::println("# bbmalloc bench: ", bench);
  micron::println("# profile: min_block=", bb::__default_min_block, " class_small=", bb::__class_small,
                  " sheet=", bb::__default_tlsf_sheet, " max_sheets=", bb::__max_tlsf_sheets,
                  " cache_cap=", bb::__default_order_cache_cap);
  micron::println("# pool bytes=", bb::capacity(), " measurements=", k_measurements,
                  " estimator=median cycles/op via rdtsc");
}

};

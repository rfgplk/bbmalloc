//  Copyright (c) 2026 David Lucius Severus
//
//  Distributed under the Boost Software License, Version 1.0.
//  See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt

#include "__bench.hpp"

namespace
{

constexpr usize samples = 65536;
constexpr usize slots = 512;

u32 lat_alloc[samples];
u32 lat_free[samples];
u32 scratch[samples];
byte *held[slots];
usize held_n[slots];

u64
next(u64 &s)
{
  u64 z = (s += 0x9E37'79B9'7F4A'7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EBull;
  return z ^ (z >> 31);
}

[[gnu::always_inline]] inline u64
ordered_cycles() noexcept
{
#if defined(__x86_64__) || defined(__i386__)
  u32 lo = 0;
  u32 hi = 0;
  __asm__ __volatile__("lfence\n\trdtsc\n\tlfence" : "=a"(lo), "=d"(hi) : : "memory");
  return (static_cast<u64>(hi) << 32) | lo;
#else
  return bbbench::cycles();
#endif
}

void
sort_u32(u32 *a, u32 *tmp, usize n)
{
  for( u32 shift = 0; shift < 32; shift += 8 ) {
    usize count[257];
    for( usize i = 0; i < 257; ++i )
      count[i] = 0;
    for( usize i = 0; i < n; ++i )
      ++count[((a[i] >> shift) & 0xFF) + 1];
    for( usize i = 0; i < 256; ++i )
      count[i + 1] += count[i];
    for( usize i = 0; i < n; ++i )
      tmp[count[(a[i] >> shift) & 0xFF]++] = a[i];
    u32 *t = a;
    a = tmp;
    tmp = t;
  }
}

struct dist {
  u32 p50;
  u32 p90;
  u32 p99;
  u32 p999;
  u32 max;
};

dist
summarize(u32 *v, usize n)
{
  sort_u32(v, scratch, n);
  return { v[n / 2], v[(n * 9) / 10], v[(n * 99) / 100], v[(n * 999) / 1000], v[n - 1] };
}

void
report(const char *name, const char *op, usize n, dist d)
{
  micron::println("[lat ", name, "] ", op, " n=", n, " p50=", d.p50, " p90=", d.p90, " p99=", d.p99, " p999=", d.p999,
                  " max=", d.max);
}

template <typename Size>
void
churn(const char *name, usize live_slots, usize warm, Size &&size_of)
{
  for( usize i = 0; i < slots; ++i )
    held[i] = nullptr;
  u64 seed = 0x1A7E0C5ull;
  usize na = 0;
  usize nf = 0;
  for( usize i = 0; na < samples || nf < samples; ++i ) {
    const usize s = static_cast<usize>(next(seed) % live_slots);
    if( held[s] == nullptr ) {
      const usize n = size_of(seed);
      const u64 t0 = ordered_cycles();
      byte *p = bb::alloc(n);
      const u64 t1 = ordered_cycles();
      bbbench::clobber(p);
      held[s] = p;
      held_n[s] = n;
      if( i >= warm && na < samples )
        lat_alloc[na++] = static_cast<u32>(t1 - t0);
    } else {
      byte *p = held[s];
      const u64 t0 = ordered_cycles();
      bb::dealloc(p);
      const u64 t1 = ordered_cycles();
      held[s] = nullptr;
      if( i >= warm && nf < samples )
        lat_free[nf++] = static_cast<u32>(t1 - t0);
    }
  }
  for( usize i = 0; i < slots; ++i )
    if( held[i] != nullptr ) {
      bb::dealloc(held[i]);
      held[i] = nullptr;
    }
  report(name, "alloc", na, summarize(lat_alloc, na));
  report(name, "free", nf, summarize(lat_free, nf));
}

void
sheet_oscillation()
{
  constexpr usize per_sheet = bb::__default_tlsf_sheet / 32;
  constexpr usize keep = per_sheet + per_sheet / 2;
  static byte *blocks[keep];
  usize na = 0;
  usize nf = 0;
  for( usize i = 0; i < keep; ++i )
    blocks[i] = bb::alloc(16);
  for( usize round = 0; na < samples; ++round ) {
    for( usize i = keep; i > 0; --i ) {
      const u64 t0 = ordered_cycles();
      bb::dealloc(blocks[i - 1]);
      const u64 t1 = ordered_cycles();
      if( round > 0 && nf < samples )
        lat_free[nf++] = static_cast<u32>(t1 - t0);
    }
    for( usize i = 0; i < keep; ++i ) {
      const u64 t0 = ordered_cycles();
      blocks[i] = bb::alloc(16);
      const u64 t1 = ordered_cycles();
      bbbench::clobber(blocks[i]);
      if( round > 0 && na < samples )
        lat_alloc[na++] = static_cast<u32>(t1 - t0);
    }
  }
  for( usize i = 0; i < keep; ++i )
    bb::dealloc(blocks[i]);
  report("sheet-oscillation", "alloc", na, summarize(lat_alloc, na));
  report("sheet-oscillation", "free", nf, summarize(lat_free, nf));
}

}

int
main()
{
  if( !bb::init() )
    return 0;
  bbbench::header("per-op latency: fenced rdtsc around each call, percentiles over 65536 samples per op kind");
  churn("tlsf-churn", 512, 4096, [](u64 &s) { return static_cast<usize>(1 + next(s) % 1024); });
  churn("buddy-churn", 128, 1024, [](u64 &s) { return static_cast<usize>(2048 + next(s) % 63488); });
  churn("mixed", 256, 2048, [](u64 &s) {
    const u64 k = next(s) % 100;
    return static_cast<usize>(k < 50 ? 1 + next(s) % 256 : (k < 85 ? 257 + next(s) % 768 : 1025 + next(s) % 15360));
  });
  sheet_oscillation();
  return 0;
}

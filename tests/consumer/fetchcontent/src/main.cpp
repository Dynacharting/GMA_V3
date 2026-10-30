// Consumer probe (ENC-1553): links and runs code from krn_engine.
#include <krn/rt/ThreadPool.hpp>

#include <atomic>
#include <cstdio>

int main() {
  std::atomic<int> ran{0};
  krn::rt::ThreadPool pool(2);
  for (int i = 0; i < 4; ++i) pool.post([&ran] { ++ran; });
  pool.drain();
  pool.shutdown();
  std::printf("KRN_SMOKE probe: %d/4 tasks ran on krn::rt::ThreadPool\n", ran.load());
  return ran.load() == 4 ? 0 : 1;
}

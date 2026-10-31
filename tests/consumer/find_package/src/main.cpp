// Consumer probe (ENC-1553): links and runs code from krn_engine, and links
// krn_host (ENC-1559).
#include <krn/host/Host.hpp>
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
  // Taking the address pulls Host.cpp's object, and everything it calls, into
  // the link; HostOptions' default must be the one the header declares.
  int (*host)(int, char**, const krn::host::HostOptions&) = &krn::host::hostMain;
  const krn::host::HostOptions opts;
  std::printf("KRN_SMOKE probe: krn::host::hostMain linked at %p, default programName '%s'\n",
              reinterpret_cast<void*>(host), opts.programName.c_str());
  return ran.load() == 4 && host != nullptr && opts.programName == "krn" ? 0 : 1;
}

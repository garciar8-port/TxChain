// mine_bench — a reproducible difficulty-calibration harness (CRE-209, resolves
// Open Design Question #1). Times pow::mine() (the m3-miner-loop-commit nonce
// search) over N coinbase-only candidates at each difficulty D and reports the
// min / median / max seconds-per-block, so the DIFFICULTY_BITS default can be
// chosen to land a single-node mine in the visible-but-fast ~1–5 s window on the
// target laptop. Not shipped in the binaries — a dev/DevEx tool.
//
// Usage: mine_bench [samples] [D ...]   (defaults: 6 samples, D = 12 16 20)

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "txchain/chain/genesis.hpp"
#include "txchain/chain/types.hpp"
#include "txchain/mempool/mempool.hpp"
#include "txchain/pow/candidate.hpp"
#include "txchain/pow/miner.hpp"

int main(int argc, char** argv) {
  using namespace txchain;

  int samples = 6;
  std::vector<unsigned> difficulties = {12, 16, 20};
  if (argc >= 2) samples = std::atoi(argv[1]);
  if (argc >= 3) {
    difficulties.clear();
    for (int i = 2; i < argc; ++i)
      difficulties.push_back(static_cast<unsigned>(std::strtoul(argv[i], nullptr, 10)));
  }

  // A coinbase-only candidate atop genesis (empty mempool).
  chain::Address miner{};
  miner.fill(0x33);
  std::map<chain::Address, chain::AccountState> st;
  mempool::Mempool mp([&st](const chain::Address& a) {
    const auto it = st.find(a);
    return it == st.end() ? chain::AccountState{} : it->second;
  });
  const auto genesis = chain::genesisBlock();
  const auto base = pow::buildCandidate(miner, 0, genesis.header.hash(), genesis.header.timestamp,
                                        genesis.header.timestamp + 1, mp,
                                        [&st](const chain::Address& a) {
                                          const auto it = st.find(a);
                                          return it == st.end() ? chain::AccountState{} : it->second;
                                        });

  std::printf("mine_bench: %d samples/D, coinbase-only blocks\n", samples);
  std::printf("%4s  %10s  %10s  %10s\n", "D", "min(s)", "median(s)", "max(s)");
  std::atomic<bool> stop{false};
  for (unsigned D : difficulties) {
    std::vector<double> secs;
    for (int s = 0; s < samples; ++s) {
      pow::Candidate cand = base;
      cand.hdr.timestamp = genesis.header.timestamp + 1 + static_cast<std::uint64_t>(s);
      const auto t0 = std::chrono::steady_clock::now();
      const auto mined = pow::mine(cand, D, stop);
      const auto t1 = std::chrono::steady_clock::now();
      if (!mined) continue;
      secs.push_back(std::chrono::duration<double>(t1 - t0).count());
    }
    std::sort(secs.begin(), secs.end());
    const double lo = secs.empty() ? 0 : secs.front();
    const double md = secs.empty() ? 0 : secs[secs.size() / 2];
    const double hi = secs.empty() ? 0 : secs.back();
    std::printf("%4u  %10.4f  %10.4f  %10.4f\n", D, lo, md, hi);
    std::fflush(stdout);
  }
  return 0;
}

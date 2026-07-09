#include "txchain/wallet/wallet.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <picojson.h>  // vendored (SYSTEM include on txchain_core), INT64 mode

#include "txchain/crypto/address.hpp"
#include "txchain/crypto/ed25519.hpp"
#include "txchain/crypto/hashutil.hpp"

namespace txchain::wallet {

namespace fs = std::filesystem;

std::string wallet_path_for_datadir(const std::string& datadir) {
  std::string p = datadir;
  if (!p.empty() && p.back() != '/') p.push_back('/');
  return p + "wallet.key";
}

Wallet wallet_create() {
  const crypto::KeyPair kp = crypto::keygen();  // CSPRNG; throws if unavailable
  Wallet w;
  w.seed = kp.seed;
  w.pubkey = kp.pubkey;  // == derive_pubkey(seed)
  w.address = crypto::address(w.pubkey);
  return w;
}

bool wallet_save(const std::string& path, const Wallet& w) {
  const std::string json = std::string("{\"version\":") + std::to_string(kWalletVersion) +
                           ",\"priv\":\"" + crypto::to_hex(w.seed) + "\"" +
                           ",\"pub\":\"" + crypto::to_hex(w.pubkey) + "\"" +
                           ",\"address\":\"" + crypto::to_hex(w.address) + "\"}\n";

  // O_CREAT mode only applies on creation, so force 0600 with fchmod to also
  // tighten a pre-existing file (private key material — never group/other).
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  if (::fchmod(fd, 0600) != 0) {
    ::close(fd);
    return false;
  }

  const char* p = json.data();
  std::size_t remaining = json.size();
  while (remaining > 0) {
    const ssize_t wr = ::write(fd, p, remaining);
    if (wr < 0) {
      if (errno == EINTR) continue;
      ::close(fd);
      return false;
    }
    p += wr;
    remaining -= static_cast<std::size_t>(wr);
  }
  if (::fsync(fd) != 0) {
    ::close(fd);
    return false;
  }
  ::close(fd);
  return true;
}

LoadResult wallet_load(const std::string& path) {
  LoadResult r;

  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) {
    r.error = "wallet.key: cannot open " + path;
    return r;
  }
  // Any group/other permission bit means the private key is exposed — warn, don't
  // fail (deliberate learning-artifact affordance, Cryptography §10).
  r.world_readable = (st.st_mode & (S_IRWXG | S_IRWXO)) != 0;

  std::ifstream in(path);
  if (!in) {
    r.error = "wallet.key: cannot read " + path;
    return r;
  }
  const std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());

  picojson::value v;
  const std::string perr = picojson::parse(v, content);
  if (!perr.empty() || !v.is<picojson::object>()) {
    r.error = "wallet.key: malformed JSON";
    return r;
  }
  const auto& o = v.get<picojson::object>();

  const auto vit = o.find("version");
  if (vit != o.end() && vit->second.is<std::int64_t>() &&
      vit->second.get<std::int64_t>() != kWalletVersion) {
    r.error = "wallet.key: unsupported version";
    return r;
  }
  const auto sit = o.find("priv");
  if (sit == o.end() || !sit->second.is<std::string>()) {
    r.error = "wallet.key: missing priv";
    return r;
  }

  Wallet w;
  // Length discipline mirrors chain load: a seed that is not exactly 32 bytes
  // (64 hex) is a hard reject, not a truncated/padded key.
  if (!crypto::from_hex<32>(sit->second.get<std::string>(), w.seed)) {
    r.error = "wallet.key: priv is not a 32-byte seed";
    return r;
  }
  // Ground truth: derive pubkey/address from the seed; the stored fields are
  // redundant display copies and are recompute-and-asserted below.
  w.pubkey = crypto::derive_pubkey(w.seed);
  w.address = crypto::address(w.pubkey);

  const auto pit = o.find("pub");
  if (pit != o.end() && pit->second.is<std::string>()) {
    crypto::PubKey32 stored{};
    if (!crypto::from_hex<32>(pit->second.get<std::string>(), stored) || stored != w.pubkey) {
      r.error = "wallet.key: stored pub does not match derive_pubkey(seed)";
      return r;
    }
  }
  const auto ait = o.find("address");
  if (ait != o.end() && ait->second.is<std::string>()) {
    crypto::Address20 stored{};
    if (!crypto::from_hex<20>(ait->second.get<std::string>(), stored) || stored != w.address) {
      r.error = "wallet.key: stored address does not match SHA-256(pub)[:20]";
      return r;
    }
  }

  r.wallet = w;
  r.ok = true;
  return r;
}

LoadResult load_or_create_wallet(const std::string& datadir) {
  std::error_code ec;
  fs::create_directories(datadir, ec);

  const std::string path = wallet_path_for_datadir(datadir);
  struct stat st {};
  if (::stat(path.c_str(), &st) == 0) return wallet_load(path);

  LoadResult r;
  const Wallet w = wallet_create();
  if (!wallet_save(path, w)) {
    r.error = "wallet.key: cannot write " + path;
    return r;
  }
  r.wallet = w;
  r.ok = true;
  return r;
}

}  // namespace txchain::wallet

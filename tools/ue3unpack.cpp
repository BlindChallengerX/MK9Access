// ue3unpack: decompress the LZO chunks of MK9 packages into one plain file each (for offline text extraction).
// The LZO1X decoder and chunk rules come from mk9-lauta shaders/tools/ue3_shader_scan.cpp (GPL-3.0).
// usage: ue3unpack <outdir> <packages...>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <fstream>
#include <iterator>
#include <filesystem>
namespace {
// LZO1X decompression (the lzo1x_decompress_safe algorithm), bounds-checked.
// Mass Effect's packages use COMPRESS_LZO (CompressionFlags = 2).
bool Lzo(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_len) {
  const uint8_t* ip = in;
  const uint8_t* const ie = in + in_len;
  uint8_t* op = out;
  uint8_t* const oe = out + out_len;
  size_t t = 0;
  const uint8_t* m;
  auto need_in = [&](size_t n) { return size_t(ie - ip) >= n; };
  auto need_out = [&](size_t n) { return size_t(oe - op) >= n; };
  auto lit = [&](size_t n) {
    if (!need_in(n) || !need_out(n)) return false;
    std::memcpy(op, ip, n); op += n; ip += n; return true;
  };
  auto copy = [&](const uint8_t* from, size_t n) {
    if (from < out || !need_out(n)) return false;
    for (size_t i = 0; i < n; ++i) op[i] = from[i];
    op += n; return true;
  };
  auto run = [&](size_t base) -> bool {
    size_t v = base;
    while (need_in(1) && *ip == 0) { v += 255; ++ip; }
    if (!need_in(1)) return false;
    t = v + *ip++;
    return true;
  };
  if (!need_in(1)) return false;
  bool first = false;
  if (*ip > 17) {
    t = *ip++ - 17;
    if (t < 4) goto match_next;
    if (!lit(t)) return false;
    first = true;
  }
  for (;;) {
    if (!first) {
      if (!need_in(1)) return false;
      t = *ip++;
      if (t >= 16) goto match;
      if (t == 0 && !run(15)) return false;
      if (!lit(t + 3)) return false;
    }
    first = false;
    if (!need_in(1)) return false;
    t = *ip++;
    if (t < 16) {
      if (!need_in(1)) return false;
      m = op - (1 + 0x0800) - (t >> 2) - (size_t(*ip++) << 2);
      if (!copy(m, 3)) return false;
      goto match_done;
    }
  match:
    for (;;) {
      if (t >= 64) {
        if (!need_in(1)) return false;
        m = op - 1 - ((t >> 2) & 7) - (size_t(*ip++) << 3);
        t = (t >> 5) - 1;
        if (!copy(m, t + 2)) return false;
      } else if (t >= 32) {
        t &= 31;
        if (t == 0 && !run(31)) return false;
        if (!need_in(2)) return false;
        m = op - 1 - ((ip[0] >> 2) + (size_t(ip[1]) << 6));
        ip += 2;
        if (!copy(m, t + 2)) return false;
      } else if (t >= 16) {
        m = op - (size_t(t & 8) << 11);
        t &= 7;
        if (t == 0 && !run(7)) return false;
        if (!need_in(2)) return false;
        m -= (ip[0] >> 2) + (size_t(ip[1]) << 6);
        ip += 2;
        if (m == op) return op == oe;  // end of stream
        m -= 0x4000;
        if (!copy(m, t + 2)) return false;
      } else {
        if (!need_in(1)) return false;
        m = op - 1 - (t >> 2) - (size_t(*ip++) << 2);
        if (!copy(m, 2)) return false;
      }
    match_done:
      t = ip[-2] & 3;
      if (t == 0) break;
    match_next:
      if (!lit(t)) return false;
      if (!need_in(1)) return false;
      t = *ip++;
    }
  }
}

uint32_t BE(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
}
int main(int argc, char** argv) {
  std::filesystem::path outdir = argv[1];
  std::filesystem::create_directories(outdir);
  for (int a = 2; a < argc; ++a) {
    std::ifstream f(argv[a], std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    std::vector<uint8_t> plain;
    size_t chunks = 0;
    for (size_t off = 0; off + 16 <= d.size(); ++off) {
      if (BE(&d[off]) != 0x9E2A83C1u || (BE(&d[off + 4]) != 0x9E2A83C1u && BE(&d[off + 4]) != 0x20000u)) continue;
      const uint32_t csize = BE(&d[off + 8]), usize = BE(&d[off + 12]);
      if (usize == 0 || usize > 0x4000000 || csize == 0 || csize > d.size()) continue;
      const size_t nblocks = (usize + 0x1FFFF) / 0x20000;
      const size_t hdr = off + 16;
      size_t data = hdr + nblocks * 8;
      if (data > d.size()) continue;
      uint64_t sum_c = 0, sum_u = 0;
      for (size_t k = 0; k < nblocks; ++k) { sum_c += BE(&d[hdr + k * 8]); sum_u += BE(&d[hdr + k * 8 + 4]); }
      if (sum_c != csize || sum_u != usize || data + csize > d.size()) continue;
      std::vector<uint8_t> out(usize);
      size_t produced = 0; bool ok = true;
      for (size_t k = 0; k < nblocks && ok; ++k) {
        const uint32_t bc = BE(&d[hdr + k * 8]), bu = BE(&d[hdr + k * 8 + 4]);
        if (!Lzo(&d[data], bc, out.data() + produced, bu)) ok = false;
        produced += bu; data += bc;
      }
      if (!ok) continue;
      ++chunks;
      plain.insert(plain.end(), out.begin(), out.end());
      off = data - 1;
    }
    std::filesystem::path o = outdir / (std::filesystem::path(argv[a]).filename().string() + ".bin");
    std::ofstream w(o, std::ios::binary);
    w.write((const char*)plain.data(), plain.size());
    std::printf("%s: %zu chunks, %zu bytes\n", argv[a], chunks, plain.size());
  }
}

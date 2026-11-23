/**
 * Plays the engine offline and writes a 16-bit stereo WAV: a check that the engine builds and sounds on this machine,
 * with no plugin host and no browser.
 *
 *   clang++ -std=c++17 -O2 -I../engine ../engine/lemnos_engine.cpp render.cpp -o render
 *   ./render out.wav [note=48] [seconds=2] [sampleRate=48000]
 */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "lemnos_engine.h"

static void put16(std::FILE* f, int v) { std::fputc(v & 255, f); std::fputc((v >> 8) & 255, f); }
static void put32(std::FILE* f, std::uint32_t v) { put16(f, v & 0xffff); put16(f, v >> 16); }

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "usage: render out.wav [note] [seconds] [sampleRate]\n"); return 1; }
  const int note = argc > 2 ? std::atoi(argv[2]) : 48;
  const double seconds = argc > 3 ? std::atof(argv[3]) : 2;
  const int rate = argc > 4 ? std::atoi(argv[4]) : 48000, block = 256, total = static_cast<int>(seconds * rate);
  lemnos_engine* engine = lemnos_create();
  lemnos_prepare(engine, rate, block);
  std::vector<float> left(block), right(block);
  std::vector<std::int16_t> pcm;
  double peak = 0;
  lemnos_note_on(engine, note, 0.8);
  for (int done = 0; done < total; done += block) {
    if (done >= total * 3 / 4 && done - block < total * 3 / 4) lemnos_note_off(engine, note);  // let go for the last quarter
    const int n = total - done < block ? total - done : block;
    lemnos_process(engine, left.data(), right.data(), n);
    for (int i = 0; i < n; i++) {
      peak = std::fmax(peak, std::fmax(std::fabs(left[i]), std::fabs(right[i])));
      pcm.push_back(static_cast<std::int16_t>(std::lround(std::fmax(-1.0, std::fmin(1.0, left[i])) * 32767)));
      pcm.push_back(static_cast<std::int16_t>(std::lround(std::fmax(-1.0, std::fmin(1.0, right[i])) * 32767)));
    }
  }
  lemnos_destroy(engine);
  std::FILE* f = std::fopen(argv[1], "wb");
  if (!f) { std::perror(argv[1]); return 1; }
  const std::uint32_t bytes = static_cast<std::uint32_t>(pcm.size() * 2);
  std::fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
  put32(f, 16); put16(f, 1); put16(f, 2); put32(f, rate); put32(f, rate * 4); put16(f, 4); put16(f, 16);
  std::fwrite("data", 1, 4, f); put32(f, bytes);
  for (std::int16_t s : pcm) put16(f, s);
  std::fclose(f);
  std::printf("wrote %s: %.2f s, peak %.3f%s\n", argv[1], seconds, peak, peak > 1 ? " (clipped: the synth is louder than full scale)" : "");
  return 0;
}

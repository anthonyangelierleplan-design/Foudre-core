// tape-wav : passe un fichier WAV dans le module TAPE (FoudreTape.h), pour comparer au vrai magnetophone.
//   tape-wav entree.wav sortie.wav [drive alc color wow flutter hiss hum mix mono]     (valeurs 0..1, mono 0/1)
//   "philips" a la place des reglages = reperes du Philips D6410 (drive 0.5, alc 1, color 1, wow 0.5, flutter 0.5, hiss 1, hum 0.1, mix 1, mono 1)
// Lit les WAV 16 / 24 / 32 bits (entier ou flottant), ecrit en 32 bits flottant. Sans JUCE.
#define _CRT_SECURE_NO_WARNINGS
#include "FoudreTape.h"
#include <cstdio>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static bool litWav (const char* nom, std::vector<std::vector<float>>& ch, int& sr)
{
    FILE* f = std::fopen (nom, "rb"); if (! f) return false;
    std::vector<unsigned char> d; unsigned char b[65536]; size_t n;
    while ((n = std::fread (b, 1, sizeof b, f)) > 0) d.insert (d.end(), b, b + n);
    std::fclose (f);
    auto u16 = [&] (size_t i) { return (unsigned) d[i] | ((unsigned) d[i + 1] << 8); };
    auto u32 = [&] (size_t i) { return u16 (i) | (u16 (i + 2) << 16); };
    if (d.size() < 12 || std::memcmp (d.data(), "RIFF", 4) || std::memcmp (d.data() + 8, "WAVE", 4)) return false;
    int fmt = 0, nc = 0, bits = 0; size_t i = 12;
    while (i + 8 <= d.size())
    {
        const size_t len = u32 (i + 4);
        if (! std::memcmp (d.data() + i, "fmt ", 4)) { fmt = (int) u16 (i + 8); nc = (int) u16 (i + 10); sr = (int) u32 (i + 12); bits = (int) u16 (i + 22); if (fmt == 0xFFFE) fmt = (int) u16 (i + 32); }
        else if (! std::memcmp (d.data() + i, "data", 4))
        {
            const size_t bps = (size_t) bits / 8, frames = std::min (len, d.size() - i - 8) / (bps * (size_t) nc);
            ch.assign ((size_t) nc, std::vector<float> (frames));
            const unsigned char* p = d.data() + i + 8;
            for (size_t k = 0; k < frames; ++k)
                for (int c = 0; c < nc; ++c, p += bps)
                {
                    float v = 0.f;
                    if (fmt == 3 && bits == 32) std::memcpy (&v, p, 4);
                    else if (bits == 16) v = (float) (int16_t) (p[0] | (p[1] << 8)) / 32768.f;
                    else if (bits == 24) { int32_t x = p[0] | (p[1] << 8) | (p[2] << 16); if (x & 0x800000) x -= 0x1000000; v = (float) x / 8388608.f; }
                    else if (bits == 32) { int32_t x; std::memcpy (&x, p, 4); v = (float) ((double) x / 2147483648.0); }
                    ch[(size_t) c][k] = v;
                }
            return true;
        }
        i += 8 + len + (len & 1);
    }
    return false;
}

static bool ecritWav (const char* nom, const std::vector<std::vector<float>>& ch, int sr)
{
    FILE* f = std::fopen (nom, "wb"); if (! f) return false;
    const unsigned nc = (unsigned) ch.size(), frames = (unsigned) ch[0].size(), data = frames * nc * 4;
    auto w32 = [f] (unsigned v) { std::fwrite (&v, 4, 1, f); }; auto w16 = [f] (unsigned v) { unsigned short s = (unsigned short) v; std::fwrite (&s, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + data); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16); w16 (3); w16 (nc); w32 ((unsigned) sr); w32 ((unsigned) sr * nc * 4); w16 (nc * 4); w16 (32);
    std::fwrite ("data", 1, 4, f); w32 (data);
    for (unsigned k = 0; k < frames; ++k) for (unsigned c = 0; c < nc; ++c) std::fwrite (&ch[c][k], 4, 1, f);
    std::fclose (f); return true;
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("tape-wav entree.wav sortie.wav [philips | drive alc color wow flutter hiss hum mix mono]\n"); return 2; }
    std::vector<std::vector<float>> a; int sr = 48000;
    if (! litWav (argv[1], a, sr)) { std::fprintf (stderr, "ERREUR : lecture de %s\n", argv[1]); return 2; }
    foudre::tape::Reglages r;
    if (argc > 3 && std::string (argv[3]) == "philips") { r.drive = 0.5f; r.alc = 1.f; r.color = 1.f; r.wow = 0.5f; r.flutter = 0.5f; r.hiss = 1.f; r.hum = 0.1f; r.mix = 1.f; r.mono = true; }
    else
    {
        float* v[8] { &r.drive, &r.alc, &r.color, &r.wow, &r.flutter, &r.hiss, &r.hum, &r.mix };
        for (int k = 0; k < 8 && 3 + k < argc; ++k) *v[k] = (float) std::atof (argv[3 + k]);
        if (argc > 11) r.mono = std::atoi (argv[11]) != 0;
    }
    auto t = std::make_unique<foudre::tape::Tape>();
    t->prepare ((double) sr);
    const size_t n = a[0].size(); const int bloc = 512;
    for (size_t i = 0; i < n; i += bloc)
    {
        const int m = (int) std::min ((size_t) bloc, n - i);
        t->process (a[0].data() + i, a.size() > 1 ? a[1].data() + i : nullptr, m, r);
    }
    if (! ecritWav (argv[2], a, sr)) { std::fprintf (stderr, "ERREUR : ecriture de %s\n", argv[2]); return 2; }
    std::printf ("ok : %s (%d Hz, %zu canaux, %.1f s) drive %.2f alc %.2f color %.2f wow %.2f flutter %.2f hiss %.2f hum %.2f mix %.2f mono %d\n",
                 argv[2], sr, a.size(), (double) n / sr, r.drive, r.alc, r.color, r.wow, r.flutter, r.hiss, r.hum, r.mix, r.mono ? 1 : 0);
    return 0;
}

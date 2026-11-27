// Module TAPE : transparent a zero, calage sur le Philips D6410 (saturation, souffle, compression, pleurage), pas de crash ni de NaN.
#include "FoudreTape.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace foudre::tape;
static const double PI = 3.14159265358979323846;

static std::vector<float> sinus (double f, double amp, double sec, double fs)
{
    std::vector<float> x ((size_t) (sec * fs));
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin (2 * PI * f * (double) i / fs));
    return x;
}
static void passe (Tape& t, std::vector<float>& L, std::vector<float>& R, const Reglages& r)
{
    for (size_t i = 0; i < L.size(); i += 256) t.process (L.data() + i, R.data() + i, (int) std::min<size_t> (256, L.size() - i), r);
}
// amplitude d'une frequence (fenetre de Hann, integration sur une bande)
static double amplitude (const std::vector<float>& x, size_t a, size_t n, double f, double fs)
{
    double re = 0, im = 0, wsum = 0;
    for (size_t i = 0; i < n; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * PI * (double) i / (double) n); re += w * x[a + i] * std::cos (2 * PI * f * (double) i / fs); im += w * x[a + i] * std::sin (2 * PI * f * (double) i / fs); wsum += w; }
    return 2 * std::sqrt (re * re + im * im) / wsum;
}
static double rms (const std::vector<float>& x, size_t a, size_t n) { double e = 0; for (size_t i = a; i < a + n; ++i) e += (double) x[i] * x[i]; return std::sqrt (e / (double) n); }
static double db (double v) { return 20 * std::log10 (std::max (v, 1e-12)); }

int main()
{
    int fail = 0;
    auto chk = [&] (bool ok, const char* quoi, double v) { std::printf ("%s %-62s %.2f\n", ok ? "ok  " : "FAIL", quoi, v); if (! ok) ++fail; };
    const double fs = 48000;

    { // 1. tout a zero (MIX a 1) : sortie = entree retardee de kLat echantillons, exactement
        auto t = std::make_unique<Tape>(); t->prepare (fs);
        auto L = sinus (440, 0.5, 1.0, fs), R = sinus (660, 0.3, 1.0, fs); const auto L0 = L, R0 = R;
        Reglages r; passe (*t, L, R, r);
        double e = 0; for (size_t i = Tape::kLat; i < L.size(); ++i) e = std::max (e, (double) std::fabs (L[i] - L0[i - Tape::kLat]) + std::fabs (R[i] - R0[i - Tape::kLat]));
        chk (e == 0.0, "tout a zero : sortie = entree (retard fixe de 36 echantillons)", e);
    }
    { // 2. DRIVE 50 % sans ALC, sinus au niveau de reference (-20 dBFS rms) : H3 du Philips avant COLOR (-29 dB)
        for (int k = 0; k < 2; ++k)
        {
            auto t = std::make_unique<Tape>(); t->prepare (fs);
            auto L = sinus (1000, 0.1414, 2.0, fs), R = L;
            Reglages r; r.drive = k == 0 ? 0.5f : 1.f; passe (*t, L, R, r);
            const size_t a = (size_t) fs, n = 24000;
            const double h1 = amplitude (L, a, n, 1000, fs), h3 = amplitude (L, a, n, 3000, fs);
            const double cible = k == 0 ? -29.1 : -22.7;
            chk (std::fabs (db (h3 / h1) - cible) < 1.0, k == 0 ? "DRIVE 50 % : H3 = passe propre du Philips (-29 dB avant COLOR)" : "DRIVE 100 % : H3 = passe saturee du Philips (-23 dB avant COLOR)", db (h3 / h1));
        }
    }
    { // 3. HISS 100 % : souffle du Philips, -40,7 dBFS ; HISS 50 % : 12 dB plus bas (loi au carre)
        for (int k = 0; k < 2; ++k)
        {
            auto t = std::make_unique<Tape>(); t->prepare (fs);
            std::vector<float> L ((size_t) (3 * fs), 0.f), R = L;
            Reglages r; r.hiss = k == 0 ? 1.f : 0.5f; passe (*t, L, R, r);
            const double v = db (rms (L, (size_t) fs, (size_t) (2 * fs)));
            chk (std::fabs (v - (k == 0 ? -40.7 : -52.7)) < 1.0, k == 0 ? "HISS 100 % : souffle du Philips (-40,7 dBFS)" : "HISS 50 % : souffle discret (-52,7 dBFS)", v);
        }
    }
    { // 4. ALC 100 % : 19 dB d'ecart a l'entree -> moins de 5 dB a la sortie
        double out[2];
        for (int k = 0; k < 2; ++k)
        {
            auto t = std::make_unique<Tape>(); t->prepare (fs);
            auto L = sinus (1000, k == 0 ? 0.1 : 0.891, 3.0, fs), R = L;
            Reglages r; r.alc = 1.f; passe (*t, L, R, r);
            out[k] = db (rms (L, (size_t) (2 * fs), (size_t) (0.5 * fs)));
        }
        chk (out[1] - out[0] < 5.0 && out[1] - out[0] > 2.0, "ALC 100 % : entree -20 / -1 dBFS -> ecart de sortie (dB)", out[1] - out[0]);
    }
    { // 5. COLOR 100 % : grave en moins, bosse d'aigus (rapport a 1 kHz : 160 Hz -10 dB, 8 kHz +10 dB)
        auto mesure = [&] (double f)
        {
            auto t = std::make_unique<Tape>(); t->prepare (fs);
            auto L = sinus (f, 0.1, 1.0, fs), R = L; Reglages r; r.color = 1.f; passe (*t, L, R, r);
            return db (amplitude (L, 24000, 19200, f, fs) / 0.1);
        };
        const double r1k = mesure (1000), r160 = mesure (160) - r1k, r8k = mesure (8000) - r1k;
        chk (std::fabs (r160 + 10.0) < 1.5, "COLOR 100 % : 160 Hz par rapport a 1 kHz (Philips -10 dB)", r160);
        chk (std::fabs (r8k - 9.9) < 1.5, "COLOR 100 % : 8 kHz par rapport a 1 kHz (Philips +9,9 dB)", r8k);
    }
    { // 6. tout a fond, signal fort : rien d'anormal (ni NaN ni valeur folle), MONO : gauche = droite
        auto t = std::make_unique<Tape>(); t->prepare (44100);
        auto L = sinus (110, 0.99, 3.0, 44100), R = sinus (1760, 0.99, 3.0, 44100);
        Reglages r; r.drive = r.alc = r.color = r.wow = r.flutter = r.hiss = r.hum = r.mix = 1.f; r.mono = true; passe (*t, L, R, r);
        double pk = 0, diff = 0; bool fini = true;
        for (size_t i = 0; i < L.size(); ++i) { fini = fini && std::isfinite (L[i]) && std::isfinite (R[i]); pk = std::max (pk, (double) std::fabs (L[i])); diff = std::max (diff, (double) std::fabs (L[i] - R[i])); }
        chk (fini && pk < 2.0, "tout a fond : sortie finie et bornee (crete)", pk);
        chk (diff < 1e-4, "MONO : gauche = droite (a l'arrondi pres)", diff);
    }
    std::printf ("%d echec(s)\n", fail);
    return fail == 0 ? 0 : 1;
}

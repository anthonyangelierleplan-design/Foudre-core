// Les mesures du banc d'essai disent-elles vrai ? Signaux connus -> valeurs attendues.
#include "Mesures.h"
#include "FoudreRegles.h"
#include <cstdio>

using namespace foudre;

static Audio sinus (double f, double amp, double duree, int canaux = 2, double sr = 48000.0)
{
    Audio a; a.sr = sr;
    std::vector<float> x ((size_t) (duree * sr));
    for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (amp * std::sin (2.0 * pi * f * (double) i / sr));
    a.ch.assign ((size_t) canaux, x);
    return a;
}

int main()
{
    int fail = 0;
    auto chk = [&] (bool ok, const char* quoi, double v) { std::printf ("%s %-55s %.2f\n", ok ? "ok  " : "FAIL", quoi, v); if (! ok) ++fail; };

    // Loudness : sinus 1 kHz a 0 dBFS sur un canal = -3,01 LUFS (definition de la norme BS.1770) ; en stereo = 0 LUFS
    { auto a = sinus (997.0, 1.0, 5.0, 1); const auto n = mesureNiveaux (a); chk (std::fabs (n.lufsIntegre + 3.01) < 0.1, "sinus 0 dBFS mono -> -3.01 LUFS", n.lufsIntegre); }
    { auto a = sinus (997.0, 0.1, 5.0, 2); const auto n = mesureNiveaux (a); chk (std::fabs (n.lufsIntegre + 20.0) < 0.1, "sinus -20 dBFS stereo -> -20 LUFS", n.lufsIntegre); }
    { auto a = sinus (997.0, 0.1, 5.0, 2, 44100.0); const auto n = mesureNiveaux (a); chk (std::fabs (n.lufsIntegre + 20.0) < 0.1, "idem a 44,1 kHz", n.lufsIntegre); }
    { auto a = sinus (440.0, 0.5, 2.0); const auto n = mesureNiveaux (a);
      chk (std::fabs (n.creteDb + 6.02) < 0.05, "crete sinus 0,5 -> -6,02 dBFS", n.creteDb);
      chk (std::fabs (n.rmsDb + 9.03) < 0.05, "RMS sinus 0,5 -> -9,03 dBFS", n.rmsDb); }

    // Clics : un sinus propre n'en a pas ; un saut brutal au milieu en donne un, au bon endroit
    { auto a = sinus (220.0, 0.5, 2.0); chk (detecteClics (a).empty(), "sinus propre : aucun clic", (double) detecteClics (a).size()); }
    { auto a = sinus (220.0, 0.5, 2.0); for (auto& c : a.ch) for (size_t i = 48000; i < c.size(); ++i) c[i] = -c[i];   // inversion de phase a 1 s
      const auto cl = detecteClics (a);
      chk (cl.size() == 1 && std::fabs (cl[0].t - 1.0) < 0.01, "saut a 1 s : un clic a 1 s", cl.empty() ? -1.0 : cl[0].t); }
    { Audio a; a.sr = 48000; std::vector<float> x (96000);          // dent de scie 110 Hz : un saut par periode, ce n'est pas un clic
      for (size_t i = 0; i < x.size(); ++i) { const double ph = std::fmod (110.0 * (double) i / 48000.0, 1.0); x[i] = (float) (0.5 * (2 * ph - 1)); }
      a.ch = { x, x }; chk (detecteClics (a).empty(), "dent de scie : aucun clic", (double) detecteClics (a).size());
      auto b = a; for (auto& c : b.ch) for (size_t i = 48000; i < c.size(); ++i) c[i] *= (float) std::exp (-(double) (i - 48000) / 4800.0);   // fin en 100 ms
      chk (detecteClics (b).empty(), "dent de scie qui s'eteint en douceur : aucun clic", (double) detecteClics (b).size());
      for (auto& c : a.ch) std::fill (c.begin() + 48000, c.end(), 0.f);                                                            // coupee net a 1 s
      const auto cl = detecteClics (a);
      chk (cl.size() == 1 && cl[0].genre == Clic::coupure && std::fabs (cl[0].t - 1.0) < 0.01, "dent de scie coupee net : une coupure a 1 s", cl.empty() ? -1.0 : cl[0].t); }

    // Spectre : un sinus 1 kHz est dans l'octave de 1 kHz ; sa brillance vaut 1 kHz
    { auto a = sinus (1000.0, 0.5, 2.0); const auto s = analyseSpectre (a);
      size_t best = 0; for (size_t i = 0; i < s.bandesDb.size(); ++i) if (s.bandesDb[i] > s.bandesDb[best]) best = i;
      chk (std::fabs (s.centres[best] - 1000.0) < 1.0, "sinus 1 kHz : octave la plus forte = 1 kHz", s.centres[best]);
      chk (std::fabs (s.centroide - 1000.0) < 20.0, "sinus 1 kHz : centroide 1 kHz", s.centroide); }

    // Distorsion : un sinus pur mesure tres bas ; avec 1 % d'harmonique 3 -> -40 dB
    { auto a = sinus (1000.0, 0.125, 2.0); const double d = distorsionDb (a, 1000.0); chk (d < -80.0, "sinus pur : distorsion < -80 dB", d); }
    { auto a = sinus (1000.0, 0.125, 2.0); auto h = sinus (3000.0, 0.00125, 2.0);
      for (size_t c = 0; c < a.ch.size(); ++c) for (size_t i = 0; i < a.ch[c].size(); ++i) a.ch[c][i] += h.ch[c][i];
      const double d = distorsionDb (a, 1000.0); chk (std::fabs (d + 40.0) < 0.5, "1 % d'harmonique 3 -> -40 dB", d); }

    // Regles de la gamme
    chk (regles::macroDuCc (16) == 1 && regles::macroDuCc (23) == 8 && regles::macroDuCc (24) == 0 && regles::macroDuCc (15) == 0, "CC 16..23 = macros 1..8", 0);
    chk (regles::ccDeLaMacro (1) == 16 && regles::ccDeLaMacro (8) == 23 && regles::ccDeLaMacro (9) == -1, "macro 1..8 = CC 16..23", 0);
    chk (regles::aimanteAuZero (0.51, 0.5) == 0.5 && regles::aimanteAuZero (0.6, 0.5) == 0.6, "cran magnetique au zero", 0);

    std::printf ("%d echec(s)\n", fail);
    return fail == 0 ? 0 : 1;
}

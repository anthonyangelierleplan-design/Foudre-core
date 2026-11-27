#pragma once
// Mesures audio du banc d'essai Foudre Audio : niveaux, loudness (LUFS), clics, spectre, distorsion.
// C++17 pur, sans JUCE : se teste seul (tools/tests/mesures.cpp).

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace foudre
{
constexpr double pi = 3.14159265358979323846;

/** Un son en memoire : un tableau d'echantillons par canal. */
struct Audio
{
    double sr = 48000.0;
    std::vector<std::vector<float>> ch;

    size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
    double duree() const  { return sr > 0 ? (double) frames() / sr : 0.0; }

    std::vector<double> mono() const
    {
        std::vector<double> m (frames(), 0.0);
        if (ch.empty()) return m;
        for (auto& c : ch) for (size_t i = 0; i < m.size(); ++i) m[i] += c[i];
        for (auto& v : m) v /= (double) ch.size();
        return m;
    }
};

inline double ampEnDb (double a)       { return a > 1e-12 ? 20.0 * std::log10 (a) : -240.0; }
inline double puissanceEnDb (double p) { return p > 1e-24 ? 10.0 * std::log10 (p) : -240.0; }

// ------------------------------------------------------------------ filtre du 2e ordre (forme directe transposee)
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double tick (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// Ponderation K de la norme ITU-R BS.1770 (meme formulation que pyloudnorm), pour n'importe quelle frequence d'echantillonnage.
inline Biquad ponderationK1 (double sr)    // etage 1 : plateau dans les aigus (+4 dB, effet de la tete)
{
    const double G = 4.0, Q = 1.0 / std::sqrt (2.0), fc = 1500.0;
    const double A = std::pow (10.0, G / 40.0), w0 = 2.0 * pi * fc / sr, c = std::cos (w0), al = std::sin (w0) / (2.0 * Q), sA = std::sqrt (A);
    const double a0 = (A + 1) - (A - 1) * c + 2 * sA * al;
    Biquad f;
    f.b0 = A * ((A + 1) + (A - 1) * c + 2 * sA * al) / a0;
    f.b1 = -2 * A * ((A - 1) + (A + 1) * c) / a0;
    f.b2 = A * ((A + 1) + (A - 1) * c - 2 * sA * al) / a0;
    f.a1 = 2 * ((A - 1) - (A + 1) * c) / a0;
    f.a2 = ((A + 1) - (A - 1) * c - 2 * sA * al) / a0;
    return f;
}

inline Biquad ponderationK2 (double sr)    // etage 2 : passe-haut 38 Hz (courbe RLB)
{
    const double Q = 0.5, fc = 38.0, w0 = 2.0 * pi * fc / sr, c = std::cos (w0), al = std::sin (w0) / (2.0 * Q);
    const double a0 = 1 + al;
    Biquad f;
    f.b0 = (1 + c) / 2 / a0; f.b1 = -(1 + c) / a0; f.b2 = (1 + c) / 2 / a0;
    f.a1 = -2 * c / a0;      f.a2 = (1 - al) / a0;
    return f;
}

// ------------------------------------------------------------------ niveaux
struct Niveaux
{
    double creteDb = -240, rmsDb = -240, dc = 0;
    double lufsIntegre = -240, lufsMomentaneMax = -240, lufsCourtTermeMax = -240;
    double debut = 0, fin = 0;          // valeur absolue du premier / dernier echantillon (un saut = un clic)
    double finDb = -240;                // niveau RMS des 100 dernieres ms (souffle ou queue qui ne s'eteint pas)
    int ecretes = 0;                    // echantillons a 0 dBFS ou plus
};

inline Niveaux mesureNiveaux (const Audio& a)
{
    Niveaux r;
    const size_t n = a.frames();
    if (n == 0) return r;

    double crete = 0, somme2 = 0, somme = 0;
    for (auto& c : a.ch)
        for (float v : c)
        {
            const double x = v;
            crete = std::max (crete, std::fabs (x)); somme2 += x * x; somme += x;
            if (std::fabs (x) >= 0.999) ++r.ecretes;
        }
    const double total = (double) n * (double) a.ch.size();
    r.creteDb = ampEnDb (crete);
    r.rmsDb   = puissanceEnDb (somme2 / total);
    r.dc      = somme / total;
    for (auto& c : a.ch) { r.debut = std::max (r.debut, (double) std::fabs (c.front())); r.fin = std::max (r.fin, (double) std::fabs (c.back())); }

    const size_t n100 = std::min (n, (size_t) std::llround (0.1 * a.sr));
    double s = 0; for (auto& c : a.ch) for (size_t i = n - n100; i < n; ++i) s += (double) c[i] * c[i];
    r.finDb = puissanceEnDb (s / ((double) n100 * (double) a.ch.size()));

    // Loudness BS.1770 : ponderation K, somme des puissances des canaux, blocs de 400 ms (pas de 100 ms), double porte -70 LUFS / -10 LU
    std::vector<double> cumul (n + 1, 0.0);
    {
        std::vector<double> p (n, 0.0);
        for (auto& c : a.ch)
        {
            auto k1 = ponderationK1 (a.sr), k2 = ponderationK2 (a.sr);
            for (size_t i = 0; i < n; ++i) { const double y = k2.tick (k1.tick (c[i])); p[i] += y * y; }
        }
        for (size_t i = 0; i < n; ++i) cumul[i + 1] = cumul[i] + p[i];
    }
    auto moyenne = [&] (size_t debut, size_t lg) { return (cumul[debut + lg] - cumul[debut]) / (double) lg; };
    auto enLufs  = [] (double p) { return p > 1e-24 ? -0.691 + 10.0 * std::log10 (p) : -240.0; };

    const size_t pas = std::max<size_t> (1, (size_t) std::llround (0.1 * a.sr));
    const size_t b400 = std::max<size_t> (1, std::min (n, (size_t) std::llround (0.4 * a.sr)));
    const size_t b3s  = std::max<size_t> (1, std::min (n, (size_t) std::llround (3.0 * a.sr)));

    std::vector<double> blocs;
    for (size_t d = 0; d + b400 <= n; d += pas) blocs.push_back (moyenne (d, b400));
    for (double p : blocs) r.lufsMomentaneMax = std::max (r.lufsMomentaneMax, enLufs (p));
    for (size_t d = 0; d + b3s <= n; d += pas) r.lufsCourtTermeMax = std::max (r.lufsCourtTermeMax, enLufs (moyenne (d, b3s)));

    double sAbs = 0; int nAbs = 0;
    for (double p : blocs) if (enLufs (p) > -70.0) { sAbs += p; ++nAbs; }
    if (nAbs > 0)
    {
        const double seuilRelatif = enLufs (sAbs / nAbs) - 10.0;
        double sRel = 0; int nRel = 0;
        for (double p : blocs) if (enLufs (p) > -70.0 && enLufs (p) > seuilRelatif) { sRel += p; ++nRel; }
        if (nRel > 0) r.lufsIntegre = enLufs (sRel / nRel);
    }
    return r;
}

// ------------------------------------------------------------------ clics
// Methode : on decoupe le son en tranches de 5 ms et on regarde, dans chaque tranche, le plus grand "coup de fouet"
// (variation brutale de la pente du signal). Un clic est une tranche ou ce coup de fouet est bien plus fort que
// dans les tranches voisines. Une dent de scie a un saut a chaque periode : ses voisines en ont autant, elle ne compte pas.
// Une coupure nette (le son tombe de plus de 30 dB en 5 ms) et une attaque seche (il monte d'autant) s'entendent
// aussi comme un clic : elles sont signalees a part.
struct Clic
{
    enum Genre { saut, coupure, attaque };
    double t = 0;              // instant (secondes)
    double force = 0;          // saut : combien de fois plus fort qu'autour ; coupure / attaque : variation en dB
    Genre genre = saut;
};

inline std::vector<Clic> detecteClics (const Audio& a, size_t maxClics = 20)
{
    std::vector<Clic> r;
    const size_t n = a.frames();
    const size_t tr = std::max<size_t> (16, (size_t) std::llround (a.sr * 0.005));
    if (n < 3 * tr) return r;
    const size_t nt = n / tr;

    std::vector<double> m (nt, 0.0), e (nt, 0.0);
    for (auto& c : a.ch)
        for (size_t i = 2; i < nt * tr; ++i)
        {
            const double d2 = std::fabs ((double) c[i] - 2.0 * c[i - 1] + c[i - 2]);
            m[i / tr] = std::max (m[i / tr], d2);
            e[i / tr] += (double) c[i] * c[i];
        }
    for (auto& v : e) v = std::sqrt (v / (double) (tr * a.ch.size()));

    auto mediane = [] (std::vector<double>& v) { if (v.empty()) return 0.0; std::nth_element (v.begin(), v.begin() + (long) (v.size() / 2), v.end()); return v[v.size() / 2]; };
    auto ajoute = [&] (double t, double force, Clic::Genre g)
    {
        if (! r.empty() && t - r.back().t < 0.012) { r.back().force = std::max (r.back().force, force); return; }   // meme evenement sur deux tranches
        if (r.size() < maxClics) r.push_back ({ t, force, g });
    };

    const double seuilAbs = 0.003;     // environ -50 dB : en dessous, on n'entend pas de clic
    const int voisins = 20;            // 100 ms de chaque cote
    std::vector<double> avant, apres;
    for (size_t f = 1; f < nt && r.size() < maxClics; ++f)
    {
        const double t = (double) (f * tr) / a.sr;

        // coupure nette / attaque seche : niveau de la tranche compare a la precedente
        const double dEn = ampEnDb (e[f]) - ampEnDb (e[f - 1]);
        if (e[f - 1] > 0.01 && dEn < -30.0) { ajoute (t, -dEn, Clic::coupure); continue; }
        if (e[f] > 0.01 && dEn > 30.0)      { ajoute (t, dEn, Clic::attaque);  continue; }

        // saut isole : plus fort que ce qui precede ET que ce qui suit
        if (m[f] < seuilAbs) continue;
        avant.clear(); apres.clear();
        for (int k = 2; k <= voisins; ++k)
        {
            if ((long) f - k >= 0) avant.push_back (m[f - (size_t) k]);
            if (f + (size_t) k < nt) apres.push_back (m[f + (size_t) k]);
        }
        const double ref = std::max ({ mediane (avant), mediane (apres), 1e-6 });
        const double rapport = m[f] / ref;
        if (rapport >= 6.0) ajoute (t, rapport, Clic::saut);
    }
    return r;
}

// ------------------------------------------------------------------ spectre
inline void fft (std::vector<std::complex<double>>& x)     // FFT radix 2, taille puissance de 2
{
    const size_t n = x.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (x[i], x[j]);
    }
    for (size_t lg = 2; lg <= n; lg <<= 1)
    {
        const std::complex<double> w (std::cos (-2.0 * pi / (double) lg), std::sin (-2.0 * pi / (double) lg));
        for (size_t i = 0; i < n; i += lg)
        {
            std::complex<double> wk (1.0, 0.0);
            for (size_t k = 0; k < lg / 2; ++k)
            {
                const auto u = x[i + k], t = wk * x[i + k + lg / 2];
                x[i + k] = u + t; x[i + k + lg / 2] = u - t; wk *= w;
            }
        }
    }
}

/** Puissance moyenne par case de frequence (methode de Welch : fenetres de 8192 points, recouvrement 50 %).
    Fenetre de Hann par defaut ; Blackman-Harris (fuites a -92 dB) pour mesurer une faible distorsion. */
inline std::vector<double> spectrePuissance (const std::vector<double>& x, size_t N = 8192, bool blackmanHarris = false)
{
    std::vector<double> p (N / 2 + 1, 0.0), w (N);
    for (size_t i = 0; i < N; ++i)
    {
        const double ph = 2.0 * pi * (double) i / (double) N;
        w[i] = blackmanHarris ? 0.35875 - 0.48829 * std::cos (ph) + 0.14128 * std::cos (2 * ph) - 0.01168 * std::cos (3 * ph)
                              : 0.5 - 0.5 * std::cos (ph);
    }
    std::vector<std::complex<double>> buf (N);
    int nb = 0;
    for (size_t d = 0; nb == 0 || d + N <= x.size(); d += N / 2)
    {
        for (size_t i = 0; i < N; ++i) buf[i] = (d + i < x.size() ? x[d + i] : 0.0) * w[i];
        fft (buf);
        for (size_t k = 0; k <= N / 2; ++k) p[k] += std::norm (buf[k]);
        ++nb;
        if (d + N >= x.size()) break;
    }
    for (auto& v : p) v /= (double) nb;
    return p;
}

struct Spectre
{
    std::vector<double> centres;    // centre de chaque octave (Hz)
    std::vector<double> bandesDb;   // part de l'energie dans chaque octave (dB par rapport au total)
    double centroide = 0;           // "centre de gravite" du spectre (Hz) : plus il est haut, plus le son est brillant
    double aigusDb = -240;          // part de l'energie au-dessus de 5 kHz (dB)
    double grosDb  = -240;          // part de l'energie en dessous de 120 Hz (dB)
};

inline Spectre analyseSpectre (const Audio& a)
{
    Spectre s;
    const size_t N = 8192;
    const auto p = spectrePuissance (a.mono(), N);
    const double df = a.sr / (double) N;
    double total = 0, pondere = 0, aigus = 0, gros = 0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        const double f = (double) k * df;
        if (f < 20.0 || f > 20000.0) continue;
        total += p[k]; pondere += f * p[k];
        if (f >= 5000.0) aigus += p[k];
        if (f < 120.0) gros += p[k];
    }
    for (double fc = 31.25; fc < 20000.0 && fc < a.sr / 2; fc *= 2.0)
    {
        double b = 0;
        for (size_t k = 1; k < p.size(); ++k) { const double f = (double) k * df; if (f >= fc / std::sqrt (2.0) && f < fc * std::sqrt (2.0)) b += p[k]; }
        s.centres.push_back (fc);
        s.bandesDb.push_back (total > 0 ? puissanceEnDb (b / total) : -240.0);
    }
    if (total > 0) { s.centroide = pondere / total; s.aigusDb = puissanceEnDb (aigus / total); s.grosDb = puissanceEnDb (gros / total); }
    return s;
}

/** Harmoniques et bruit ajoutes a une sinusoide de frequence f0 : energie hors de f0 par rapport a celle de f0 (dB). */
inline double distorsionDb (const Audio& a, double f0)
{
    const size_t N = 8192;
    const auto p = spectrePuissance (a.mono(), N, true);
    const double df = a.sr / (double) N, largeur = std::max (30.0, 6.0 * df);
    double dedans = 0, dehors = 0;
    for (size_t k = 1; k < p.size(); ++k)
    {
        const double f = (double) k * df;
        if (f < 20.0 || f > 20000.0) continue;
        (std::fabs (f - f0) <= largeur ? dedans : dehors) += p[k];
    }
    return dedans > 0 ? puissanceEnDb (dehors / dedans) : 0.0;
}
} // namespace foudre

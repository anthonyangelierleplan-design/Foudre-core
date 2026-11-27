#pragma once
// Module TAPE de la gamme Foudre Audio : un magnetophone a cassette, cale sur un Philips D6410 (cassette type I) mesure au banc
// (voir C:\Dev\TAPE-mesure et tools/tape). Ecrit une seule fois pour Orage et Zephyr. C++17 pur, sans JUCE, sans allocation dans process().
//
// Chaine (comme dans la machine) : MONO -> ALC (compression automatique d'enregistrement) -> DRIVE (saturation de bande, x4)
//                                  -> COLOR (couleur du Philips) -> WOW / FLUTTER (defilement) -> HISS / HUM (lecture) -> MIX
// Tous les potards a zero (MIX compris) : le son passe sans changement.
// Reperes "Philips" : DRIVE 50 % = passe propre (H3 -24 dB), 100 % = passe saturee (H3 -17 dB) ; COLOR 100 % ; WOW et FLUTTER 50 % ;
//                     HISS 100 % = souffle du Philips (-41 dBFS) ; HUM 10 % = ronflement du Philips (100 % = 10 fois plus fort).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace foudre::tape
{
struct Reglages
{
    float drive = 0.f, alc = 0.f, color = 0.f, wow = 0.f, flutter = 0.f, hiss = 0.f, hum = 0.f, mix = 1.f;   // 0..1
    bool mono = false;
};

namespace detail
{
constexpr double kPi = 3.14159265358979323846;

// ---- mesures du Philips D6410 (C:\Dev\TAPE-mesure\analyse : calibre.py, analyse.py)
// COLOR : passe-haut 2e ordre, plateau, cloche d'aigus, passe-bas 4e ordre (ecart a la mesure : 1,3 dB rms de 40 Hz a 20 kHz)
constexpr float kHpF = 235.f, kHpQ = 0.52f, kShF = 1508.f, kShG = 2.1f, kPkF = 7746.f, kPkQ = 0.93f, kPkG = 12.f, kLpF = 16024.f, kLpQ = 0.40f;
// saturation : u = k x ; y = tanh (u + c u^2) / k   (kA : k x amplitude d'un sinus au niveau ou l'ALC place le son : -20 dBFS rms)
constexpr float kRefAmp = 0.1414f, kKaPropre = 0.683f, kCPropre = 0.074f, kKaSature = 1.037f, kCSature = 0.132f;   // harmoniques avant COLOR (la bosse d'aigus les releve ensuite de 2 a 4 dB)
// pleurage (vitesse relative) : raies mesurees (Hz, amplitude) + composantes aleatoires (echelle du bruit filtre)
constexpr int kNbRaies = 5;
constexpr float kRaieF[kNbRaies] { 1.39f, 6.29f, 7.18f, 9.42f, 71.8f };
constexpr float kRaieA[kNbRaies] { 4.6e-4f, 5.5e-4f, 8.5e-4f, 6.1e-4f, 6.8e-4f };
constexpr bool  kRaieWow[kNbRaies] { true, false, false, false, false };
constexpr float kWowAleaFc = 0.8f, kWowAlea = 0.0621f, kFlAleaFc = 25.f, kFlAlea = 5.7e-4f;
// lecture : souffle (rms) et ronflement (raies 50..400 Hz, amplitude crete)
constexpr float kHissRms = 0.0092f;                       // -40,7 dBFS
constexpr float kHumA[8] { 2.30e-4f, 2.13e-4f, 0.92e-4f, 0.73e-4f, 0.76e-4f, 0.33e-4f, 0.86e-4f, 0.25e-4f };   // recale a l'ecoute du modele : meme emergence que le Philips

struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f, z1 = 0.f, z2 = 0.f;
    float run (float x) noexcept { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void reset() noexcept { z1 = z2 = 0.f; }
    // RBJ : 0 passe-haut, 1 passe-bas, 2 cloche, 3 plateau d'aigus
    void set (int type, double fs, double f0, double q, double gainDb) noexcept
    {
        f0 = std::min (f0, 0.49 * fs);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * kPi * f0 / fs, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        double B0, B1, B2, A0, A1, A2;
        switch (type)
        {
            case 0:  B0 = (1 + c) / 2; B1 = -(1 + c); B2 = (1 + c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case 1:  B0 = (1 - c) / 2; B1 = 1 - c; B2 = (1 - c) / 2; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
            case 2:  B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
            default:
            {
                const double sq = 2.0 * std::sqrt (A) * al;
                B0 = A * ((A + 1) + (A - 1) * c + sq); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sq);
                A0 = (A + 1) - (A - 1) * c + sq; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sq;
            }
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0); a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }
};

// demi-bande (FIR a phase lineaire, fenetre de Kaiser) : x2 en montant, /2 en descendant
struct DemiBande
{
    static constexpr int kN = 49, kC = kN / 2;        // 49 coefficients : retard entier (x4 aller-retour = 36 echantillons)
    float h[kN] {};
    float up[kN] {}, dn[kN] {};
    int pu = 0, pd = 0;
    DemiBande()
    {
        auto i0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
        const double beta = 8.0; double sum = 0;
        for (int i = 0; i < kN; ++i)
        {
            const double m = i - kC, sinc = m == 0 ? 0.5 : std::sin (kPi * 0.5 * m) / (kPi * m);
            const double r = 2.0 * i / (kN - 1) - 1.0, win = i0 (beta * std::sqrt (std::max (0.0, 1 - r * r))) / i0 (beta);
            h[i] = (float) (sinc * win); sum += h[i];
        }
        for (auto& v : h) v = (float) (v / sum);
    }
    void reset() noexcept { for (auto& v : up) v = 0.f; for (auto& v : dn) v = 0.f; pu = pd = 0; }
    static float conv (const float* buf, int p, const float* h) noexcept
    {
        float s = 0.f;
        for (int i = 0; i < kN; ++i) s += h[i] * buf[(p + i) % kN];
        return s;
    }
    void monte (float x, float& y0, float& y1) noexcept                  // 1 echantillon -> 2 (gain 2 pour compenser les zeros inseres)
    {
        pu = (pu + kN - 1) % kN; up[pu] = 2.f * x;  y0 = conv (up, pu, h);
        pu = (pu + kN - 1) % kN; up[pu] = 0.f;      y1 = conv (up, pu, h);
    }
    float descend (float x0, float x1) noexcept                          // 2 echantillons -> 1
    {
        pd = (pd + kN - 1) % kN; dn[pd] = x0;
        pd = (pd + kN - 1) % kN; dn[pd] = x1;
        return conv (dn, pd, h);
    }
};

struct Alea
{
    uint32_t s = 0x9E3779B9u;
    float bi() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) ((double) s / 2147483648.0 - 1.0); }
};
} // namespace detail

class Tape
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        for (auto& c : canal) { c = Canal(); }
        alcEnv = 0.f; alcGain = 1.f;
        for (auto& p : phRaie) p = 0.0;
        phHum = 0.0; wowA1 = wowA2 = flA1 = flA2 = 0.f; centre = 0.f; w = 0;
        hissHp = (float) (1.0 - std::exp (-2.0 * detail::kPi * 200.0 / fs));      // 2 poles : le souffle du Philips a peu de grave
        for (auto& c : canal)
        {
            c.hissSh.set (3, fs, detail::kShF, 0.7, detail::kShG); c.hissPk.set (2, fs, detail::kPkF, detail::kPkQ, detail::kPkG);
            c.hissLp1.set (1, fs, detail::kLpF, detail::kLpQ, 0); c.hissLp2.set (1, fs, detail::kLpF, detail::kLpQ, 0);
        }
        couleur (0.f); colorFaite = -1.f;
        // le souffle filtre doit sortir a kHissRms : on mesure le gain du filtre sur du bruit blanc
        {
            detail::Alea a; detail::Biquad s = canal[0].hissSh, p = canal[0].hissPk, l1 = canal[0].hissLp1, l2 = canal[0].hissLp2; float hp = 0.f, hp2 = 0.f; double e = 0; const int n = (int) fs * 2;
            for (int i = 0; i < n; ++i) { const float x = a.bi(); hp += hissHp * (x - hp); hp2 += hissHp * ((x - hp) - hp2); float y = l2.run (l1.run (p.run (s.run (x - hp - hp2)))); if (i > n / 4) e += (double) y * y; }
            hissNorm = (float) (detail::kHissRms / std::sqrt (e / (n - n / 4 - 1)));
        }
        lissage = (float) (1.0 - std::exp (-1.0 / (0.02 * fs)));
        alcAtt = (float) (1.0 - std::exp (-1.0 / (0.06 * fs)));
        alcRel = (float) (1.0 - std::exp (-1.0 / (0.7 * fs)));
        alcDet = (float) (1.0 - std::exp (-1.0 / (0.01 * fs)));
        wowC1 = (float) (1.0 - std::exp (-2.0 * detail::kPi * detail::kWowAleaFc / fs));
        flC1 = (float) (1.0 - std::exp (-2.0 * detail::kPi * detail::kFlAleaFc / fs));
        centreC = (float) (1.0 - std::exp (-1.0 / (0.4 * fs)));
        dcC = (float) (1.0 - std::exp (-2.0 * detail::kPi * 8.0 / fs));
        premier = true;
    }

    /** Traite un bloc sur place. R peut etre nul (mono). */
    void process (float* L, float* R, int n, const Reglages& r) noexcept
    {
        using namespace detail;
        if (premier) { lis = r; premier = false; }
        const int nc = R != nullptr ? 2 : 1;
        float* io[2] { L, R };
        for (int i = 0; i < n; ++i)
        {
            // reglages lisses (aucun craquement quand on tourne un potard)
            lis.drive += lissage * (r.drive - lis.drive); lis.alc += lissage * (r.alc - lis.alc); lis.color += lissage * (r.color - lis.color);
            lis.wow += lissage * (r.wow - lis.wow); lis.flutter += lissage * (r.flutter - lis.flutter); lis.hiss += lissage * (r.hiss - lis.hiss);
            lis.hum += lissage * (r.hum - lis.hum); lis.mix += lissage * (r.mix - lis.mix);
            if ((i & 31) == 0) blocReglages (r.mono);

            float x[2], dry[2];
            for (int c = 0; c < nc; ++c) dry[c] = x[c] = io[c][i];
            if (mono > 0.f && nc == 2) { const float m = 0.5f * (x[0] + x[1]); x[0] += mono * (m - x[0]); x[1] += mono * (m - x[1]); }

            // ALC : le magnetophone regle lui-meme son niveau (attaque 60 ms, relache 0,7 s, jusqu'a 5:1)
            if (lis.alc > 1e-4f)
            {
                const float pk = nc == 2 ? std::max (x[0] * x[0], x[1] * x[1]) : x[0] * x[0];
                alcEnv += alcDet * (pk - alcEnv);
                const float levDb = 10.f * std::log10 (alcEnv + 1e-12f);                 // niveau rms
                const float ratio = 1.f + 4.f * lis.alc, cibleDb = -20.f;     // -20 dBFS rms : les cretes restent sous 0 dBFS apres COLOR
                float gDb = -(1.f - 1.f / ratio) * (levDb - cibleDb);
                gDb = std::min (gDb, 20.f * lis.alc);                                 // remonte les sons faibles, jusqu'a +20 dB
                if (levDb < -60.f) gDb = std::min (gDb, 20.f * std::log10 (alcGain));  // dans le silence : le gain ne remonte plus
                const float g = std::pow (10.f, gDb / 20.f);
                alcGain += (g < alcGain ? alcAtt : alcRel) * (g - alcGain);
                for (int c = 0; c < nc; ++c) x[c] *= alcGain;
            }
            else alcGain = 1.f;

            for (int c = 0; c < nc; ++c)
            {
                auto& C = canal[c];
                float v = x[c];
                // DRIVE : saturation de bande, suréchantillonnee x4 (retard fixe kLat ; a DRIVE 0, le signal est seulement retarde d'autant)
                {
                    float a0, a1, b[4];
                    C.os1.monte (v, a0, a1);
                    C.os2a.monte (a0, b[0], b[1]); C.os2b.monte (a1, b[2], b[3]);
                    if (satK > 1e-4f) for (float& u : b) { const float t = satK * u; u = std::tanh (t + satC * t * t) / satK; }
                    const float s = C.os1d.descend (C.os2ad.descend (b[0], b[1]), C.os2bd.descend (b[2], b[3]));
                    C.pre[w & 63] = v;
                    if (satK > 1e-4f) { C.dc += dcC * (s - C.dc); v = s - C.dc; }     // l'asymetrie laisse du continu
                    else { C.dc = 0.f; v = C.pre[(w - kLat) & 63]; }
                }
                // COLOR
                if (colorOn) v = C.lp2.run (C.lp1.run (C.pk.run (C.sh.run (C.hp.run (v)))));
                // WOW / FLUTTER : lecture a vitesse variable
                C.buf[w] = v;
                if (centre > 1e-6f)
                {
                    const float rp = (float) w - std::max (2.f, (centre + delaiMod) * (float) fs);
                    const float fl = std::floor (rp); const float fr = rp - fl; const int i0 = (int) fl;
                    auto at = [&C] (int k) { return C.buf[k & kMask]; };
                    const float xm1 = at (i0 - 1), x0 = at (i0), x1 = at (i0 + 1), x2 = at (i0 + 2);
                    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                    v = ((c3 * fr + c2) * fr + c1) * fr + x0;
                }
                // le son direct (MIX) attend autant que la bande : pas d'effet de peigne
                C.dbuf[w] = dry[c];
                {
                    const float rd = (float) w - (float) kLat - centre * (float) fs; const float fd = std::floor (rd); const float frd = rd - fd; const int j0 = (int) fd;
                    dry[c] = C.dbuf[j0 & kMask] + frd * (C.dbuf[(j0 + 1) & kMask] - C.dbuf[j0 & kMask]);
                }
                // HISS / HUM : la lecture ajoute son souffle et le secteur
                if (hissAmp > 0.f)
                {
                    const float z = (mono > 0.5f && c == 1) ? hissMono : C.alea.bi();
                    if (c == 0) hissMono = z;
                    C.hissHpS += hissHp * (z - C.hissHpS); C.hissHpS2 += hissHp * ((z - C.hissHpS) - C.hissHpS2);
                    v += hissAmp * C.hissLp2.run (C.hissLp1.run (C.hissPk.run (C.hissSh.run (z - C.hissHpS - C.hissHpS2))));
                }
                v += humVal;
                io[c][i] = dry[c] + lis.mix * (v - dry[c]);
            }
            // horloges communes (la bande defile pareil a gauche et a droite)
            w = (w + 1) & kMask;
            avance();
        }
    }

    /** Retard total du module (echantillons) : kLat + celui de WOW / FLUTTER (variable). */
    int retard() const noexcept { return kLat + (int) (centre * fs); }

private:
    static constexpr int kLen = 8192, kMask = kLen - 1;
public:
    static constexpr int kLat = 36;                     // retard fixe du module (echantillons) : suréchantillonnage aller-retour
private:
    struct Canal
    {
        detail::DemiBande os1, os2a, os2b, os1d, os2ad, os2bd;
        detail::Biquad hp, sh, pk, lp1, lp2, hissSh, hissPk, hissLp1, hissLp2;
        float buf[kLen] {}, dbuf[kLen] {}, pre[64] {};
        float dc = 0.f, hissHpS = 0.f, hissHpS2 = 0.f;
        detail::Alea alea;
    } canal[2];
    double fs = 48000.0;
    Reglages lis;
    bool premier = true, colorOn = false;
    float lissage = 0.01f, alcEnv = 0.f, alcGain = 1.f, alcAtt = 0.f, alcRel = 0.f, alcDet = 0.f, mono = 0.f;
    float satK = 0.f, satC = 0.f, colorFaite = -1.f, hissAmp = 0.f, hissNorm = 1.f, hissHp = 0.f, hissMono = 0.f, humVal = 0.f, humAmp = 0.f;
    double phRaie[detail::kNbRaies] {}, phHum = 0.0;
    float wowA1 = 0.f, wowA2 = 0.f, flA1 = 0.f, flA2 = 0.f, wowC1 = 0.f, flC1 = 0.f, centre = 0.f, centreC = 0.f, delaiMod = 0.f, dcC = 0.f;
    float wowM = 0.f, flM = 0.f;
    int w = 0;

    // reglages recalcules tous les 32 echantillons
    void blocReglages (bool monoOn) noexcept
    {
        using namespace detail;
        mono = monoOn ? 1.f : 0.f;
        // DRIVE : 0 -> rien, 50 % -> passe propre du Philips, 100 % -> passe saturee
        const float d = lis.drive;
        if (d < 1e-3f) satK = 0.f;
        else
        {
            float ka, c;
            if (d <= 0.5f) { const float t = d / 0.5f; ka = kKaPropre * t; c = kCPropre * t; }
            else { const float t = (d - 0.5f) / 0.5f; ka = kKaPropre * std::pow (kKaSature / kKaPropre, t); c = kCPropre + (kCSature - kCPropre) * t; }
            satK = ka / kRefAmp; satC = c;
        }
        // COLOR : du plat a la courbe du Philips
        if (std::fabs (lis.color - colorFaite) > 0.002f) couleur (lis.color);
        // WOW / FLUTTER : 50 % = Philips ; au-dela on depasse la machine (jusqu'a 8x pour le WOW, 3x pour le FLUTTER)
        auto loi = [] (float k, float maxi) { return k <= 0.5f ? std::pow (k / 0.5f, 1.5f) : 1.f + (k - 0.5f) / 0.5f * (maxi - 1.f); };
        wowM = lis.wow < 1e-3f ? 0.f : loi (lis.wow, 8.f);
        flM = lis.flutter < 1e-3f ? 0.f : loi (lis.flutter, 3.f);
        // HISS : 100 % = souffle du Philips ; loi au carre (les premiers crans restent discrets)
        hissAmp = lis.hiss < 1e-3f ? 0.f : hissNorm * lis.hiss * lis.hiss;
        // HUM : 10 % = Philips, 100 % = 10 fois plus fort
        humAmp = lis.hum < 1e-3f ? 0.f : 10.f * lis.hum;
    }

    void couleur (float a) noexcept
    {
        using namespace detail;
        colorFaite = a; colorOn = a > 1e-3f;
        if (! colorOn) return;
        const double hpF = 20.0 * std::pow (kHpF / 20.0, a), lpF = 0.49 * fs * std::pow (kLpF / (0.49 * fs), a);
        const double lpQ = 0.7071 + (kLpQ - 0.7071) * a;
        for (auto& C : canal)
        {
            C.hp.set (0, fs, hpF, kHpQ, 0); C.sh.set (3, fs, kShF, 0.7, kShG * a); C.pk.set (2, fs, kPkF, kPkQ, kPkG * a);
            C.lp1.set (1, fs, lpF, lpQ, 0); C.lp2.set (1, fs, lpF, lpQ, 0);
        }
    }

    void avance() noexcept
    {
        using namespace detail;
        // pleurage : raies mesurees + derive lente aleatoire (WOW), frottement rapide (FLUTTER), exprimes en retard de lecture
        float d = 0.f;
        if (wowM > 0.f || flM > 0.f)
        {
            for (int k = 0; k < kNbRaies; ++k)
            {
                phRaie[k] += kRaieF[k] / fs; if (phRaie[k] >= 1.0) phRaie[k] -= 1.0;
                const float m = kRaieWow[k] ? wowM : flM;
                if (m > 0.f) d += m * kRaieA[k] / (float) (2.0 * kPi * kRaieF[k]) * (float) std::sin (2.0 * kPi * phRaie[k]);
            }
            const float z = canal[0].alea.bi();
            wowA1 += wowC1 * (z - wowA1); wowA2 += wowC1 * (wowA1 - wowA2);
            flA1 += flC1 * (z - flA1); flA2 += flC1 * (flA1 - flA2);
            d += wowM * kWowAlea * wowA2 + flM * kFlAlea * flA2;
        }
        delaiMod = d;
        // retard moyen : juste ce qu'il faut pour que la lecture puisse pleurer dans les deux sens
        const float cible = (wowM > 0.f || flM > 0.f) ? 0.0015f + 0.0006f * wowM + 0.0002f * flM : 0.f;
        centre += centreC * (cible - centre);
        if (cible == 0.f && centre < 2e-5f) centre = 0.f;
        // HUM : 50 Hz et ses harmoniques
        if (humAmp > 0.f)
        {
            phHum += 50.0 / fs; if (phHum >= 1.0) phHum -= 1.0;
            float h = 0.f; for (int k = 0; k < 8; ++k) h += kHumA[k] * (float) std::sin (2.0 * kPi * (k + 1) * phHum + 0.7 * k);
            humVal = humAmp * h;
        }
        else humVal = 0.f;
    }
};
} // namespace foudre::tape

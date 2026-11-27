// Chord Seq : les notes jouees ensemble vont-elles dans le meme pas ? Les accords ont-ils le bon nom ?
#include "FoudreAccords.h"
#include <cstdio>
#include <cstring>

using namespace foudre::accords;

int main()
{
    int fail = 0;
    auto nom = [&] (std::initializer_list<int> n, const char* attendu)
    {
        int v[8]; int k = 0; for (int x : n) v[k++] = x;
        const auto s = nomAccord (v, k);
        const bool ok = s == attendu;
        std::printf ("%s accord -> %-12s (attendu %s)\n", ok ? "ok  " : "FAIL", s.c_str(), attendu);
        if (! ok) ++fail;
    };
    nom ({ 60, 64, 67 }, "C");
    nom ({ 53, 56, 60, 63 }, "Fm7");                        // F2 Ab2 C3 Eb3 (maquette)
    nom ({ 49, 53, 56, 60 }, "D\xe2\x99\xad" "maj7");      // Db2 F2 Ab2 C3
    nom ({ 51, 55, 58 }, "E\xe2\x99\xad");
    nom ({ 64, 67, 72 }, "C/E");                            // renversement
    nom ({ 57, 60, 64, 67 }, "Am7");                        // la basse decide (pas C6)
    nom ({ 60, 63, 66, 70 }, "Cm7\xe2\x99\xad" "5");
    nom ({ 60, 67 }, "C5");
    nom ({ 60, 61 }, "");                                   // inconnu : le pas affiche sa note la plus grave
    nom ({ 60 }, "");

    // Groupeur : 3 notes a 0, 15 et 40 ms = un pas ; une note 300 ms plus tard = nouveau pas
    {
        GroupeurAccord g;
        const bool a = g.rejoint (1.000, 60), b = g.rejoint (1.015, 64), c = g.rejoint (1.040, 67), d = g.rejoint (1.300, 62);
        const bool ok = ! a && b && c && ! d;
        std::printf ("%s groupeur : accord de 3 notes puis nouvelle note\n", ok ? "ok  " : "FAIL"); if (! ok) ++fail;
        g.ferme();
        const bool e = g.rejoint (1.310, 65);
        std::printf ("%s groupeur : apres REST, la note suivante ouvre un pas\n", ! e ? "ok  " : "FAIL"); if (e) ++fail;
        GroupeurAccord h; int pris = 0; for (int i = 0; i < 9; ++i) pris += h.rejoint (2.0 + 0.001 * i, 50 + i) ? 1 : 0;
        std::printf ("%s groupeur : 9 notes ensemble = un seul pas (%d rattachees)\n", pris == 8 ? "ok  " : "FAIL", pris); if (pris != 8) ++fail;
    }
    { int v[] { 67, 60, 64, 60, 72, 76, 79, 84 }; const int n = rangeNotes (v, 8);
      const bool ok = n == 6 && v[0] == 60 && v[1] == 64 && v[5] == 79;
      std::printf ("%s rangeNotes : tri, doublons retires, 6 notes max\n", ok ? "ok  " : "FAIL"); if (! ok) ++fail; }

    std::printf ("%d echec(s)\n", fail);
    return fail == 0 ? 0 : 1;
}

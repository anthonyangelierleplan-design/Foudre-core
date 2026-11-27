#pragma once
// Accords du sequenceur (Chord Seq), ecrits une seule fois pour Orage et Zephyr :
//  - GroupeurAccord : en enregistrement pas a pas, les notes jouees ensemble vont dans le meme pas ;
//  - nomAccord : le nom de l'accord affiche sur le pas (Fm7, Dbmaj7, C/E...).
// C++17 pur, sans JUCE et sans allocation : utilisable dans le thread audio.

#include <algorithm>
#include <string>

namespace foudre::accords
{
constexpr int notesMaxParPas = 6;          // notes d'un accord dans un pas (la premiere = la plus grave)
constexpr double fenetreAccordSec = 0.06;  // notes jouees a moins de 60 ms l'une de l'autre = le meme accord

/** Regroupe les notes d'enregistrement : la premiere note ouvre un pas, celles qui suivent dans la fenetre le completent. */
struct GroupeurAccord
{
    double fenetre = fenetreAccordSec;

    /** Renvoie true si la note rejoint l'accord du pas qui vient d'etre ecrit, false si elle ouvre un nouveau pas. */
    bool rejoint (double tempsSec, int note) noexcept
    {
        if (ouvert && tempsSec - debut <= fenetre)
        {
            for (int i = 0; i < n; ++i) if (notes[i] == note) return true;     // meme note rejouee : deja dans le pas
            if (n < notesMaxParPas) notes[n++] = note;                          // au-dela de 6 notes : ignoree (le pas est plein)
            return true;
        }
        ouvert = true; debut = tempsSec; n = 1; notes[0] = note;
        return false;
    }
    /** REST, BACK, TIE, fin d'enregistrement : la prochaine note ouvre forcement un nouveau pas. */
    void ferme() noexcept { ouvert = false; n = 0; }
    /** Notes du pas en cours (dans l'ordre de jeu) : a recopier dans le pas apres chaque appel a rejoint(). */
    int nombre() const noexcept { return n; }
    const int* lesNotes() const noexcept { return notes; }

private:
    bool ouvert = false; double debut = 0.0; int n = 0; int notes[notesMaxParPas] {};
};

/** Trie les notes, retire les doublons, garde au plus notesMaxParPas notes. Renvoie le nombre de notes. */
inline int rangeNotes (int* notes, int n) noexcept
{
    n = std::max (0, n);
    std::sort (notes, notes + n);
    int m = 0;
    for (int i = 0; i < n; ++i) if (m == 0 || notes[m - 1] != notes[i]) notes[m++] = notes[i];
    return std::min (m, notesMaxParPas);
}

/** Nom d'une note sans octave, en bemols (UTF-8) : 0 = C, 1 = Db... */
inline const char* nomNote (int note) noexcept
{
    static const char* const nm[12] { "C", "D\xe2\x99\xad", "D", "E\xe2\x99\xad", "E", "F", "G\xe2\x99\xad", "G", "A\xe2\x99\xad", "A", "B\xe2\x99\xad", "B" };
    return nm[((note % 12) + 12) % 12];
}

/** Nom de l'accord forme par ces notes (MIDI, ordre quelconque), ou "" si ce n'est pas un accord connu.
    La fondamentale est cherchee d'abord sur la basse ; sinon le nom est donne avec la basse : "C/E". */
inline std::string nomAccord (const int* notes, int n)
{
    if (n < 2) return {};
    int basse = notes[0];
    unsigned masque = 0;
    for (int i = 0; i < n; ++i) { basse = std::min (basse, notes[i]); masque |= 1u << (((notes[i] % 12) + 12) % 12); }
    struct Forme { unsigned m; const char* suffixe; };
    auto M = [] (std::initializer_list<int> iv) { unsigned m = 0; for (int i : iv) m |= 1u << i; return m; };
    static const Forme formes[] {
        { M ({ 0, 4, 7 }), "" },            { M ({ 0, 3, 7 }), "m" },          { M ({ 0, 3, 6 }), "dim" },        { M ({ 0, 4, 8 }), "aug" },
        { M ({ 0, 2, 7 }), "sus2" },        { M ({ 0, 5, 7 }), "sus4" },       { M ({ 0, 7 }), "5" },
        { M ({ 0, 4, 7, 10 }), "7" },       { M ({ 0, 4, 7, 11 }), "maj7" },   { M ({ 0, 3, 7, 10 }), "m7" },     { M ({ 0, 3, 7, 11 }), "m(maj7)" },
        { M ({ 0, 3, 6, 10 }), "m7\xe2\x99\xad" "5" },                          { M ({ 0, 3, 6, 9 }), "dim7" },    { M ({ 0, 5, 7, 10 }), "7sus4" },
        { M ({ 0, 4, 7, 9 }), "6" },        { M ({ 0, 3, 7, 9 }), "m6" },      { M ({ 0, 2, 4, 7 }), "add9" },    { M ({ 0, 2, 3, 7 }), "m(add9)" },
        { M ({ 0, 4, 10 }), "7" },          { M ({ 0, 4, 11 }), "maj7" },      { M ({ 0, 3, 10 }), "m7" },        // sans quinte
        { M ({ 0, 2, 4, 7, 10 }), "9" },    { M ({ 0, 2, 4, 7, 11 }), "maj9" },{ M ({ 0, 2, 3, 7, 10 }), "m9" },
        { M ({ 0, 2, 4, 10 }), "9" },       { M ({ 0, 2, 4, 11 }), "maj9" },   { M ({ 0, 2, 3, 10 }), "m9" },     // sans quinte
        { M ({ 0, 4, 7, 9, 2 }), "6/9" },   { M ({ 0, 4, 7, 10, 1 }), "7\xe2\x99\xad" "9" }, { M ({ 0, 4, 7, 10, 5 }), "11" }, { M ({ 0, 3, 7, 10, 5 }), "m11" },
    };
    auto tourne = [masque] (int racine) { const int r = ((racine % 12) + 12) % 12; return ((masque >> r) | (masque << (12 - r))) & 0xFFFu; };
    auto cherche = [&] (int racine) -> const char* { const unsigned t = tourne (racine); for (auto& f : formes) if (f.m == t) return f.suffixe; return nullptr; };
    if (const char* s = cherche (basse)) return std::string (nomNote (basse)) + s;
    for (int r = 0; r < 12; ++r)                                       // renversement : fondamentale ailleurs, basse en "/"
        if ((masque >> r) & 1u)
            if (const char* s = cherche (r)) return std::string (nomNote (r)) + s + "/" + nomNote (basse);
    return {};
}
} // namespace foudre::accords

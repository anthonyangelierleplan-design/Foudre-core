#pragma once
// Regles de la gamme Foudre Audio, ecrites une seule fois pour tous les plugins
// (Orage, Zephyr, Zenith...) et pour le banc d'essai qui les verifie.
// C++17 pur, sans JUCE : un plugin peut l'inclure tel quel.

namespace foudre::regles
{
// ---- Hydrasynth : les 8 macros du clavier envoient les CC 16 a 23 (macro 1 = CC 16 ... macro 8 = CC 23)
// Regle des instruments : les effets (Orage FX, Zephyr FX, Zenith...) ne recoivent pas le MIDI, c'est voulu.
constexpr int premierCcMacro = 16;
constexpr int nombreMacros   = 8;
constexpr int dernierCcMacro = premierCcMacro + nombreMacros - 1;

/** Numero de macro (1..8) pilotee par ce CC, ou 0 si ce CC n'est pas un CC de macro. */
constexpr int macroDuCc (int cc) noexcept
{
    return (cc >= premierCcMacro && cc <= dernierCcMacro) ? cc - premierCcMacro + 1 : 0;
}

/** CC MIDI de la macro 1..8, ou -1 hors limites. */
constexpr int ccDeLaMacro (int macro) noexcept
{
    return (macro >= 1 && macro <= nombreMacros) ? premierCcMacro + macro - 1 : -1;
}

// ---- Demarrage neutre : rien d'agressif pour les oreilles a l'ouverture
// Instrument : une note (do 3 = 60, velocite 100) jouee avec le reglage d'ouverture.
constexpr double instrumentCreteMaxDb   = -1.0;   // au-dessus : ca sature (KO)
constexpr double instrumentLufsMaxAlert = -10.0;  // au-dessus : trop fort pour un premier son (a regarder)
// Effet : modules a zero a l'ouverture, le son doit traverser sans changement.
constexpr double effetBruitMaxDb        = -70.0;  // sortie maximale quand l'entree est muette
constexpr double effetEcartGainMaxDb    = 1.0;    // ecart de volume tolere entre l'entree et la sortie
constexpr double effetDistorsionMaxDb   = -40.0;  // harmoniques ajoutees tolerees (sinus 1 kHz a -18 dBFS)

// ---- Cran magnetique au zero des potards : zone ou le potard "colle" au zero (part de la course totale).
constexpr double zoneCranZero = 0.02;

/** Applique le cran magnetique : une valeur normalisee 0..1 proche de 'zero' est ramenee sur 'zero'. */
constexpr double aimanteAuZero (double valeur, double zero) noexcept
{
    const double d = valeur - zero;
    return (d > -zoneCranZero && d < zoneCranZero) ? zero : valeur;
}
} // namespace foudre::regles

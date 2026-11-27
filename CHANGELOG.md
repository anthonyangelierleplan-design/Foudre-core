# Historique

## v1 — banc d'essai (27 nov. 2026)
- Création du tronc commun Foudre-Core.
- `core/FoudreRegles.h` : règles de la gamme (macros Hydrasynth CC 16-23, démarrage neutre, cran au zéro).
- **foudre-banc** : hôte VST3 hors ligne. Commandes `infos`, `params`, `rendu` (notes, fichier MIDI, signaux de test, presets, Program Change, paramètres, état), `mesure`, `compare`, `verifie`.
- Mesures vérifiées sur signaux connus (`tools/tests/mesures.cpp`, 17 contrôles) : LUFS à 0,05 dB près, clics (saut, coupure nette, attaque sèche) sans fausse alerte sur une dent de scie, spectre par octave, distorsion jusqu'à -88 dB.
- Premier passage sur la gamme : Orage, Zephyr, Zenith, Orage FX et Zephyr FX chargent, fenêtres redimensionnables, démarrage neutre ; Orage et Zephyr répondent aux CC 16-23 ; Orage FX et Zephyr FX démarrent avec Output à -6 dB.
- Les effets ne reçoivent pas le MIDI (voulu) : la règle des macros Hydrasynth est « sans objet » pour eux.

# Historique

## v3 — module TAPE (27 nov. 2026)
- `core/FoudreTape.h` : magnétophone à cassette calé sur un Philips D6410 mesuré (DRIVE, ALC, COLOR, WOW, FLUTTER, HISS, HUM, MIX, MONO), commun à Orage (v25) et Zephyr (v6). Tout à zéro : transparent (retard fixe de 36 échantillons).
- `tools/tests/tape.cpp` : transparence, harmoniques du Philips, souffle, compression, courbe, stabilité, mono.
- `tools/tape/tape-wav.cpp` : passe un WAV dans le module (`philips` = réglage du Philips) pour comparer à la vraie machine. Mesures et scripts : `C:\Dev\TAPE-mesure\analyse` (hors dépôt).

## v2 — accords et captures (27 nov. 2026)
- `core/FoudreAccords.h` : Chord Seq commun à Orage (v24) et Zephyr (v5) : `GroupeurAccord` (notes jouées à moins de 60 ms = même pas, 6 notes au plus), `nomAccord` (majeurs, mineurs, 7e, maj7, m7♭5, sus, 6, 9, 11…, renversements « C/E »), `rangeNotes`. Tests : `tools/tests/accords.cpp`.
- **foudre-banc capture** : photo de l'interface d'un plugin en PNG (`--onglet 1` = page ARP d'Orage / Zephyr, `--taille`). Des clics simulés ne marchent pas (JUCE lit la vraie position du curseur) : les plugins lisent `FOUDRE_ONGLET` à l'ouverture de leur fenêtre.

## v1 — banc d'essai (27 nov. 2026)
- Création du tronc commun Foudre-Core.
- `core/FoudreRegles.h` : règles de la gamme (macros Hydrasynth CC 16-23, démarrage neutre, cran au zéro).
- **foudre-banc** : hôte VST3 hors ligne. Commandes `infos`, `params`, `rendu` (notes, fichier MIDI, signaux de test, presets, Program Change, paramètres, état), `mesure`, `compare`, `verifie`.
- Mesures vérifiées sur signaux connus (`tools/tests/mesures.cpp`, 17 contrôles) : LUFS à 0,05 dB près, clics (saut, coupure nette, attaque sèche) sans fausse alerte sur une dent de scie, spectre par octave, distorsion jusqu'à -88 dB.
- Premier passage sur la gamme : Orage, Zephyr, Zenith, Orage FX et Zephyr FX chargent, fenêtres redimensionnables, démarrage neutre ; Orage et Zephyr répondent aux CC 16-23 ; Orage FX et Zephyr FX démarrent avec Output à -6 dB.
- Les effets ne reçoivent pas le MIDI (voulu) : la règle des macros Hydrasynth est « sans objet » pour eux.

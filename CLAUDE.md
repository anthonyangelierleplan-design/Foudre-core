# Foudre-Core — notes pour Claude

Tronc commun de la gamme Foudre Audio. Les règles générales (français, novice, règles des plugins) sont dans le CLAUDE.md personnel ; ici, ce qui est propre au dépôt.

## Carte
- `core/FoudreRegles.h` : règles de la gamme en code (CC 16-23 des macros, seuils de démarrage neutre, cran au zéro). C++17 pur, sans JUCE. **Changer un seuil ici le change pour tout le banc.**
- `banc/Source/Mesures.h` : mesures (niveaux, LUFS BS.1770, clics, spectre, distorsion), sans JUCE.
- `banc/Source/Hote.h` : hôte VST3 hors ligne (chargement, paramètres, rendu, WAV, signaux de test).
- `banc/Source/Main.cpp` : commandes `infos`, `params`, `rendu`, `mesure`, `compare`, `verifie`.
- `tools/tests/` : tests sans JUCE, lancés par `bash tools/tests/run.sh` (et par GitHub avant le build).

## Utiliser le banc pour un plugin
- Compiler le plugin (VST3 Release), puis `foudre-banc verifie <chemin>.vst3` et des `rendu` avant / après chaque modification de son, comparés avec `compare`.
- Orage / Zephyr ne publient pas leurs presets au DAW : `--programme N` (Program Change).
- Noms de paramètres : ceux affichés par `params` (les ID JUCE ne sont pas visibles depuis l'hôte VST3).
- Le banc mesure, il n'écoute pas : toujours rendre les WAV au propriétaire pour qu'il tranche.

## Pièges rencontrés
- `runDispatchLoopUntil` exige `JUCE_MODAL_LOOPS_PERMITTED=1` (déjà dans `banc/CMakeLists.txt`).
- Mesure de distorsion : fenêtre de Hann = plancher à -52 dB ; Blackman-Harris = plancher à -88 dB (utilisée).
- Détecteur de clics : comparer une tranche à ses voisines des **deux** côtés séparément, sinon chaque fin de note passe pour un clic.

## Circuit de travail
Comme les autres dépôts : une branche par version, push → GitHub compile, fusion sur `main` après le « go » du propriétaire.

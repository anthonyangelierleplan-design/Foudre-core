# Foudre-Core — tronc commun Foudre Audio

Le code partagé par toute la gamme (Orage, Zephyr, Zenith…) et les outils pour la tester. JUCE 8.0.4, C++17.

## Contenu

- **`core/`** — code commun, sans JUCE, que chaque plugin peut inclure :
  - `FoudreRegles.h` : les règles de la gamme écrites une seule fois (macros de l'Hydrasynth sur les CC 16 à 23, seuils du démarrage neutre, cran magnétique au zéro).
- **`banc/`** — **foudre-banc**, le banc d'essai : charge un `.vst3` sans Ableton, joue des notes ou un signal, enregistre un WAV, le mesure, le compare à une référence et vérifie les règles de la gamme. Mode d'emploi : [`banc/LISEZMOI.md`](banc/LISEZMOI.md).
- **`tools/tests/`** — tests automatiques (les mesures du banc vérifiées sur des signaux connus).

## Compiler (Windows)

```
cmake -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release --target FoudreBanc
```
Le banc est dans `build\banc\FoudreBanc_artefacts\Release\foudre-banc.exe`. Sur GitHub, chaque push le compile (onglet Actions → artefact `foudre-banc-Windows`).

## Utiliser le code commun dans un plugin

Dans le `CMakeLists.txt` du plugin, après JUCE :
```cmake
FetchContent_Declare(FoudreCore GIT_REPOSITORY https://github.com/anthonyangelierleplan-design/Foudre-Core.git GIT_TAG main)
FetchContent_MakeAvailable(FoudreCore)
target_link_libraries(MonPlugin PRIVATE foudre::core)
```
puis `#include "FoudreRegles.h"`. Le banc n'est pas compilé quand Foudre-Core est inclus dans un plugin.

## Prochaines étapes (pas commencées)

Orage et Zephyr ont les mêmes fichiers mais leur contenu a divergé partout. À rapatrier ici, un module à la fois, chacun testé à l'oreille dans les deux plugins :
1. l'arpégiateur / séquenceur (`Arp.h`, le plus proche entre les deux) ;
2. le module CASSETTE / VHS ;
3. les thèmes et l'ergonomie (potards, cran au zéro, fenêtre) ;
4. les modules qui font la couleur de chaque synthé (filtres, VCO) **restent** dans leur dépôt.

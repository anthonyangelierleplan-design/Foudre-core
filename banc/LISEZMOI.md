# foudre-banc — le banc d'essai Foudre Audio

Un mini-Ableton sans écran : il charge un `.vst3`, lui joue des notes (ou un son, pour un effet), enregistre ce qui sort dans un WAV, puis le **mesure**. Le rendu est plus rapide que le temps réel et donne toujours le même résultat (pratique pour comparer avant / après une modification).

Il ne remplace pas vos oreilles : il dit *combien* (volume, brillance, clics), vous dites *si c'est beau*.

Programme compilé : `build\banc\FoudreBanc_artefacts\Release\foudre-banc.exe`

## Les commandes

| Commande | Ce qu'elle fait |
|---|---|
| `foudre-banc infos Plugin.vst3` | type (instrument / effet), canaux, latence, presets |
| `foudre-banc params Plugin.vst3 [filtre]` | liste des réglages avec leur valeur (`params Orage.vst3 Amp`) |
| `foudre-banc capture Plugin.vst3 --sortie f.png` | photo de l'interface (`--onglet 1` : page ARP d'Orage / Zephyr ; `--taille 1400x900` ; accepte `--etat`, `--param`, `--programme`) |
| `foudre-banc rendu Plugin.vst3 --sortie son.wav` | joue, enregistre, puis affiche les mesures |
| `foudre-banc mesure son.wav [autre.wav ...]` | mesure des fichiers existants (un enregistrement de l'Hydrasynth, par exemple) |
| `foudre-banc compare reference.wav rendu.wav` | écarts de volume et de spectre, résumé en clair |
| `foudre-banc verifie Plugin.vst3` | règles de la gamme (voir plus bas) ; code de sortie 1 si une règle est KO |

## Options de `rendu`

- Notes : `--notes 48,55,60` (accord), `--velocite 100`, `--duree 2` (note tenue, en secondes), `--queue 1.5` (temps d'enregistrement après le relâché).
- Morceau : `--midi riff.mid` (remplace `--notes`), `--tempo 120` (tempo donné au plugin : arpégiateur, LFO synchronisés).
- Réglages : `--preset 3` ou `--preset "Nom"` (presets publiés par le plugin), `--programme 5` (Program Change MIDI, comme l'Hydrasynth : c'est ce qu'il faut pour Orage et Zephyr), `--param "Amp R=0.2"` (valeur 0..1, option répétable), `--param "Output=texte:-3 dB"`, `--etat reglage.bin` / `--sauve-etat reglage.bin`.
- Effets : `--entree son.wav`, ou un signal fabriqué : `--signal sinus|bruit|balayage|impulsion|silence`, `--niveau -18` (dBFS), `--freq 440`, `--garde-entree` (enregistre aussi le signal d'entrée).
- Technique : `--sr 48000`, `--bloc 512`.

Le WAV est écrit en 32 bits flottants : rien n'est écrêté dans le fichier, même au-dessus de 0 dBFS.

## Ce que disent les mesures

- **Crête / RMS** (dBFS) : le point le plus fort, et le volume moyen.
- **Loudness** (LUFS, norme BS.1770 comme dans les DAW) : le volume ressenti. *Max momentané* = le passage le plus fort sur 0,4 s.
- **Écrêtage** : échantillons à 0 dBFS ou plus.
- **Décalage DC** : un signal décentré (gaspille de la marge, fait « pop » à l'arrêt).
- **Fin (100 ms)** : ce qui reste à la fin du fichier (souffle, queue de réverbération qui ne s'éteint pas).
- **Spectre** : la part de chaque octave. **Brillance (centroïde)** : le « centre de gravité » du spectre ; plus il est haut, plus le son est brillant. *Aigus* > 5 kHz et *graves* < 120 Hz.
- **Clics suspects** :
  - *saut isolé* : une cassure du signal plus forte que tout ce qui l'entoure (avant et après) ;
  - *coupure nette* : le son perd plus de 30 dB en 5 ms (relâché trop court, voix volée) ;
  - *attaque sèche* : il en gagne autant (normal si l'attaque est réglée à 0).
  Une dent de scie a une cassure à chaque période : ce n'est pas un clic, le banc le sait.

## `verifie` : les règles de la gamme

| Règle | Comment le banc la vérifie |
|---|---|
| Plugin VST3 | il se charge sans erreur |
| Fenêtre redimensionnable | ouvre l'interface et demande si elle est redimensionnable |
| Démarrage neutre (instrument) | joue un do 3 (vélocité 100) avec le réglage d'ouverture : KO si crête > -1 dBFS, à regarder si > -10 LUFS |
| Démarrage neutre (effet) | entrée muette → sortie muette ; sinus 1 kHz à -18 dBFS → même volume (±1 dB, plus bas accepté) et pas d'harmoniques ajoutées (< -40 dB) |
| Macros Hydrasynth | balaye chaque CC 16 à 23 et regarde quel réglage bouge (sans objet pour les effets : ils ne reçoivent pas le MIDI, c'est voulu) |
| Cran magnétique au zéro | ne peut pas se vérifier sans souris : à faire à la main |

Les seuils sont dans `core/FoudreRegles.h` : un seul endroit pour toute la gamme.

## La boucle de travail

1. Rendu de référence : `foudre-banc rendu Orage.vst3 --programme 5 --notes 48 --sortie avant.wav`
2. Modification du code, recompilation du plugin.
3. Même rendu → `apres.wav`, puis `foudre-banc compare avant.wav apres.wav`.
4. On écoute les deux fichiers, vous tranchez.

Pour comparer à l'Hydrasynth : enregistrez-le dans Ableton (même note, même durée, patch proche), exportez en WAV, puis `foudre-banc compare hydrasynth.wav rendu.wav`.

## Limites

- Le banc ne touche pas à l'interface (pas de souris), sauf pour savoir si la fenêtre est redimensionnable.
- Les presets d'Orage et de Zephyr ne sont pas publiés au DAW (0 preset) : utiliser `--programme`.
- Pas de mesure du repliement de spectre ici : voir `tools/tests/mesure_repliement.py` dans Orage-Synth / Zephyr-VST.

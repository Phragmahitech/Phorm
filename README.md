# Phorm by Phragma

Instrument VST3 qui génère des wavetables au hasard pour Serum : on tire une table, on l'écoute en jouant des notes, on l'exporte.

- **GENERATE** : nouvelle table du **STYLE** choisi, graine tirée au hasard sur 32 bits (4,3 milliards de valeurs), jamais une graine déjà tirée dans le projet. ‹ › : tables précédentes et suivantes (historique de 100 tables, gardé dans le projet).
- **STYLE** (pour la prochaine table) :
  - RANDOM : un des styles ci-dessous, tiré avec la graine.
  - ADDITIVE : spectres d'harmoniques tirés au hasard qui se transforment.
  - FORMANT : formants qui se déplacent (voix, growl).
  - FM : FM à 3 opérateurs, rapports entiers.
  - FOLD : wavefolding progressif.
  - SYNC : hard sync dont le rapport monte, avec ou sans fenêtre.
  - DIGITAL : marches aléatoires et réduction de bits.
  - CHAOS : deux styles combinés (fondu, ring mod ou distorsion de phase).
- **BRIGHT**, **MORPH**, **SYMMETRY**, **FRAMES** (pour la table en cours, refaite avec la même graine) : nombre d'harmoniques (16 à 1023), passage entre les images clés (SMOOTH, LINEAR, STEP), symétrie de chaque frame, 16 à 256 frames.
  - SYMMETRY MIRROR : la seconde moitié de la frame est le reflet de la première (forme symétrique autour du centre).
  - SYMMETRY FLIP : la seconde moitié est la première inversée (symétrie de demi-onde : harmoniques impaires seulement, son creux, façon carré).
- **PLAY** : n'importe quelle note MIDI joue la table (8 voix). POSITION, SCAN (balayage de la position jusqu'au bout de la table), RATE, ATTACK, RELEASE, LEVEL. Une version de la table par octave : pas de repliement dans l'aigu.
- **EXPORT** : écrit la table (WAV 32 bits float, frames de 2048 échantillons, chunk `clm` lu par Serum) dans `Documents/Xfer/<Serum…>/Tables/Phorm/` pour chaque Serum trouvé (Serum et Serum 2), sinon dans `Documents/Phorm/Tables/`. **DRAG** : exporte puis glisse le fichier, à déposer sur un oscillateur de Serum. **FOLDER** : ouvre le dossier.
- Nom du fichier : `style-graine.wav`, plus les réglages qui s'écartent du défaut (`fm-3829104756-256f-b40-step-mir.wav`). Même nom = même table.

JUCE 8, C++17, CMake. Le moteur (`Source/WavetableGenerator.h`, `Source/PhormSynth.h`) n'utilise pas JUCE. Les polices (Michroma, IBM Plex Mono, licence OFL : voir `Assets/`) sont téléchargées à la configuration depuis `google/fonts` à un commit fixe et vérifiées par SHA-256.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Phorm_VST3
```

Tests : `-DPHORM_BUILD_TESTS=ON`, puis `ctest --test-dir build -C Release`.

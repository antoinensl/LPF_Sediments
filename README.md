# Sediments (prototype v0.8)

[![Build](https://github.com/antoinensl/LPF_Sediments/actions/workflows/build.yml/badge.svg)](https://github.com/antoinensl/LPF_Sediments/actions/workflows/build.yml)

Instrument VST3 pour Reaper, entre deux approches : le **terrain latent** (inspiré de [nn.terrain](https://jasper-zheng.github.io/nn_terrain/)) et la **synthèse concaténative** (dans l'esprit de CataRT).

Tes enregistrements se déposent sur une carte 2D, les sons semblables les uns près des autres. Tu t'y déplaces à la souris, avec des gestes enregistrés ou au clavier MIDI, et le son est soit réinventé par un modèle neuronal RAVE, soit recomposé à partir de grains de tes sons d'origine, soit un mélange des deux. Tout se fait dans Reaper, sans Max.

Un projet [Life Pass Filter](https://lifepassfilter.com). Polices : Atkinson Hyperlegible et OpenDyslexic (SIL Open Font License).

## Principe

1. **Le codec (RAVE)** encode le son en vecteurs latents (une trame toutes les ~2048 échantillons) et les redécode. Il est pré-entraîné : on ne l'entraîne pas en direct.
2. **Le corpus** : chaque fichier glissé sur la carte est encodé. Chaque trame latente devient un point, coloré selon son fichier. Les trames silencieuses (< -60 dBFS) sont ignorées.
3. **La disposition** : une analyse en composantes principales (PCA) sur les latents place les points. Deux sons proches sur la carte sont proches pour le modèle.
4. **Le terrain** : à chaque trame, la position du curseur donne un vecteur latent, que RAVE décode. Ce vecteur vient de deux sources, que le bouton Terrain mélange :
   - les **voisins** : un mélange des points les plus proches (proche de la synthèse concaténative, mais repeint par le modèle) ;
   - le **terrain appris** : un petit réseau de neurones, entraîné en quelques secondes en tâche de fond à chaque changement de fichiers. Il apprend une surface continue qui passe près de tes points et invente des sons nouveaux entre et autour d'eux. C'est l'approche de nn.terrain. Sur la carte, il apparaît comme un relief coloré : deux zones de même couleur sonnent de façon proche.

| Réglage | Effet |
|---|---|
| **X / Y** | Position sur la carte. Ce sont des paramètres VST : automatisables, modulables (LFO, enveloppes, MIDI learn dans Reaper). |
| **Blur** | Largeur du mélange des voisins (le cercle autour du curseur). Bas : colle aux sons les plus proches. Haut : mélanges doux, mais qui tendent vers un son « moyen ». N'agit que sur la partie « voisins ». |
| **Terrain** | De « voisins » à « appris ». Le terrain appris est prêt quand le bandeau affiche « Learned terrain ready » ; avant, le plugin utilise les voisins. |
| **Glide** | Lissage temporel du trajet latent, de quasi instantané à plusieurs secondes. |
| **Gain** | Niveau du son généré. |
| **Dry in** | Laisse passer l'entrée de la piste (prévu pour la future entrée guitare). 0 par défaut. |

Tu peux glisser sur la carte des fichiers wav, aif, flac, mp3 ou ogg, un dossier entier, ou un modèle `.ts`.

## Deux façons de faire sonner la carte

- **Avec un modèle RAVE** : le modèle rejoue tout avec son propre timbre et invente des sons entre tes enregistrements (synthèse neuronale).
- **Avec tes sons d'origine** : tes enregistrements eux-mêmes sont rejoués en petits grains, choisis autour du curseur (synthèse concaténative, dans l'esprit de CataRT).

Les deux se combinent : avec un modèle chargé, le réglage **Source** fait le fondu de « Model » à « Your originals ». Sans modèle (bouton « Play without a model » à l'accueil, ou menu de l'étape Model), seuls les originaux jouent.

### Sans modèle

Les fichiers sont analysés en petites unités d'environ 23 ms, décrites par quatre propriétés : **hauteur** (avec un indice de netteté), **brillance** (centroïde spectral), **bruit** (platitude spectrale) et **volume**. Rien à télécharger, l'analyse est quasi instantanée, et la latence descend à environ 30 ms. Le terrain appris et le réglage Terrain ne s'appliquent qu'avec un modèle.

### Les réglages en mode originaux

| Réglage | Effet |
|---|---|
| **Focus** | Jusqu'où, autour du curseur, les grains peuvent être choisis. Bas : toujours le même moment (son « gelé »). Haut : un mélange de moments voisins. |
| **Flow** | Probabilité que le grain suivant continue là où le précédent s'arrêtait dans son fichier : le son garde sa propre évolution au lieu de sauter d'un moment à l'autre. |
| **Flow speed** | Vitesse de cette lecture continue (1× = vitesse d'origine). |
| **Grain size** | Longueur des grains, de 10 ms (granuleux) à 1 s (de vrais extraits). |
| **Glide** | Lissage des déplacements du curseur. |

### Disposition de la carte

Le bouton **Map** (en haut à droite de la carte) choisit ce que représentent les axes : **Similarity** (rangement par ressemblance : selon le modèle s'il y en a un, sinon selon l'ensemble des descripteurs), ou une propriété précise de gauche à droite et de bas en haut : **Pitch**, **Brightness**, **Noisiness**, **Loudness**. Par exemple hauteur × brillance, comme dans CataRT. Les axes choisis sont sauvegardés avec le projet et fonctionnent aussi avec un modèle.

## L'interface

- **En haut** : les trois étapes (Modèle, Sons, Terrain) avec leur état. Cliquer sur « Model » ou « Sounds » ouvre le sélecteur de fichiers. À droite : le volume, l'aide et les réglages d'affichage.
- **Premier lancement** : un écran d'accueil guide pas à pas (choisir un modèle, ajouter des sons, jouer).
- **Trois onglets** : Explore, Gesture et Keys (raccourcis 1, 2, 3).
- **La carte** : une marque par moment de son, colorée et avec une forme propre à chaque fichier. Les lignes de niveau montrent où les sons se regroupent, et le nom de chaque son est affiché sur sa région. Survoler une marque indique le fichier et l'instant ; Maj + clic fait entendre ce moment d'origine.
- **« You're hearing »** (onglet Explore) : indique en clair quels sons composent ce que tu entends, avec leurs proportions.
- **Sons** : la liste des fichiers, avec écoute du fichier d'origine (▶), retrait (×) et changement de couleur (clic sur la pastille).
- **Shape the sound** : Focus, Terrain, Glide et Flow, puis « More » pour la vitesse du flow et l'entrée de la piste.

### Affichage et accessibilité (bouton Settings)

Ces réglages sont enregistrés pour tous les projets, sur ton ordinateur.

| Réglage | Choix |
|---|---|
| **Theme** | Dusk (sombre), Daylight (clair), High contrast, Night (faible luminosité, chaud, pour le studio de nuit). |
| **Sound colours** | Standard, Red-green safe (palette Okabe–Ito, pour protanopie et deutéranopie), Blue-yellow safe (pour la tritanopie). Les teintes sont ajustées automatiquement pour toujours ressortir sur la carte. |
| **Couleur d'un son** | Clic sur la pastille d'un son dans la liste : palette ou couleur libre. Enregistrée avec le projet. |
| **Accent colour** | La couleur des boutons et repères. Le contraste avec les panneaux est affiché ; s'il est trop faible pour du texte, « Make it readable » propose la nuance lisible la plus proche. |
| **Font** | Atkinson Hyperlegible (par défaut, conçue pour bien distinguer les lettres semblables), la police du système, ou OpenDyslexic. |
| **Text size** | 100, 125 ou 150 %. Toute la fenêtre s'agrandit avec le texte, rien n'est coupé. |
| **Letter spacing** | Normal, Wide, Wider. |
| **Shapes as well as colours** | Chaque son a sa forme (rond, carré, triangle…), pour ne jamais dépendre de la couleur seule. |
| **Calm motion** | Pas d'animation : le curseur saute directement à sa position. |
| **Announce what you hear** | Les lecteurs d'écran annoncent les sons présents quand on se déplace. |

Tout se fait aussi au clavier : Tab pour passer d'un contrôle à l'autre, flèches pour déplacer le curseur (Maj pour un pas fin), Espace pour entendre le moment d'origine le plus proche, R pour enregistrer un geste, 1/2/3 pour les onglets, ? pour l'aide, Échap pour fermer un panneau. Dans Reaper, si un raccourci est capté par Reaper au lieu du plugin, active « Send all keyboard input to plug-in » dans le menu « + » de la fenêtre FX.

## Jouer : flow, gestes et notes MIDI

### Flow (le temps propre des sons)

Sans Flow, chaque point est un instantané figé. Avec Flow, le plugin part du point le plus proche du curseur et rejoue la façon dont le son d'origine évoluait dans le temps à partir de là : battements, respiration, grain. Si tu éloignes le curseur, il saute vers les sons de la nouvelle position. À la fin d'un fichier, il repart d'un point voisin pris au hasard.

| Réglage | Effet |
|---|---|
| **Flow** | 0 : instantané figé. 1 : suit entièrement le mouvement des sons d'origine. Entre les deux : un mélange. |
| **Vitesse** | Vitesse de lecture de ce mouvement : 1 x = vitesse d'origine, moins = étiré, plus = accéléré. |

### Gestes (trajectoires)

1. Clique sur **● Rec geste**. La carte s'entoure de rouge.
2. Dessine un geste sur la carte avec la souris : sa forme et son rythme sont enregistrés.
3. Au relâchement, le geste tourne en boucle (**▶ Lecture**).

| Réglage | Effet |
|---|---|
| **Libre / 1 temps … 4 mesures** | Libre : durée enregistrée × Vitesse. Sinon : la boucle est étirée pour durer exactement ce nombre de temps, calée sur le tempo et la position de lecture de Reaper. |
| **Avant / Arrière / Aller-retour** | Sens de lecture. |
| **Fixe / Suit le curseur** | Fixe : le geste est rejoué là où tu l'as dessiné. Suit le curseur : il garde sa forme mais part de la position X/Y, ou de la zone de la note MIDI jouée. |
| **Vitesse** (section Geste) | Vitesse en mode Libre (grisé quand le geste est calé sur le tempo). |
| **Effacer** | Supprime le geste. |

Le geste est sauvegardé avec le projet Reaper.

### Notes MIDI

Active **Notes MIDI** : le plugin écoute alors le MIDI de la piste.

- La carte est découpée automatiquement en **12 zones**, une par nom de note : **C en haut**, puis dans le sens des aiguilles d'une montre. L'octave ne change rien : un C2 et un C5 jouent la même zone. Les zones s'affichent sur la carte.
- Chaque note **déclenche** le son (enveloppe Attack / Release) et la **vélocité** règle son niveau.
- Jeu **monophonique** : la dernière note jouée l'emporte. En legato, le son glisse vers la nouvelle zone sans se relancer. Glide règle la vitesse de ce glissement : c'est un portamento de timbre.
- **Spray** : à chaque note, un point est tiré au hasard autour du centre de la zone. Deux frappes de la même note ne sonnent donc jamais exactement pareil (idée reprise de Coalescence).
- Avec un geste en mode **Suit le curseur**, chaque note rejoue le geste depuis sa zone.

Pour jouer : sur la piste du plugin, active l'armement d'enregistrement et choisis ton clavier MIDI comme entrée, ou écris des notes dans un item MIDI sur la piste. Le clavier à l'écran de l'onglet Keys joue aussi les zones (et active les notes MIDI automatiquement).

Hors mode MIDI, le son est continu (drone) et suit X/Y comme avant.

## Télécharger une version prête (Windows et Mac)

Le plugin est compilé automatiquement par GitHub à chaque mise à jour : plus besoin de Visual Studio ni de libtorch.

1. Sur la page du dépôt, colonne de droite, ouvre **Releases** et prends la dernière version.
2. Télécharge `Sediments-Windows.zip` ou `Sediments-macOS.zip` (Mac Apple Silicon : M1, M2, M3…, macOS 14 Sonoma ou plus récent).
3. Dézippe, puis suis le fichier `LISEZMOI.txt` : sous Windows, un double-clic sur `INSTALLER.bat` ; sur Mac, clic droit sur `Installer.command`, puis « Ouvrir ».

Les toutes dernières versions de travail sont aussi dans l'onglet **Actions** (choisis la dernière exécution réussie, en bas de la page : « Artifacts »). Elles y restent 14 jours, et il faut être connecté à GitHub pour les télécharger.

Sur Mac, le plugin n'est pas signé par Apple (la signature officielle coûte 99 € par an). L'installateur retire la mise en quarantaine pour que macOS accepte de le charger sur ton ordinateur.

## Compilation sous Windows (pas à pas)

Seulement si tu veux compiler toi-même.

Tu n'as à installer que deux choses une fois, puis à double-cliquer sur un script.

### 1. Installer Visual Studio (une seule fois, environ 30 min)

1. Télécharge **Visual Studio Community** (gratuit) sur <https://visualstudio.microsoft.com/fr/>.
2. Pendant l'installation, coche la case **« Développement Desktop en C++ »**, puis clique sur Installer. Le téléchargement fait plusieurs Go.
3. Pas besoin d'ouvrir Visual Studio ensuite : le script s'en sert tout seul.

Si le script te dit plus tard qu'il manque Git, installe-le depuis <https://git-scm.com/download/win> en gardant toutes les options par défaut.

### 2. Télécharger libtorch (une seule fois)

1. Va sur <https://pytorch.org/get-started/locally/>.
2. Dans le tableau, choisis : **Stable**, **Windows**, **LibTorch**, **C++/Java**, **CPU**.
3. Télécharge le lien de la version **Release** (pas celui marqué Debug).
4. Dézippe-le directement sur `C:\`. Tu dois obtenir `C:\libtorch\lib`, `C:\libtorch\include`, etc.

### 3. Compiler le plugin

1. Récupère le projet : sur la page GitHub du dépôt, bouton vert **Code**, puis **Download ZIP**. Dézippe-le dans un dossier au chemin court, par exemple `C:\Sediments`.
2. Double-clique sur **`COMPILER.bat`**.
3. Attends : 5 à 15 minutes la première fois, moins ensuite.
4. Vers la fin, le script te demande s'il peut ajouter libtorch à ton « Path ». Réponds **O** : c'est ce qui permet à Reaper de trouver les fichiers de libtorch.

À la fin, un dossier **`Plugin_pour_Reaper`** contient `Sediments.vst3`.

Si Windows affiche « Windows a protégé votre ordinateur », clique sur « Informations complémentaires » puis « Exécuter quand même ».

Si une étape échoue, le script l'explique en rouge. Envoie le message à Claude.

### 4. Ajouter le plugin dans Reaper

1. **Ferme et rouvre Reaper**, pour qu'il prenne en compte le changement de Path.
2. *Options → Preferences → Plug-ins → VST*.
3. À côté de « VST plug-in paths », clique sur **Add…** et choisis le dossier `Plugin_pour_Reaper`.
4. Clique sur **Re-scan**, puis sur OK.
5. Sur une piste : bouton **FX**, puis cherche « Sediments ».

Si Reaper indique que le plugin ne se charge pas, c'est presque toujours le Path. Vérifie que Reaper a bien été entièrement fermé puis rouvert. Au besoin, redémarre l'ordinateur.

Pour tester sans Reaper, lance `build\Sediments_artefacts\Release\Standalone\Sediments.exe`.

### En ligne de commande (pour plus tard)

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/libtorch
cmake --build build --config Release
```

## Modèles RAVE

Il faut un modèle RAVE exporté pour nn~ (fichier `.ts`). Des modèles pré-entraînés sont disponibles sur l'IRCAM Forum et via le dépôt GitHub de RAVE.

- Privilégie les exports **streaming**. Un modèle non streaming fonctionne aussi, mais avec des artefacts aux jointures entre trames.
- Pour entraîner un modèle sur tes propres sons, utilise le script d'entraînement officiel de RAVE, une fois et hors ligne, sur une machine avec un GPU, puis exporte-le avec `--streaming`.
- Le plugin lit la fréquence d'échantillonnage du modèle quand elle est exposée. Sinon il suppose 44,1 kHz, et l'indique dans le bandeau.

## Limites de cette version

- **Latence** : environ 2 trames, soit ~90 à 120 ms avec un ratio de 2048, y compris entre une note MIDI et le son. C'est suffisant pour des textures, des gestes et des phrases lentes, mais pas pour du jeu percussif serré. Les gestes calés sur le tempo, eux, sont bien synchronisés : le plugin compense son avance.
- **Monophonique** : une seule voix à la fois (une seule position sur la carte).
- **Mémoire** : pour pouvoir rejouer les originaux, les fichiers sont gardés en mémoire (environ 23 Mo par minute de stéréo à 48 kHz, 20 minutes maximum par fichier).
- **Hauteur** : les moments sans hauteur nette (bruits, textures) sont placés au milieu de l'axe Pitch.
- **Disposition** : elle est recalculée à chaque ajout ou retrait de fichiers, donc les points se réorganisent, et le terrain appris est réentraîné (quelques secondes).
- **Terrain appris** : l'entraînement partage le processeur avec la synthèse. Il est volontairement ralenti pour ne pas provoquer de coupures, mais sur une machine modeste, attends la fin de l'entraînement avant d'enregistrer.
- **Calcul** : tout tourne sur le CPU, avec le modèle en double (une instance pour l'encodage, une pour la synthèse).
- **Sessions** : l'état est sauvegardé dans le projet Reaper (chemin du modèle, liste des fichiers, réglages). À la réouverture, les fichiers sont ré-encodés, ce qui prend quelques secondes.

## Prochaines étapes prévues

1. **Entrée live** : enregistrer des prises de guitare directement dans le corpus, puis faire piloter la position par la guitare elle-même (son encodage projeté sur la carte).
2. **Plusieurs voix** : jouer des accords, avec un décodeur RAVE par voix.
4. **Édition de la carte** : déplacer des groupes de points à la main pour composer sa propre géographie sonore.

## Structure du code

| Fichier | Rôle |
|---|---|
| `Source/RaveModel.*` | Chargement TorchScript, `encode` / `decode` (seul fichier qui inclut libtorch). |
| `Source/Corpus.*` | Corpus de trames latentes, disposition PCA, mélange des voisins (interpolation gaussienne). |
| `Source/Analysis.*` | Descripteurs audio : hauteur (méthode de McLeod), brillance, bruit, volume. |
| `Source/GrainPlayer.*` | Lecture concaténative des originaux : grains fenêtrés, choix autour du curseur, Flow. |
| `Source/LearnedTerrain.*` | Terrain appris : réseau à coordonnées (Fourier features + MLP) entraîné sur le CPU, et image du relief. |
| `Source/LatentEngine.*` | Thread d'import (modèle, fichiers), thread de synthèse (terrain → RAVE → rééchantillonnage → FIFO), lecture côté audio sans verrou. |
| `Source/PluginProcessor.*` | Paramètres, routage audio, sauvegarde de l'état. |
| `Source/PluginEditor.*` | Assemblage de l'interface, mise à l'échelle (taille du texte), raccourcis clavier, glisser-déposer. |
| `Source/ui/UiCore.*` | Thèmes, palettes de couleurs, contraste, polices intégrées, réglages d'affichage, icônes, style des boutons et curseurs. |
| `Source/ui/Widgets.*` | Curseurs étiquetés, choix segmentés, cases à cocher avec description. |
| `Source/ui/MapView.*` | La carte : marques, lignes de niveau, zones MIDI, geste, curseur accessible au clavier. |
| `Source/ui/Panels.*` | Barre du haut, onglets, « You're hearing », panneaux Geste et Keys, liste des sons, réglages du son, accueil, réglages d'affichage, aide. |
| `Resources/fonts/` | Atkinson Hyperlegible et OpenDyslexic (licence SIL Open Font License, fichiers OFL inclus). |
| `tools/` | Outils de test : faux modèle RAVE, rendu en ligne de commande, capture de l'interface. |

Pour compiler les outils de test : `-DLT_BUILD_CLI=ON`. Exemple : `sediments_cli modele.ts sortie.wav a.wav b.wav` rend 10 secondes de trajet circulaire sur la carte.

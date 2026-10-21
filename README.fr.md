[日本語](README.md) · [English](README.en.md)

# stackchan-idf EN-FR

Fork de [ciniml/stackchan-idf](https://github.com/ciniml/stackchan-idf) avec le robot en
**français** ou en **anglais** (le japonais reste disponible), pour un usage avec un serveur
vocal local ([StackChan-Local-server](https://github.com/Emilien-Etadam/StackChan-Local-server)).

## Ce qui change par rapport à l'original

- **Langue** : français, anglais ou japonais, au choix. Elle s'applique à l'écran du robot,
  à l'invite et aux descriptions d'outils envoyées au LLM, et à la voix propre du robot.
- **Voix propre du robot** : en français et en anglais, les phrases que le robot dit de
  lui-même (minuteur, rappels, babillage, bouton d'essai) sont demandées au service
  voix-fr par le Wi-Fi (`POST <url>/tts/stream`). Sans URL ou si le serveur ne répond
  pas, le robot reste silencieux et affiche seulement la bulle. Le synthétiseur japonais
  interne n'est utilisé qu'en japonais.
- **Fuseau horaire** réglable (Europe/Paris par défaut ; l'original était figé sur
  l'heure du Japon).
- **Pages de réglages (Bluetooth et Wi-Fi) et flasheur web** en français ou en anglais :
  menu de langue en haut à droite, choix mémorisé par le navigateur.
- **Mises à jour OTA** depuis les versions de ce fork (et non de l'original).
- **Caméra réactivée** (320×240) pour que le serveur tourne la tête du robot vers la personne
  qui parle. Pour lui faire de la place en mémoire, le **Bluetooth ne démarre plus une fois le
  Wi-Fi configuré** (sauf si le Wi-Fi ne répond pas en 30 s) : les réglages se font alors par
  la page Wi-Fi du robot. Option « Garder le Bluetooth toujours actif » dans « Langue et voix ».

## Installation

- **Premier flashage** : <https://emilien-etadam.github.io/stackchan-idf-EN-FR/>
  (Chrome ou Edge, câble USB).
- **Réglages Bluetooth** : <https://emilien-etadam.github.io/stackchan-idf-EN-FR/settings.html>
- **Réglages Wi-Fi** : `http://stackchan-XXXXXX.local/` une fois le robot sur le Wi-Fi.

## Réglage « Langue et voix »

Dans l'onglet *Connecter* des pages de réglages :

| Champ | Exemple | Effet |
|---|---|---|
| Langue | Français | écran, LLM, voix propre |
| Fuseau horaire (POSIX TZ) | `CET-1CEST,M3.5.0,M10.5.0/3` | heure affichée et donnée au LLM |
| URL du serveur vocal | `http://192.168.1.10:8200` | voix propre du robot hors japonais |

Pris en compte après *Enregistrer & redémarrer*.

## Écarts connus

- Les phrases personnalisées en japonais déjà enregistrées (babillage, minuteur) sont
  ignorées hors japonais ; les phrases par défaut sont alors en français ou en anglais.
- En anglais, la voix vient du même service voix-fr, réglé pour le français.
- Les mises à jour de l'original se récupèrent avec le bouton « Sync fork » de GitHub ;
  les nouveaux textes japonais des pages de réglages s'ajoutent à
  `tools/i18n/dict.json` (`python3 tools/i18n/build.py --missing` les liste).

## Publier une version

Pousser un tag `vX.Y.Z-alpha.N` : GitHub compile le firmware, crée la version et met à
jour le flasheur web et la page de réglages (GitHub Pages, source « GitHub Actions »).
Chaque push sur `main` compile aussi le firmware, sans publier.

# Document Technique : Correspondance Portage C → Code Assembleur

**Alien Breed Special Edition 92 — CD32 / Amiga**
*Produit lors de l'implémentation du portage C — à utiliser pour renommer les labels non identifiés dans main.asm*

---

## Table des matières

1. [Structure générale du programme](#1-structure-générale-du-programme)
2. [Variables globales — correspondances lbW/lbL → noms C](#2-variables-globales--correspondances-lbwlbl--noms-c)
3. [Utilisation du matériel Amiga](#3-utilisation-du-matériel-amiga)
4. [Séquence d'initialisation et boucle principale](#4-séquence-dinitialisation-et-boucle-principale)
5. [Procédures de chargement des assets](#5-procédures-de-chargement-des-assets)
6. [Logique des niveaux — init et particularités](#6-logique-des-niveaux--init-et-particularités)
7. [Tables de tuiles et leurs attributs](#7-tables-de-tuiles-et-leurs-attributs)
8. [Logique des armes et projectiles](#8-logique-des-armes-et-projectiles)
9. [Logique des IA — aliens normaux](#9-logique-des-ia--aliens-normaux)
10. [Logique des IA — boss](#10-logique-des-ia--boss)
11. [Séquence de destruction et compte à rebours](#11-séquence-de-destruction-et-compte-à-rebours)
12. [Labels C non utilisés directement — analyse de labels ASM non nommés](#12-labels-c-non-utilisés-directement--analyse-de-labels-asm-non-nommés)

---

## 1. Structure générale du programme

Le jeu est découpé en plusieurs binaires séparés, chacun correspondant à une `section` du fichier assembleur :

| Binaire / Section  | Fichier ASM            | Rôle                                                  |
|--------------------|------------------------|-------------------------------------------------------|
| `start` / `main`   | `src/main/main.asm`    | Moteur principal, boucle de jeu, IA, tuiles, armes    |
| `menu`             | `src/menu/menu.asm`    | Écran titre, menu principal, copper palette           |
| `intex`            | `src/intex/intex.asm`  | Terminal INTEX (boutique inter-niveaux)               |
| `briefingstart`    | `src/briefingstart/`   | Premier briefing (niveau 1)                           |
| `briefingcore`     | `src/briefingcore/`    | Briefings niveaux 2–12                                |
| `gameover`         | `src/gameover/gameover.asm` | Animation game-over IFF ANIM-5                   |
| `end`              | `src/end/end.asm`      | Écran de fin de jeu                                   |
| `story`            | `src/story/`           | Écran d'intro / story                                 |

Le programme principal (section `start`) démarre à l'étiquette `begin` (L253) et appelle successivement :
```
begin → obtain_vbr_register → disable_cache → load_graphics_lib →
        install_sound_interrupt → disable_interrupts → set_blank_copper →
        install_lev4irq → kill_system → init_main_copperlist →
        init_tables → set_main_copperlist → convert_input_table_to_keycodes
```

---

## 2. Variables globales — correspondances lbW/lbL → noms C

### 2.1 Variables de contrôle de la boucle de jeu

| Label ASM               | Offset / Type | Nom C suggéré                    | Rôle                                                                                       |
|-------------------------|--------------|----------------------------------|--------------------------------------------------------------------------------------------|
| `game_running_flag`     | dc.w         | `g_game_running_flag`            | 1 = boucle de jeu active ; 0 = sortie                                                      |
| `flag_end_level`        | dcb.w 2      | `g_flag_end_level`               | ≠ 0 = le joueur a atteint la sortie (exit tile)                                            |
| `flag_jump_to_gameover` | dc.l         | `g_flag_jump_to_gameover`        | ≠ 0 = déclencher game-over                                                                 |
| `flag_destruct_level`   | dc.l         | `g_flag_destruct_level`          | ≠ 0 = niveau en cours de destruction                                                       |
| `in_destruction_sequence_flag` | dcb.w 2 | `g_in_destruction_sequence`    | ≠ 0 = séquence cinématique d'explosion en cours                                            |
| `map_overview_on`       | dc.w         | `g_map_overview_on`              | 1 = vue d'ensemble de la carte active                                                      |
| `frame_flipflop`        | dc.w         | (interne)                        | Alterne 0/−1 chaque VBL ; cadence logique à 25 Hz                                         |
| `lbW0004BA`             | dc.w         | `s_game_tick_ready`              | Mis à 1 par le IRQ niveau 3 toutes les 2 VBL ; la boucle attend ce flag pour avancer d'un tick |
| `lbW0004B2`             | dc.w         | `s_render_ready_flag`            | Mis à 1 en fin de tick par la boucle principale                                             |
| `lbW0004BC`             | dc.w         | `s_vbl_sub_counter`              | Compte les VBL (0-1) ; déclenche `lbW0004BA` à 2                                          |
| `lbW0003A2`             | dc.w (=1)    | `s_music_disabled_flag`          | 1 = musique désactivée (mode debug) ; 0 = mode normal avec musique                        |
| `music_enabled`         | dc.w         | `g_music_enabled`                | 0 = musique activée (flag inversé : 0 = on, 1 = off)                                      |
| `done_fade`             | dc.w         | `g_done_fade`                    | ≠ 0 = fondu terminé                                                                        |
| `slowdown_pause_display`| dc.w (=5)    | `s_pause_slowdown_ctr`           | Compteur anti-rebond pour l'entrée PAUSE                                                   |
| `rnd_number`            | dc.w         | `g_rnd_number`                   | Valeur pseudo-aléatoire courante (max 256, calculée dans l'IRQ)                            |
| `done_holocode_jump`    | dc.w         | `g_done_holocode_jump`           | Empêche un double saut via holocode                                                         |
| `in_intex_map_flag`     | dc.w         | `g_in_intex_map_flag`            | ≠ 0 = le jeu est en mode carte INTEX                                                       |
| `player_using_intex`    | dc.l         | `g_player_using_intex`           | Pointeur vers la structure joueur utilisant le terminal                                     |
| `run_intex_ptr`         | dc.l         | `g_run_intex_ptr`                | Si ≠ 0, l'INTEX doit être lancé ; contient l'adresse de `run_intex`                       |
| `share_credits`         | dc.w         | `g_share_credits`                | 1 = les crédits sont partagés entre joueurs                                                 |
| `input_enabled`         | dc.w         | `g_input_enabled`                | 1 = les entrées clavier/joystick sont traitées                                              |
| `lbW0004EA`             | dc.w         | `g_boss_active`                  | 1 = rencontre de boss active (empêche un double déclenchement)                             |
| `lbW0004D8`             | dc.w         | `s_self_destruct_trigger_flag`   | Mis à 1 lors du déclenchement de la séquence de destruction (via tuile, alarme ou boss)   |
| `lbW0004EE`             | dc.w         | `s_boss4_active`                 | Spécifique boss_nbr=4 (niveau 10) : mis à 1 lors de l'activation des 7 patrouilles         |
| `lbW0004E6`             | dc.w         | `s_fire_door_activated`          | Niveaux 3 et 12 : 1 = la séquence de portes coupe-feu a été activée                       |

### 2.2 État des joueurs

| Label ASM               | Offset / Type | Nom C suggéré                    | Rôle                                                                                       |
|-------------------------|--------------|----------------------------------|--------------------------------------------------------------------------------------------|
| `player_1_data`         | struct ~450o | `g_players[0]`                   | Structure complète du joueur 1                                                              |
| `player_2_data`         | struct ~450o | `g_players[1]`                   | Structure complète du joueur 2                                                              |
| `player_1_input`        | dc.b         | (lu dans IRQ)                    | Octet d'entrée courant du joueur 1 (joystick + boutons)                                    |
| `player_2_input`        | dc.b         | (lu dans IRQ)                    | Octet d'entrée courant du joueur 2                                                          |
| `player_1_old_input`    | dc.b         | (détection front montant)        | Ancienne entrée P1 pour détecter le flanc de `NEXT_WEAPON`                                 |
| `player_2_old_input`    | dc.b         | (détection front montant)        | Ancienne entrée P2                                                                          |
| `number_players`        | dc.l         | `g_number_players`               | 1 ou 2 joueurs                                                                              |
| `lbW005D64`             | dc.w (dans player_1_data) | `g_players[0].death_counter` | Compteur d'animation de mort du joueur 1 (200 à 0) ; ≠ 0 = en cours de mort              |
| `lbW006504`             | dc.w (dans player_2_data) | `g_players[1].death_counter` | Compteur d'animation de mort du joueur 2                                                    |
| `lbW007B46`             | dc.w         | (dérivé de `alive`)              | Mis à 1 si le joueur traité est « vivant » (non en train de mourir) ; gardien de sécurité dans les handlers de tuiles |
| `player_1_tbl_weapon_pos` | dc.l (=14) | `s_p1_weapon_table_pos`         | Pointeur courant dans `weapons_attr_table` pour le joueur 1 (incrémenté de 14 par arme)   |
| `player_2_tbl_weapon_pos` | dc.l (=14) | `s_p2_weapon_table_pos`         | Idem pour le joueur 2                                                                       |

### 2.3 État des aliens

| Label ASM               | Offset / Type | Nom C suggéré                    | Rôle                                                                                       |
|-------------------------|--------------|----------------------------------|--------------------------------------------------------------------------------------------|
| `alien1_struct`–`alien7_struct` | struct | `g_aliens[0]`–`g_aliens[6]`    | Tableaux de structures des aliens actifs (7 slots) — voir section 9                       |
| `global_aliens_extra_strength` | dc.w    | `g_global_aliens_extra_strength` | Bonus de HP ajouté à chaque alien lors de sa création (niveaux 9–12 : 5/10/15/20)        |
| `boss_nbr`              | dc.w (=1)    | `g_boss_nbr`                     | Type de boss pour le niveau courant (0=aucun, 1-4)                                         |
| `lbW009C62`             | dc.w         | `s_boss_retreat_countdown`       | Compteur de retraite boss : chargé à 40 par le déclencheur aléatoire, décrémenté chaque tick |
| `lbL009C64`             | dc.l         | `s_boss_retreat_flag`            | 0 = poursuite du joueur, 1 = retraite (fuite du joueur)                                    |
| `lbW0097F2`             | dc.w (=20)   | `s_target_refresh_countdown`     | Compteur de rafraîchissement de la position cible des aliens (toutes les 20 VBL)           |
| `lbW009CDE`             | dc.w         | `s_boss_anim_flipflop`           | Alternance d'animation boss (cadence visuelle)                                              |
| `lbW009CE0`             | dc.w         | `s_boss_sound_trigger`           | Déclenche le son de rugissement boss aléatoire                                              |
| `play_alien_hatching_sample` | dc.w    | `s_play_hatching_sample`         | ≠ 0 → jouer le sample d'éclosion d'alien                                                   |
| `cur_aliens_speed`      | dc.w         | `s_cur_alien_spawn_speed`        | Vitesse aléatoire calculée par `get_alien_rnd_speed`, appliquée au prochain alien créé    |
| `lbL00D226`             | dc.l         | `s_next_spawn_alien_struct_ptr`  | Pointeur vers le descripteur de structure alien à utiliser pour la prochaine éclosion (lbW008F94/lbW009094/lbW009414) |
| `lbL00D29A`             | (8 longs)    | `s_spawn_queue_an[]`             | File de création d'aliens normaux (8 emplacements, 4 longs chacun : position + struct)    |
| `lbL00D2AA`             | (8 longs)    | `s_spawn_queue_bo[]`             | File de création d'aliens boss / secondaires                                               |

### 2.4 État des niveaux

| Label ASM               | Offset / Type | Nom C suggéré                    | Rôle                                                                                       |
|-------------------------|--------------|----------------------------------|--------------------------------------------------------------------------------------------|
| `cur_level`             | dc.l         | `g_cur_level`                    | Adresse du label de début du niveau courant (level_2, level_4, …) ou 0 pour le niveau 1  |
| `level_flag`            | dc.l (=-1)   | `s_level_flag`                   | Valeur spécifique au niveau déterminant quel jeu d'animations de tuiles et de comportements activer (voir section 6) |
| `exit_unlocked`         | dc.w         | `g_exit_unlocked`                | 1 = la sortie est franchissable (tuile EXIT = passable)                                    |
| `select_speed_boss`     | dc.w         | `s_select_speed_boss`            | 1 = les aliens utilisent leur vitesse de struct au lieu d'une vitesse aléatoire (mode boss) |
| `lbW0005AA`             | dc.w (=20)   | `s_alien_speed_range`            | Plage maximale pour la vitesse aléatoire des aliens (toujours 20)                          |
| `lbW0004EE`             | dc.w         | *(voir 2.1)*                     | Drapeau spécifique boss_nbr=4 (voir ci-dessus)                                             |
| `lbW002E04`             | dc.w         | `s_lvl9_auto_destruct_counter`   | Niveau 9 uniquement : compteur de 300 → 0 ; quand il atteint 0, active `self_destruct_initiated` |
| `lbW002AC0`             | dc.w         | `g_alarm_buttons_pressed`        | Nombre de boutons d'alarme activés (niveaux 3 et 6)                                        |
| `lbW002AC2`             | dc.w         | `g_alarm_system_active`          | 1 = le système d'alarme est armé (niveau 3)                                                |
| `lbL00E756`             | dc.l         | `g_alarm_last_tile_ptr`          | Pointeur vers la dernière tuile d'alarme activée (évite de comptabiliser deux fois)        |
| `lbW008C9A`             | dc.w         | `g_zone_voice_id`                | ID de zone annoncée par la voix (1-6 pour "ZONE ONE".."ZONE SIX") ; niveau 8              |
| `lbW0084C4`             | dc.w         | `s_acid_pool_damage_ctr`         | Compte à rebours entre deux applications de dégâts acide (25 ticks)                       |

### 2.5 Graphismes et carte

| Label ASM               | Offset / Type | Nom C suggéré                    | Rôle                                                                                       |
|-------------------------|--------------|----------------------------------|--------------------------------------------------------------------------------------------|
| `cur_palette_ptr`       | dc.l         | `g_cur_palette_ptr`              | Pointeur vers la palette de couleurs du niveau courant (`level_palette1`)                  |
| `lbL000554`             | dc.l         | `g_alien_sprite_table_ptr`       | Pointeur vers la table des descripteurs de sprites aliens (dépend du niveau → LEGACY ou COMPACT) |
| `lbL000558`             | dc.l         | `g_tile_sprite_table_ptr`        | Pointeur vers la table des descripteurs de sprites de tuiles (BOB table)                   |
| `cur_map_top_ptr`       | dc.l         | (équiv. `cur_map_top`)           | Pointeur base du buffer anneau de la carte                                                  |
| `map_pos_x`/`map_pos_y` | dc.l         | `g_map_pos_x`/`g_map_pos_y`      | Position en pixels de la caméra dans le monde                                               |
| `frame_bkgnd_flag`      | dc.w         | `s_frame_bkgnd_flag`             | 1 = redessiner le fond ce tick, 2 = redessiner en double-buffer                            |
| `lbW0113B8`             | dc.w         | `s_scroll_lock_flag`             | Si négatif, force le mode double-buffer du fond                                             |
| `lbW023E9C`             | dc.w         | `s_palette_fade_timer`           | Décompte pour la durée d'un fondu de palette                                                |

### 2.6 Audio

| Label ASM               | Offset / Type | Nom C suggéré                    | Rôle                                                                                       |
|-------------------------|--------------|----------------------------------|--------------------------------------------------------------------------------------------|
| `sample_to_play`        | dc.w         | `g_sample_to_play`               | Index du sample à déclencher (lu par `trigger_sample`)                                     |
| `sample_struct_to_play` | dc.l         | `g_sample_struct_to_play`        | Pointeur vers une structure sample complète à jouer                                         |
| `audio_dmacon`          | dc.w         | (DMACON audio)                   | Cache de la valeur DMACON pour les canaux audio                                             |
| `lbW023156`             | struct       | `smp_zone_struct_1`              | Structure de sample pour l'annonce de zone (utilisée par `schedule_sample_to_play`)        |
| `lbW02328A`             | dc.w         | `s_infinite_ammo_flag`           | ≠ 0 = mode munitions infinies (debug)                                                       |

### 2.7 Projectiles

| Label ASM                  | Offset / Type | Nom C suggéré                  | Rôle                                                                                     |
|----------------------------|--------------|--------------------------------|------------------------------------------------------------------------------------------|
| `projectile_struct_1`–`5`  | struct 26 o  | `g_projectiles[0]`–`[4]`       | 5 structures de projectiles actifs (offset 0 = pos X/Y, 4 = vit X/Y, 8 = ptr BOB, 16 = force, 18 = pénétrant, 20 = ptr joueur, 24 = compteur rebonds) |
| `lbL00E9C2`                | dc.l (liste) | `s_p1_projectile_list`         | Liste des projectiles appartenant au joueur 1 (pointeurs vers projectile_struct_1/2/3)   |
| `lbL00E9D2`                | dc.l (liste) | `s_p2_projectile_list`         | Liste des projectiles appartenant au joueur 2 (projectile_struct_4/5)                    |
| `lbW00E958`/72/8C/A6/C0`   | dc.w (=0)    | `Projectile.bounce_count`      | Compteur de rebonds restants (offset 24 de la struct projectile)                          |
| `lbL00EAA6`                | dc.l (=0)    | `s_arc_counter`                | Compteur de tir en arc (FLAMEARC/PLASMAGUN/LAZER) : cycle 1→2→3→0                        |
| `lbW0004D6`                | dc.w (=8)    | `s_bounce_speed_default`       | Valeur de remplacement si la composante de vitesse est nulle après rebond (8 px/tick)     |

### 2.8 Réacteur et portes coupe-feu (niveau 8)

| Label ASM               | Offset / Type | Nom C suggéré                    | Rôle                                                                                       |
|-------------------------|--------------|----------------------------------|--------------------------------------------------------------------------------------------|
| `reactor_up_done`       | dc.w         | `g_reactor_up_done`              | Nombre de coups sur la face supérieure du réacteur (6 coups → face détruite)              |
| `reactor_left_done`     | dc.w         | `g_reactor_left_done`            | Face gauche                                                                                 |
| `reactor_down_done`     | dc.w         | `g_reactor_down_done`            | Face inférieure                                                                             |
| `reactor_right_done`    | dc.w         | `g_reactor_right_done`           | Face droite                                                                                 |
| `reactor_to_patch`      | dc.l         | `s_reactor_to_patch_ptr`         | Pointeur vers les données de remplacement de tuile du réacteur en cours de destruction    |
| `door_impact`           | dc.w         | `g_door_impact_accum`            | Accumulation de dégâts sur une porte (300 coups → porte forcée)                           |
| `lbL00E4EC`             | dc.l         | `g_door_impact_tile_ptr`         | Pointeur vers la tuile de porte en cours de bombardement (changer = reset le compteur)    |
| `lbW00E4F0`             | dc.w         | `s_door_hit_ctr`                 | Compteur de coups sur la porte (décrémente les munitions du tireur tous les 2 coups)      |
| `lbW00853C`             | dc.w         | `s_intex_used_flag`              | Mis à 0 avant d'appeler `run_intex` pour éviter une double activation                     |

---

## 3. Utilisation du matériel Amiga

### 3.1 Registres custom chips (OCS/ECS)

Le jeu accède aux registres custom via la constante `CUSTOM` (= `$DFF000`).

| Registre                          | Accès                       | Usage dans le jeu                                                    |
|-----------------------------------|-----------------------------|----------------------------------------------------------------------|
| `CUSTOM+COLOR00` (`$180`)         | Écriture                    | Flash d'erreur (`error_flash`), fondu de palette (`copper_main_palette`) |
| `CUSTOM+COP1LCH` (`$080`/`$082`) | Écriture                    | Chargement de la copperlist principale ou de la copperlist vide (`copper_blank`) |
| `CUSTOM+INTREQ` (`$09C`)          | Écriture                    | Accusé de réception des interruptions : `INTF_VERTB` (VBL) et `INTF_COPER` (copper) |
| `CUSTOM+INTREQR` (`$01E`)         | Lecture (IRQ lev3)          | Détection type d'interruption : bit 4 = copper, bit 5 = VBL          |
| `CUSTOM+POTINP` (`$016`)          | Lecture                     | Bouton fire 2 (bit 10/14 selon port)                                  |
| `CUSTOM+POTGO` (`$034`)           | Écriture                    | Configuration des lignes POT pour la lecture du bouton                |
| `CUSTOM+VHPOSR` (`$006`)          | Lecture (busy-wait)         | Attente de la ligne de balayage `$FF` puis `$2C` (synchronisation VBL) dans `do_level_destruction` |
| `CUSTOM+DMACON` via `sprites_dma`/`audio_dmacon` | Écriture | Activation/désactivation DMA sprites et audio                       |

### 3.2 CIAA / CIA

| Registre                     | Accès       | Usage                                                                            |
|------------------------------|-------------|----------------------------------------------------------------------------------|
| `CIAA` (`$BFE001`)           | Lecture/Écriture | Joystick/fire port A (bit `CIAB_GAMEPORT0`), direction DDR                    |
| `CIAB_GAMEPORT0` / `CIAB_GAMEPORT1` | Constantes | Bits de lecture des ports joystick dans `user_input`                        |
| `key_pressed`                | Variable    | Dernier code de touche reçu (traité par `keyboard_handler` dans l'IRQ niveau 3) |

### 3.3 Interruptions

| Interruption    | Vecteur                  | Rôle                                                                                           |
|-----------------|--------------------------|------------------------------------------------------------------------------------------------|
| Niveau 3 (VBL + Copper) | `lev3irq`        | Lecture joystick (toutes les VBL), tick de jeu à 25 Hz, fondu palette, scroll, rendu copper  |
| Niveau 5 (CIA)  | `lev5irq` → `lbC024142` | Driver audio (Soundmon/BPMusic) + canal audio 4 (samples SFX)                                 |

**Fonctionnement du tick à 25 Hz** (dans `lev3irq`, L811–L874) :
```
Chaque VBL : not.w frame_flipflop
  → Si frame_flipflop ≠ 0 : set lbW0004B4 = -1 (semaphore)
  → Lit joystick P1 et P2
  → Incrémente lbW0004BC
  → Si lbW0004BC ≥ 2 : lbW0004BC = 0, lbW0004BA = 1 (≈ tick 25 Hz déclenché)
```
La boucle principale (`game_level_loop`) attend `lbW0004BA ≠ 0` avant chaque mise à jour.

### 3.4 Copperlist

| Label ASM                | Rôle                                                                                             |
|--------------------------|--------------------------------------------------------------------------------------------------|
| `copper_blank`           | Copperlist vide (fond noir), utilisée pendant les transitions                                    |
| `copper_main_palette`    | Zone de la copperlist où sont écrites les 32 couleurs de palette (registres COLOR00–COLOR1F)    |
| `copper_copyright_pal`   | Palette pour l'écran de copyright (menu)                                                         |
| `lbW09A250`, `lbW09A2C4` | Zones de la copperlist gérant les registres sprites (SPR0PTH/L…SPR7PTH/L)                     |

---

## 4. Séquence d'initialisation et boucle principale

### 4.1 Séquence de démarrage d'un niveau

Pour chaque niveau, le code exécute exactement la séquence suivante (L278–L286, etc.) :

```
install_level_tune       → charge et installe la musique de niveau (soundmon_level)
display_briefing_N       → affiche le texte de briefing (via exe_briefingcore/briefingstart)
init_level_N             → initialise les variables spécifiques au niveau (voir section 6)
hold_briefing_screen     → attend un appui bouton sur l'écran de briefing
copy_gfx                 → copie les graphismes de sprites en CHIP RAM
set_destruction_timer    → initialise le compteur de destruction avec timer_digit_hi:lo
init_level_variables     → remet à zéro les drapeaux de jeu (flag_end_level, etc.)
init_players_variables   → réinitialise la position et l'animation des joueurs
init_aliens_variables    → vide toutes les structures aliens (lbC00FA4C)
finalize_level           → positionne la caméra, copper, position de départ des joueurs
game_level_loop          → boucle de jeu principale (25 Hz)
```

### 4.2 Boucle de jeu principale (`game_level_loop`, L521)

```
game_level_loop:
  destruction_sequence          ; si self_destruct_initiated → lancer cinématique
  game_running_flag = 1
  [attendre lbW0004BA = 1]      ; synchronisation 25 Hz
  lbC003EFC                     ; mise à jour du timer de destruction
  lbC00ACE4                     ; mise à jour/rendu des sprites aliens
  lbC00A6C2                     ; traitement des tuiles autour des joueurs
  lbC0097F6                     ; rafraîchissement de la position cible des aliens (tous les 20 ticks)
  
  [niveau 9] cmp #7, timer_digit_lo : si timer=7 → décompte lbW002E04 → self_destruct
  [niveau 3] si lbW002AC2=1 et lbW002AC0≥3 → self_destruct (alarme)
  
  aliens_collisions_with_weapons ; collision projectiles / aliens
  aliens_collisions_with_players ; collision aliens / joueurs
  lbC00E8F0                     ; mise à jour position des projectiles
  lbC011860                     ; rendu des projectiles (BOBs)
  jump_to_intex                 ; si run_intex_ptr ≠ 0 → exécuter l'INTEX
  check_players_invincibility   ; décompte d'invincibilité après dégât
  lbC00D17E                     ; vérification et création de nouveaux aliens (spawn)
  print_more_6_keys_sign        ; affichage du signe "+" si joueur a >6 clés
  lbC006C7A                     ; déplacement et action des joueurs
  
  [si flag_jump_to_gameover] → trigger_game_over
  [si flag_end_level] → exit (retour au code de séquencement)
  [si map_overview_on + bouton] → display_map_overview
  [si touche P / bouton PAUSE] → display_pause
  [si touche ESC / both fire held en mode musique] → trigger_game_over
  
  [gestion changement d'arme P1/P2]
  → boucle
```

---

## 5. Procédures de chargement des assets

### 5.1 Structure de chargement par niveau

Chaque niveau définit une `lev_N_load_struct` (L7972–L8018) contenant 3 entrées (longueur + destination) :

```
lev1_load_struct:   dc.l 'L0MA', aliens_sprites_block  ; → carte principale
                    dc.l 'L0AN', bkgnd_anim_block       ; → sprites animations fond
                    dc.l 'L0BO', aliens_sprites_block   ; → sprites aliens/boss
```

| Nom de fichier | Contenu                                    | Destination mémoire        |
|----------------|--------------------------------------------|----------------------------|
| `LNMA` (L0MA…LBMA) | Carte IFF (format BODY : tiles 120×96) | `cur_map_top` / anneau de 248 octets/ligne |
| `LNAN` (L0AN…L5AN) | Sprites animés de fond (engine tiles)  | `bkgnd_anim_block`         |
| `LNBO` (L0BO…L5BO) | Sprites aliens et boss                 | `aliens_sprites_block`     |
| `(tilespic_filename)` | Image de fond 320×256 (palette ×4)  | `bkgnd_tiles_block`        |

### 5.2 Procédure `load_level` (L8056)

```
load_level:
  load_map_file         ; lit le fichier LNMA dans temp_map_buffer
    → retrieve_bkgnd_tiles_filename  ; extrait le nom du fichier de tuiles de la map
    → get_map_palettes               ; extrait les 2 palettes de 32 couleurs
    → get_map_datas                  ; cherche le chunk BODY et copie 23040 octets
  copy_map_datas        ; transfère la carte dans aliens_sprites_block
  load_map_sprites      ; charge L_AN (animations de fond)
  load_map_bkgnd_tiles  ; charge le fichier de tuiles de fond
  load_map_sprites      ; charge L_BO (sprites aliens)
```

### 5.3 Format de la carte (IFF BODY)

- Taille : 248 octets/ligne × 103 lignes = 25544 octets dans le buffer ASM (3 lignes d'en-tête + 100 lignes de données)
- Taille utile : 248 octets/ligne × 96 lignes = données de jeu (MAP_HEIGHT = 96 en C, MAP_WIDTH = 120 tuiles)
- Format d'un mot de tuile (16 bits) :
  - Bits 0–5 : attribut de tuile (voir section 7)
  - Bits 6–15 : index graphique de la tuile
- Stride (pas de ligne) : 248 octets = 124 mots (120 mots utiles + 4 mots de padding)
- **Conversion adresse → coordonnées** : `offset = adresse - cur_map_top` ; `ligne = offset / 248` ; `colonne = (offset % 248) / 2`
- Le portage C n'inclut **pas** les 3 lignes d'en-tête : les coordonnées IFF sont directement ligne/colonne 0-indexé

### 5.4 Chargement des exécutables externes

Les sous-programmes du jeu (INTEX, menu, game-over, etc.) sont chargés dans `temp_buffer` puis exécutés depuis là :

```
run_intex:   jsr load_exe(exe_intex) → jsr temp_buffer
run_menu:    jsr load_exe(exe_menu)  → jsr temp_buffer
run_end:     jsr load_exe(exe_end)   → jsr temp_buffer
```

---

## 6. Logique des niveaux — init et particularités

### 6.1 Tableau récapitulatif des niveaux

| N° | Label      | Fichier MA | Fichier AN | Fichier BO | `level_flag` | Timer (hi:lo) | `exit_unlocked` | `boss_nbr` | Particularités                          |
|----|------------|-----------|-----------|-----------|--------------|--------------|----------------|-----------|----------------------------------------|
| 1  | (début)    | L0MA      | L0AN      | L0BO      | -256 (0xFF00) | 6:0 (60 s)   | 1              | 0         | Sortie ouverte dès le départ            |
| 2  | `level_2`  | L1MA      | L1AN      | L1BO      | 0            | 6:0 (60 s)   | 0              | 0         | —                                      |
| 3  | —          | L2MA      | L3AN      | L3BO      | 512          | 4:0 (40 s)   | 0              | 0         | Alarme à 3 boutons → destruction ; `lbW002AC2=1`, `lbW0004EE` clr, `lbL01FDAA=500` |
| 4  | `level_4`  | L3MA      | L4AN      | L4BO      | 768          | 9:0 (90 s)   | 1              | 0         | —                                      |
| 5  | —          | L4MA      | L4AN      | L4BO      | 768          | 9:0 (90 s)   | 0              | 1         | Boss alien queen (lbW009114) ; `lbW009C62=0` |
| 6  | `level_6`  | L5MA      | L3AN      | L3BO      | 512          | 0:2 (2 s)    | 1 (ironique)   | 0         | Timer très court ; portes coupe-feu, `lbL004BE4`–`lbL004C00` clr, `lbC004DB4` init |
| 7  | —          | L6MA      | L3AN      | L2BO      | 256          | 9:9 (99 s)   | 0              | 2         | Boss queen + 2 secondaires ; portes coupe-feu init |
| 8  | `level_8`  | L7MA      | L3AN      | L2BO      | 256          | 6:0 (60 s)   | 1              | 2         | Réacteur 4 faces ; zones 0x30-0x33 ; `boss_nbr=2` = bouclier réacteur |
| 9  | —          | L8MA      | L2AN      | L2BO      | 256          | 7:7 (77 s)   | 1              | 0         | `lbW002E04=300` (compteur auto-destruction) ; `alien_extra_strength=5` |
| 10 | `level_10` | L9MA      | L1AN      | L1BO      | 0            | 8:0 (80 s)   | 0              | 4         | 7 patrouilles orbitales ; `lbW0004EE=0` |
| 11 | —          | LAMA      | L1AN      | L2BO      | 0            | 6:0 (60 s)   | 1              | 0         | `alien_extra_strength=15`              |
| 12 | —          | LBMA      | L5AN      | L5BO      | 1024         | 1:4 (14 s)   | 0              | 3         | Boss final ; `map_overview_on=0` (DEBUG) ; `lbL01FDAA=500` ; `lbW0004E6=0` |

### 6.2 Signification de `level_flag`

`level_flag` est une longue utilisée comme discriminant dans les handlers de tuiles et dans la sélection du jeu d'animation moteur :

| Valeur (`level_flag`) | Niveaux  | Effet principal                                                                 |
|-----------------------|----------|---------------------------------------------------------------------------------|
| -256 (0xFFFFFF00)     | 1        | `tile_facehuggers_hatch` : sélectionne `lbW008F94` (grand alien standard)      |
| 0                     | 2, 10, 11 | `tile_facehuggers_hatch` : sélectionne `lbW008F94` ; `lbC004962` = rts pour tuile 0x1B |
| 256                   | 7, 8, 9  | `tile_facehuggers_hatch` : sélectionne `lbW009094` (grand alien secondaire) ; seules animations 0x18/0x19 |
| 512                   | 3, 6     | `tile_facehuggers_hatch` : rts (pas d'éclosion) ; tuiles 0x1A-0x1D → handlers d'alarme (fire doors alarm) |
| 768                   | 4, 5     | `tile_facehuggers_hatch` : rts ; toutes les animations 0x18-0x1C              |
| 1024                  | 12       | `tile_facehuggers_hatch` : sélectionne `lbW009414` (facehugger) ; `boss_move` active la palette blanche lors des hits ; animation 0x19 = bra.w none |

### 6.3 Niveau 3 — Système d'alarme

Le niveau 3 (`init_level_3`) initialise :
- `lbW002AC2 = 1` → système d'alarme armé
- `lbW002AC0 = 0` → compteur de boutons pressés
- `lbL00E756 = 0` → pointeur du dernier bouton pressé

Chaque fois qu'un projectile frappe une tuile de type `alarm` (0x12 ou 0x13, traité par `patch_fire_door_left_btn_alarm` / `patch_fire_door_right_btn_alarm`) :
- Si `lbW002AC2 ≠ 0` et que la tuile est différente de `lbL00E756` : `lbW002AC0++`, mémoriser `lbL00E756 = a5`
- Quand `lbW002AC0 ≥ 3` : dans `game_level_loop` → `self_destruct_initiated = 1`

À la fin du niveau 3 (L324), reset :
```asm
clr.w lbW002AC0
clr.l lbL00E756
move.w #0, lbW002AC2
```

### 6.4 Niveau 6 — Timer bizarre

Le timer est initialisé `timer_digit_hi = 0` (`sf.b timer_digit_hi`) et `timer_digit_lo = 2`. Cette valeur spéciale sert de discriminant dans `tile_start_destruction` (L5501) :
```asm
cmp.b #2, timer_digit_lo   ; si lo=2 → déclencher directement la destruction via lbC0083DE
```
Autrement dit, le niveau 6 n'a pas de timer vrai ; la destruction est déclenchée par un événement (le 1-UP), et la branche `cmp.b #2` est un cas particulier codé en dur.

### 6.5 Niveau 8 — Réacteur à 4 faces

Tuiles réacteur : 0x2A (haut), 0x2B (gauche), 0x2C (bas), 0x2D (droite).
- Chaque face : 6 coups pour être détruite.
- Variables de suivi : `reactor_up_done`, `reactor_left_done`, `reactor_down_done`, `reactor_right_done`.
- Quand les 4 vont à zéro dans `check_reactors` : `self_destruct_initiated = 1`.
- Sample lors de destruction d'une face : index 11 (`SAMPLE_REACTOR_BLAST`).

Tuiles de zone (niveau 8 uniquement) : 0x30–0x33 → annonce voix "ZONE N" via `lbW008C9A` + `lbC022D1E`.

### 6.6 Niveau 9 — Compte à rebours automatique

`lbW002E04` est initialisé à 300 dans `init_level_9`.
Chaque tick de 25 Hz dans `game_level_loop` :
```asm
cmp.b #7, timer_digit_lo   ; seulement actif quand le timer affiche 7
subq.w #1, lbW002E04
bne …
move.w #1, self_destruct_initiated
```
Ce mécanisme simule le fait que la destruction du niveau 9 est inévitable après ~12 secondes (300/25).

### 6.7 Niveaux 6 et 7 — Portes coupe-feu (Fire Doors)

Les niveaux utilisant `level_flag = 512` (niveaux 3, 6) et 256 (niveaux 7, 8, 9) activent les portes coupe-feu. À l'init du niveau, 8 longs (`lbL004BE4`–`lbL004C00`) sont effacés et `lbC004DB4` initialise les structures de porte.

Structure d'une porte coupe-feu (visible dans `patch_fire_door_*_btn`) :
- Bouton gauche (tuile 0x08) → active `lbL020D92` + `lbL020E3A` + `lbL020DE2` + `lbL020DBE`
- Bouton droit (tuile 0x09) → même tuiles, ordre différent
- Résultat : les tuiles adjacentes deviennent des murs (attribut 0x23 `TILE_HARD_CLIMB_RIGHT`) sur 3 rangées de hauteur

---

## 7. Tables de tuiles et leurs attributs

### 7.1 Table d'action des tuiles (`tiles_action_table`, L5059)

Format : tableau de branchements (`bra.w`), un par attribut (0x00–0x3F). Chaque entrée pointe vers un handler dans main.asm.

| Attribut hex | Label ASM (handler)             | Nom C (`constants.h`)         | Comportement joueur                              |
|--------------|---------------------------------|-------------------------------|--------------------------------------------------|
| 0x00         | `tile_floor` / `return`         | `TILE_FLOOR`                  | Plancher passable                                |
| 0x01         | `tile_wall`                     | `TILE_WALL`                   | Mur bloquant                                     |
| 0x02         | `tile_exit`                     | `TILE_EXIT`                   | Sortie du niveau (si `exit_unlocked=1`)           |
| 0x03         | `tile_door`                     | `TILE_DOOR`                   | Porte (nécessite une clé ou `force_door`)        |
| 0x04         | `tile_key`                      | `TILE_KEY`                    | +1 clé, remplace la tuile par plancher           |
| 0x05         | `tile_first_aid`                | `TILE_FIRST_AID`              | Soin complet (health = PLAYER_MAX_HEALTH)        |
| 0x06         | `tile_ammo`                     | `TILE_AMMO`                   | +1 paquet de munitions                           |
| 0x07         | `tile_1up`                      | `TILE_1UP`                    | +1 vie ; déclenche auto-destruction si niveau 6 |
| 0x08         | `patch_fire_door_left_btn` (via `calc_shot_impact`) | `TILE_FIRE_DOOR_A` | Bouton gauche porte coupe-feu (projectile seulement) |
| 0x09         | `patch_fire_door_right_btn`     | `TILE_FIRE_DOOR_B`            | Bouton droit porte coupe-feu                     |
| 0x0A         | `tile_facehuggers_hatch`        | `TILE_FACEHUGGER_HATCH`       | Fait éclore un alien (selon `level_flag`)         |
| 0x0B         | `tile_add_100_credits`          | `TILE_CREDITS_100`            | +100 crédits (×50 = 5000 en interne)             |
| 0x0C         | `tile_add_1000_credits`         | `TILE_CREDITS_1000`           | +1000 crédits (×50 = 50000)                      |
| 0x0D         | `tile_not_used` (= rts)         | `TILE_METALLIC_FLOOR`         | Plancher en grille métallique (ignoré)           |
| 0x0E         | `tile_one_way_up`               | `TILE_ONEWAY_UP`              | `extra_spd_y = -2`                               |
| 0x0F         | `tile_one_way_right`            | `TILE_ONEWAY_RIGHT`           | `extra_spd_x = +2`                               |
| 0x10         | `tile_one_way_down`             | `TILE_ONEWAY_DOWN`            | `extra_spd_y = +2`                               |
| 0x11         | `tile_one_way_left`             | `TILE_ONEWAY_LEFT`            | `extra_spd_x = -2`                               |
| 0x12         | `patch_fire_door_left_btn_alarm`| (ALARM + feu gauche)          | Bouton gauche porte coupe-feu avec alarme (niveaux 3, 6) |
| 0x13         | `patch_fire_door_right_btn_alarm`| (ALARM + feu droit)          | Bouton droit porte coupe-feu avec alarme         |
| 0x14         | `tile_deadly_hole`              | `TILE_DEADLY_HOLE`            | Trou mortel (health = 0 si alive et non en mort) |
| 0x15         | `tile_start_destruction` → `lbC0083DE` | `TILE_DESTRUCT_TRIGGER` | Déclenche l'auto-destruction           |
| 0x16         | `tile_acid_pool`                | `TILE_ACID_POOL`              | -1 HP toutes les 25 frames                       |
| 0x17         | `tile_intex_terminal`           | `TILE_INTEX`                  | Active l'INTEX si Fire2 ou Space                 |
| 0x1A–0x1D   | `lbC00E83E`–`lbC00E8B0` (via impact_table) | (réacteur faces) | Tuiles réacteur 0x2A-0x2D frappées par projectile |
| 0x23         | `tile_hard_climb_right`         | `TILE_HARD_CLIMB_RIGHT`       | Mur pour joueur vivant ; `extra_spd_x=+2` pour joueur mourant |
| 0x26         | `tile_one_deadly_way_right`     | `TILE_ONE_DEADLY_WAY_RIGHT`   | Convoyeur + mort si vient de gauche             |
| 0x27         | `tile_climb_left`               | `TILE_CLIMB_LEFT`             | `extra_spd_x = +1`                              |
| 0x28         | `lbC0049EA`                     | `TILE_ALIEN_SPAWN_BIG`        | Point de spawn grand alien                       |
| 0x29         | `lbC004A18`                     | `TILE_ALIEN_SPAWN_SMALL`      | Point de spawn facehugger                        |
| 0x2A–0x2D   | `patch_reactor_*` (via impact)  | (réacteur faces)              | Faces du réacteur : 6 coups pour détruire        |
| 0x2E         | `tile_one_deadly_way_left`      | `TILE_ONE_DEADLY_WAY_LEFT`    | Convoyeur + mort si vient de droite             |
| 0x2F         | `tile_climb_right`              | `TILE_CLIMB_RIGHT`            | `extra_spd_x = -1`                              |
| 0x30         | `tile_unknown5`                 | `TILE_ZONE_1_BOUNDARY`        | Annonce "ZONE ONE" (si adjacent=0x30) ou "ZONE THREE" |
| 0x31         | `tile_unknown6`                 | `TILE_ZONE_2_BOUNDARY`        | Annonce "ZONE TWO" (si adjacent=0x31) ou "ZONE FOUR" |
| 0x32         | `tile_unknown7`                 | `TILE_ZONE_5_BOUNDARY`        | Annonce toujours "ZONE FIVE"                    |
| 0x33         | `tile_force_fields_sequence`    | `TILE_ZONE_6_TRIGGER`         | Annonce toujours "ZONE SIX"                     |
| 0x34         | (hole avec alien qui sort)      | `TILE_ALIEN_HOLE`             | Trou d'éclosion : invoque un alien avec animation zoom-in |
| 0x37         | `tile_climb_up`                 | `TILE_CLIMB_UP`               | `extra_spd_y = +1`                              |
| 0x38         | `tile_one_way_up_right`         | `TILE_ONEWAY_DIAG_UR`         | `extra_spd_x=+2, extra_spd_y=-2`               |
| 0x39         | `tile_one_way_down_right`       | `TILE_ONEWAY_DIAG_DR`         | `extra_spd_x=+2, extra_spd_y=+2`               |
| 0x3A         | `tile_one_way_down_left`        | `TILE_ONEWAY_DIAG_DL`         | `extra_spd_x=-2, extra_spd_y=+2`               |
| 0x3B         | `tile_one_way_up_left`          | `TILE_ONEWAY_DIAG_UL`         | `extra_spd_x=-2, extra_spd_y=-2`               |
| 0x3D         | `tile_boss_trigger`             | `TILE_BOSS_TRIGGER`           | Déclenche l'apparition du boss                   |
| 0x3F         | `tile_climb_down`               | `TILE_CLIMB_DOWN`             | `extra_spd_y = -1`                              |

### 7.2 Table d'impact des projectiles (`weapons_special_impact_table`, L9535)

Entrées aux mêmes indices que les attributs de tuile, mais pour les projectiles :

| Attribut | Handler d'impact         | Rôle                                                          |
|----------|--------------------------|---------------------------------------------------------------|
| 0x01     | `impact_on_wall`         | Rebond (FLAMEARC/LAZER) ou destruction du projectile         |
| 0x03     | `impact_on_door`         | Accumulation dégâts porte (`door_impact`) + son              |
| 0x08     | `patch_fire_door_left_btn` | Activation bouton gauche fire door                         |
| 0x09     | `patch_fire_door_right_btn` | Activation bouton droit fire door                         |
| 0x12     | `patch_fire_door_left_btn_alarm` | Idem + compteur alarme                               |
| 0x13     | `patch_fire_door_right_btn_alarm` | Idem                                                  |
| 0x19     | `lbC00E83E`              | Impact sur tuile 0x1A (face réacteur haut, level_flag=512)   |
| 0x1A     | `lbC00E864`              | Impact sur tuile 0x1B (face réacteur gauche)                 |
| 0x1B     | `lbC00E88A`              | Impact sur tuile 0x1C (face réacteur bas)                    |
| 0x1C     | `lbC00E8B0`              | Impact sur tuile 0x1D (face réacteur droite)                 |
| 0x23     | `impact_on_wall`         | Mur dur (TILE_HARD_CLIMB_RIGHT)                              |
| 0x2A     | `patch_reactor_up`       | +1 à `reactor_up_done`, destroy à 6                          |
| 0x2B     | `patch_reactor_left`     | +1 à `reactor_left_done`                                     |
| 0x2C     | `patch_reactor_down`     | +1 à `reactor_down_done`                                     |
| 0x2D     | `patch_reactor_right`    | +1 à `reactor_right_done`                                    |

---

## 8. Logique des armes et projectiles

### 8.1 Table des attributs d'armes (`weapons_attr_table`, L736)

Format : 7 entrées de 7 mots (14 octets), terminées par -1.

| Index | Arme          | Mot 0 (index) | Mot 1 (vitesse) | Mot 2 (cadence) | Mot 3 (force) | Mot 4 (pénétrant) | Mot 5 (sample) | Mot 6 (shots/ammo) |
|-------|---------------|--------------|----------------|----------------|--------------|------------------|----------------|-------------------|
| 1     | MACHINEGUN    | 1            | 16             | 3              | 9            | 0                | 37             | 4                 |
| 2     | TWINFIRE      | 2            | 16             | 8              | 13           | 0                | 4              | 3                 |
| 3     | FLAMEARC      | 3            | 12             | 9              | 19           | 0                | 2              | 2                 |
| 4     | PLASMAGUN     | 4            | 14             | 8              | 12           | 1                | 0              | 1                 |
| 5     | FLAMETHROWER  | 5            | 8              | 3              | 12           | 1                | 6              | 1                 |
| 6     | SIDEWINDERS   | 6            | 16             | 8              | 32           | 0                | 4              | 1                 |
| 7     | LAZER         | 7            | 8              | 8              | 18           | 1                | 3              | 1                 |

**Champs dans la structure joueur (offsets depuis `player_N_data`) :**
- Mot 0 → `PLAYER_WEAPON_INDEX` (258)
- Mot 1 → `PLAYER_WEAPON_SPEED+2` (254)
- Mot 2 → `PLAYER_WEAPON_RATE+2` (262)
- Mot 3 → `PLAYER_WEAPON_STRENGTH` (266)
- Mot 4 → offset 270 (pénétrant, copié en offset 18 de la struct projectile)
- Mot 5 → `PLAYER_WEAPON_SMP` (384)
- Mot 6 → `PLAYER_SHOT_AMOUNT` (398)

### 8.2 Table de comportement des armes (`weapons_behaviour_table`, L10000)

8 entrées (longues) pointant vers des sous-tables de BOB animation par direction (8 directions × 1 ou N frames) :

| Entrée | Arme          | Sous-table      | Frames atlas y (BOB)    |
|--------|---------------|-----------------|-------------------------|
| 0      | (inutilisé)   | `lbL00EDAA`     | —                       |
| 1      | MACHINEGUN    | `lbL00EACA`     | y=176 (0xB0), entrées 24–31 |
| 2      | TWINFIRE      | `lbL00EB8E`     | y=160 (0xA0), entrées 0–7  |
| 3      | FLAMEARC      | `lbL00EC12`     | y=160, entrées 16–23 (8 frames animées) |
| 4      | PLASMAGUN     | `lbL00ECFE`     | y=160/176, entrées 32–39  |
| 5      | FLAMETHROWER  | `lbL00ED82`     | (utilise même sprites que PLASMAGUN) |
| 6      | SIDEWINDERS   | `lbL00EC7A`     | (TWINFIRE variant)        |
| 7      | LAZER         | `lbL00EDAA`     | y=240 (0xF0), entrées 68–71 |

### 8.3 Structures de projectiles

5 slots de projectile actifs (`projectile_struct_1`–`5`, L9938–L9980) :
```
+0  word : pos X (32000 = inactif)
+2  word : pos Y
+4  word : vélocité X
+6  word : vélocité Y
+8  long : pointeur vers la structure BOB courante
+12 word : direction (1-8, = cur_sprite du joueur tireur)
+14 word : ?
+16 word : force (PROJECTILE_STRENGTH)
+18 word : pénétrant (1 = passe à travers aliens)
+20 long : pointeur vers la structure joueur (PROJECTILE_PLAYER)
+24 word : compteur de rebonds
```

Listes de projectiles par joueur :
- `lbL00E9C2` → joueur 1 : slots 1, 2, 3
- `lbL00E9D2` → joueur 2 : slots 4, 5

### 8.4 Arc-fire (FLAMEARC, PLASMAGUN, LAZER)

`lbL00EAA6` est un compteur de cycle 0→1→2→3→0 incrémenté à chaque tir de ces armes :
- Valeur 1 : tir droit (pas de correction)
- Valeur 2 : tir avec déviation `+d7` (horizontal) et `-d6` (vertical)
- Valeur 3 : tir avec déviation `-d7` et `+d6` ; reset à 0

---

## 9. Logique des IA — aliens normaux

### 9.1 Structures de données alien

La structure de base d'un alien (`alien1_struct`–`alien7_struct`, L5918–) :

```
+0  long : pointeur vers cur_alien_N_dats (bounding box + ptr struct)
+8  word : état (-1 = mort, 0 = actif, ≥1 = spécial)
+10 word : ALIEN_SPEED (vitesse courante)
+12 word : (vitesse résultante ?)
+14 word : ?
+16 word : ?
+18 long : pointeur vers la structure BOB courante
+22 long : pointeur vers lbW012xxx (position écran double)
+26 long : pointeur vers le descripteur de type alien (lbW008F94/lbW009014/etc.)
+30 word/word : ALIEN_POS_X, ALIEN_POS_Y
+34 long : état de suivi (référence vers l'alien cible, pour lbC009A74)
+38 long : pointeur vers l'alien référent (pour les secondaires)
+40 word : index de frame d'animation (54(a0))
+42 word : type alien secondaire (lbB00A24F table de direction)
+44 word : valeur de seuil de proximité
+46 word : alternance animation (flipflop)
+50 word : hit_flag (1 = vient de recevoir des dégâts, montre la frame ALT)
+52 word : mort flag (56(a0) : 1 = alien en état de mort déclenché)
+54 word : type spawn (40(a1) dans lbC00A872)
+56 word : (lbW007B46-like pour alien ?)
+60 word : ALIEN_STRENGTH (points de vie)
+62 word : init flag (1 = alien vient d'être créé)
+64 long : pointeur BOB initial (0(a1) lors de la création)
+72 long : pointeur orbite courante (boss_nbr=4 : index dans lbW0256B4)
+76 word : hatch_timer (20 → 0 : animation d'éclosion)
+78 word : evade_x (timer d'évasion axe X)
+80 word : evade_y (timer d'évasion axe Y)
+82 word : blocked_axis (0=X bloqué, 1=Y bloqué)
+84 word : ?
+88 word : stuck_counter (bloqué depuis N ticks → déclenchement évasion à 25)
```

### 9.2 IA des aliens normaux (`lbC00987E`)

Algorithme de navigation (traduit en C comme `alien_update`) :
1. **Rafraîchissement cible** : toutes les `lbW0097F2` (20) VBL, les positions joueurs cibles sont mises à jour depuis `lbL0097EA` (cache des positions joueurs).
2. **Direction vers la cible** : calcul de la direction 1-8 via `lbC009BC4` (table tangentes `lbB00A24F`).
3. **Tentative de déplacement** : applique ±vitesse sur X et Y selon la direction.
4. **Collision et évasion** : si bloqué :
   - Si bloqué en Y → `evade_x = 50` (inverse la direction X pendant 50 ticks, décrémenté ×2/tick)
   - Si bloqué en X → `evade_y = 50` (inverse la direction Y)
   - `stuck_counter` : incrémenté chaque tick sans mouvement ; à 25 → reset et bascule l'évasion (`lbC009A16–lbC009A60`)
5. **Animation** : alternance de frame selon `46(a0)` (flipflop chaque tick).
6. **Hit flash** : si `50(a0) ≠ 0` → affiche la frame ALT à y=96 pendant un tick, puis clear.

### 9.3 Spawn des aliens

**Depuis la carte** (`lbC0049EA` pour 0x28, `lbC004A18` pour 0x29) :
- Enregistre le point de spawn dans la file `lbL00D29A`
- `lbC00D17E` / `lbC00D1B4` : chaque tick, tente de placer un alien sur chaque point de la file si la position est dans le viewport étendu

**Depuis une tuile hatch (0x0A)** (`tile_facehuggers_hatch`) :
- Sélectionne le descripteur selon `level_flag`
- Appelle `lbC00D22A` → `lbC00D1B4` avec délai de 20 ticks
- Remplace la tuile (`patch_tiles` avec `lbL0200F2` ou `lbL020BF6`)

**Depuis un trou alien (0x34)** (`do_alien_hatch`, L7455) :
- Crée l'alien directement depuis le descripteur courant
- Met `76(a0) = 20` (hatch_timer) → animation zoom-in pendant 20 ticks (frames 0-2 à atlas x=288, y=288/320/352 tant que timer > 12)

---

## 10. Logique des IA — boss

### 10.1 Descripteurs de type boss

| Label ASM     | boss_nbr | Niveau | HP    | Vitesse | AI principale  | Rôle                         |
|---------------|---------|--------|-------|---------|---------------|------------------------------|
| `lbW009014`   | 4       | 10     | 64    | 0 (ASM) | `lbC009AFC`   | Patrouille orbitale (7 aliens)|
| `lbW009114`   | 1       | 5      | 256   | 4       | `lbC009CE2`   | Boss alien queen (primaire)  |
| `lbW009154`   | 1       | 5      | 256   | 4       | `lbC009C68`   | Secondaire boss 1            |
| `lbW009194`   | 1       | 5      | 256   | 4       | `lbC009C68`   | Secondaire boss 1            |
| `lbW009254`   | 2       | 7/8    | 256   | 4       | `lbC009CE2`   | Boss queen/réacteur (primaire)|
| `lbW009294`   | 2       | 7/8    | 512   | 4       | `lbC009C68`   | Secondaire boss 2            |
| `lbW0092D4`   | 2       | 7/8    | 256   | 4       | `lbC009C68`   | Secondaire boss 2            |
| `lbW009314`   | 3       | 12     | 320   | 4       | `lbC009CE2`   | Boss final (primaire)        |
| `lbW009354`   | 3       | 12     | 320   | 5       | `lbC009C68`   | Secondaire boss 3            |
| `lbW009394`   | 3       | 12     | 320   | 5       | `lbC009C68`   | Secondaire boss 3            |

### 10.2 AI du boss principal (`lbC009CE2`, L6811)

Appelée chaque tick pour `alien1_struct` (boss_rank=0) des boss 1, 2, 3.

```
lbC009CE2:
  si lbL008D2E ≠ 0 → set 56(a0) = 1 (mode mort)
  si 56(a0) ≠ 0 et boss_nbr==1 → self_destruct_initiated = 1

  [mode de déplacement — timer de retraite]
  rand(300) → d0
  si lbW009C62 == 0 et d0 < 2 → lbL009C64 = 1, lbW009C62 = 40  (retraite 40 ticks)
  si lbW009C62 > 0 → décrémenter lbW009C62, aller en mode retraite
  sinon [lbW009C62 == 0] :
    lbL009C64 = 0 (poursuite)
    si lbW005D64 ≠ 0 (joueur 1 meurt) ou lbW006504 ≠ 0 (joueur 2 meurt) →
       lbL009C64 = 1 (retraite pendant la mort du joueur)

  [si level_flag == 1024 (niveau 12)] : mettre à jour `lbW099FBA` avec la couleur boss
    + flash blanc quand 50(a0) hit_flag
  [sinon] : mettre à jour `lbW09A212` + flash blanc sur hit

  [calcul du vecteur de déplacement]
  si lbL009C64 == 0 (poursuite) : se déplacer VERS le joueur
    seuil de proximité ±4 px (via lbW00A2FA / lbW00A306 / lbW00A312 / lbW00A31E)
  si lbL009C64 == 1 (retraite) : se déplacer EN SENS INVERSE
    pas de seuil de proximité

  [mise à jour position et BOB]
  → bra lbC009F60 (mise à jour copperlist position) → rts
```

**Tables de hitbox boss** (L7085–L7096) :

| Table         | Signification                                | Valeurs (6 entrées: 3 tailles + 3 offsets) |
|---------------|----------------------------------------------|----------------------------------------------|
| `lbW00A29A`   | Boîte de collision gauche (Left probe)       | `move.w -6(a0)` jusqu'à `+16` → LEFT = nx−54 |
| `lbW00A2A6`   | Boîte collision droite (Right probe)         | RIGHT = nx+52                                 |
| `lbW00A2B2`   | Boîte collision haut (Up probe)              | UP = ny−74                                    |
| `lbW00A2BE`   | Boîte collision bas (Down probe)             | DOWN = ny+48                                  |
| `lbW00A2CA`–`lbW00A2EE` | (valeurs intermédiaires pour les 3 tailles de boss) | — |
| `lbW00A2FA`–`lbW00A31E` | Hitbox boss_nbr=4 (plus grande)   | LEFT=−6, RIGHT=+100, UP=−10, DOWN=+112        |

### 10.3 AI des aliens secondaires boss (`lbC009C68`, L6777)

Alien secondaire (boss_rank=1+) :
- Copie chaque tick les coordonnées de `alien1_struct.ALIEN_POS_X/Y` vers sa propre position.
- Ainsi tous les secondaires suivent le boss primaire exactement.
- Si `52(a0)` ≠ 0 (hit) : met `lbW009C62 = 10` et `lbL009C64 = 1` (retraite forcée du boss primaire).
- Ne déclenche **pas** de self_destruct lors de sa mort.

### 10.4 AI du boss orbital (`lbC009AFC`, L6640) — boss_nbr=4

Boss 4 (niveau 10) : 7 aliens orbitaux qui suivent une trajectoire circulaire prédéfinie.
- `72(a0)` : pointeur courant dans la table de points d'orbite (`lbW0256B4`)
- Si `(a6) < 0` (fin de table) : reset à `68(a0)` (pointeur de début d'orbite)
- Avancement : `addq.l #4, 72(a0)` chaque tick → 4 octets = 1 point (X,Y)
- La position mondiale est mise à jour directement depuis la table

La table d'orbite `lbW0256B4` contient N points (X,Y en mots signés) décrivant une trajectoire circulaire autour du réacteur.

### 10.5 Déclenchement de la mort du boss (`lbC009F62`, L6991)

Appelé quand l'alien primaire (boss_rank=0) meurt. Séquence :
1. `lbW0004EA = 0` (désactive le verrou boss)
2. `lbW0004D8 = 1` (déclenche la cinématique de destruction)
3. Tue les aliens secondaires 2 et 3 (`set_alien_default_vars`)
4. Positionne les aliens 4–7 autour de la position du boss pour l'animation de mort (`lbC00A212`)
5. Selon `boss_nbr` :
   - 1 → `lbC00A056` : ouvre la porte de sortie (`patch_tiles lbL0201C2, lbW062D52`), puis `lbC00A5CC` (FX explosion)
   - 2 → `lbC00A0EE` : restaure les tuiles de sortie (`patch_tiles lbL02087E, lbW060644`), déclenche auto-destruction
   - 3 → `lbC00A1BA` : ouvre 4 portes de sortie, déclenche auto-destruction

### 10.6 Coordonnées de spawn des boss

Dérivées des adresses dans le buffer de carte (voir `level.h` pour la méthode de calcul) :

| boss_nbr | Niveau | Label ptr ASM | IFF (row, col) | Pixels (×16) |
|---------|--------|---------------|----------------|--------------|
| 1       | 5      | `lbW0619E8`   | (49, 104)      | (1664, 784)  |
| 2       | 7/8    | `lbW05F7A8`   | (14, 60)       | (960, 224)   |
| 3       | 12     | `lbW062872`   | (52, 103)      | (1648, 832)  |
| 4 (×7)  | 10     | voir tableau  | voir alien.c   | —            |

Boss 4 — positions des 7 aliens orbitaux :

| Alien | Label ptr ASM | IFF (row, col) |
|-------|---------------|----------------|
| 1     | `lbW06188C`   | (46, 97)       |
| 2     | `lbW061AFC`   | (49, 37)       |
| 3     | `lbW061D6C`   | (51, 101)      |
| 4     | `lbW061FDC`   | (54, 41)       |
| 5     | `lbW06224C`   | (56, 105)      |
| 6     | `lbW06258C`   | (60, 25)       |
| 7     | `lbW0627FC`   | (62, 89)       |

---

## 11. Séquence de destruction et compte à rebours

### 11.1 Déclenchement de la destruction

`self_destruct_initiated` (L1251) est mis à 1 par :
- `tile_start_destruction` (tuile 0x15) si conditions remplies
- `lbC0083DE` (tuile 0x15 + niveau 6 : timer_digit_lo==2)
- `lbC008424` (tuile 0x15 + boss_nbr==4)
- Fin de `lbW002E04` = 0 (niveau 9, auto-destruction)
- `lbW002AC0 ≥ 3` + `lbW002AC2 ≠ 0` (alarme niveau 3)
- `check_reactors` : toutes les 4 faces réacteur détruites (niveau 8)
- `lbC009F62` + boss_nbr 2 ou 3 : mort du boss primaire

### 11.2 Affichage du timer (`set_destruction_timer`, L1257)

```asm
set_destruction_timer:
  self_destruct_initiated = 0
  destruction_sequence_already_initialized = 0
  cur_timer_digit_hi ← timer_digit_hi
  cur_timer_digit_lo ← timer_digit_lo
  → display_timer_digits         ; affiche les chiffres sur le HUD
```

Le timer visible est stocké dans `cur_timer_digit_hi` et `cur_timer_digit_lo` (octets séparés). La valeur en secondes = `hi * 10 + lo`.

### 11.3 Décompte du timer (`lbC003EFC`)

Appelé chaque tick à 25 Hz dans `game_level_loop`. Décrémente `cur_timer_digit_lo` :
- Si `cur_timer_digit_lo > 0` : décrémentation
- Sinon : `cur_timer_digit_lo = 9`, décrémente `cur_timer_digit_hi`
- Si `cur_timer_digit_hi == 0` et `cur_timer_digit_lo == 0` : timer expiré → game over
- Voix de compte à rebours : quand les secondes restantes ≤ 8, joue `VOICE_EIGHT`–`VOICE_ONE`

### 11.4 Cinématique de destruction (`do_level_destruction`, L9155)

Séquence visuelle :
1. Arrêt de la boucle de jeu (`game_running_flag = 0`)
2. Appel de `lbC00DF6A` (stop SFX, init variables explosion)
3. 4 passes de tremblement de caméra (`lbC00DDB8`) :
   - Amplitude 16 → fondu vers blanc → 14 → 12 → 10 → attente fin fondu
   - Chaque passe : décale `map_pos_x/y` de ±d1 pixels et appelle `lbC00DE18` pour un rendu
4. Fondu palette vers blanc (`prep_fade_speeds_fade_to_rgb`)
5. Attente VBL (synchronisation raster via `cmp.b #$FF, CUSTOM+VHPOSR`)
6. Désactivation DMA sprites (`DMAF_SPRITE`)
7. 150 frames d'explosions aléatoires :
   - Toutes les 10 frames : sélectionne un alien struct aléatoire → place à position aléatoire (`lbW013308` = struct pré-allouée)
   - Toutes les 4 aliens placés : joue sample 10 ou 11 (`SAMPLE_EXPLOSION_A` / `SAMPLE_REACTOR_BLAST`)
8. Fondu palette vers noir
9. Attente fin de fondu (`tst.w done_fade`)
10. Réactivation DMA sprites

Le mécanisme d'animation d'explosion utilise la liste `lbL00DF36` (pointeurs vers des structures alien pré-allouées `lbW012B44`, `lbW012C60`, etc.).

### 11.5 Variables de la cinématique

| Label ASM    | Nom C suggéré                   | Rôle                                                          |
|--------------|---------------------------------|---------------------------------------------------------------|
| `lbW00DC72`  | `s_destruct_sound_playing`      | 1 = son de destruction en cours (structure sample `lbW0231BA`) |
| `lbW00DC74`  | `s_explosion_alien_counter`     | Compteur d'aliens d'explosion placés (déclenche son à 4)     |
| `lbW00DF2E`  | `s_explosion_random_pos`        | 1 = mode explosion aléatoire actif                           |
| `lbW00DF30`  | `s_explosion_timer`             | Compte à rebours entre deux placements d'explosion (10 frames)|
| `lbL00DF32`  | `s_explosion_alien_list_ptr`    | Pointeur courant dans `lbL00DF36` (liste des structs aliens d'explosion) |
| `lbL00DF36`  | `s_explosion_alien_structs[]`   | Liste de pointeurs vers les structures alien utilisées pour les explosions (lbW012B44, lbW012C60, lbW012D7C, ...) |
| `lbL01FDAA`  | `s_explosion_duration`          | Durée de la cinématique (500 frames pour niveaux 3/12, 32000 pour boss_nbr=4) |

---

## 12. Labels C non utilisés directement — analyse de labels ASM non nommés

### 12.1 Offsets player non documentés dans constants.h

| Offset joueur | Contenu / Rôle identifié                                                       |
|--------------|--------------------------------------------------------------------------------|
| 270          | `s_penetrating_flag` = flag pénétrant de l'arme courante (= Mot 4 de weapons_attr_table) |
| 274          | `s_player_dying_flag` (tst.w 274(a0)) : 0 = vivant, ≠ 0 = en animation de mort |
| 278          | Réinitialisé à 0 dans `init_player_dats` (clr.w 278(a0))                      |
| 280          | `s_death_counter_flag` (tst.w 280(a0) dans tile_hard_climb_right) : 0 = vivant pour la vérification de mur |
| 284          | Réinitialisé à 0 dans init_player_dats                                         |
| 292          | Réinitialisé à 0 dans init_player_dats                                         |
| 328          | `s_invincibility_flag` (tst.w 328(a0) dans tile_one_deadly_way_* : 0 = vulnérable à la mort instantanée) |
| 368          | Réinitialisé à 0 dans init_player_dats                                         |
| 372          | `anim_flipflop` réinitialisé à 0 dans init_players_variables                   |
| 420          | `s_low_ammo_warned` (mis à 1 par le code de détection munitions faibles) — cf. lbW02328A check |
| 424          | Réinitialisé à 0 dans init_player_dats                                         |

### 12.2 Variables non documentées, identifiées par le contexte

| Label ASM       | Nom C suggéré              | Rôle déduit du contexte                                                         |
|-----------------|----------------------------|---------------------------------------------------------------------------------|
| `lbW007B16`     | `s_p1_move_left_ptr`       | Pointeur vers le code/état de déplacement gauche du joueur 1                    |
| `lbW007B22`     | `s_p1_move_right_ptr`      | Idem à droite (comparé dans `tile_one_deadly_way_*` pour détecter la direction) |
| `lbL008CCE`     | `s_boss_hit_sound_flag`    | Mis à 1 quand un boss secondaire est touché (force le son de hit boss)          |
| `lbL008D2E`     | `s_boss_death_trigger`     | ≠ 0 = forcer la mort du boss primaire (set 56(a0)=1 dans lbC009CE2)            |
| `lbW0039B4`     | `s_p2_is_shooting`         | ≠ 0 = le joueur 2 tire (booste la vitesse FLAMETHROWER de +4)                  |
| `lbW0097F2`     | `s_alien_target_refresh_rate` | Intervalle de rafraîchissement de la cible alien (20 ticks). Copié depuis `42(a1)` lors du spawn |
| `lbL0097EA`     | `s_cached_player_positions` | Cache des positions joueurs mis à jour toutes les 20 VBL                        |
| `lbW002FE0`     | `s_destruct_seq_done`      | Mis à 1 quand la séquence de destruction a déjà été exécutée                   |
| `lbW023E9C`     | `s_palette_fade_duration`  | Durée restante du fondu de palette (décrémentée dans le IRQ niveau 3)          |
| `lbW0113B8`     | `s_double_buffer_flag`     | Contrôle du mode double-buffer du fond (négatif = activer double-buffer)        |
| `lbW023204`     | `s_sample_channel_select`  | Index du canal audio choisi pour le sample de destruction                       |
| `lbW0231BA`     | `s_destruction_sample_struct` | Structure sample pour le son continu de destruction                           |
| `lbW099FBA`     | `s_boss3_palette_color`    | Registre de couleur boss niveau 12 (modifié dynamiquement lors des hits)       |
| `lbW09A212`     | `s_boss12_palette_color`   | Registre de couleur boss niveaux 1-3 (flash blanc sur hit)                     |
| `lbW003644`     | `s_render_dirty_flag`      | Effacé avant le rendu, marque les tiles à redessiner                           |
| `lbB00AC9E+2`   | `s_patch_queue_count`      | Compteur d'entrées actives dans la queue de patch de tuiles (max 32)           |
| `lbW00ACE2`     | `s_patch_pending_flag`     | ≠ 0 = une demande de patch est en attente de traitement                        |
| `lbW012A6A`     | `s_patch_null_sentinel`    | Sentinelle : si `a3 == lbW012A6A`, ignorer le patch (tuile déjà effacée)      |
| `lbL004BE4`–`lbL004C00` | `s_fire_door_states[8]` | États des 8 portes coupe-feu (longs, effacés à l'init des niveaux 6/7)      |
| `lbW005D64`     | `g_players[0].death_counter` | Identifié comme compteur de mort P1 (vérifié dans lbC009D80 pour la retraite boss) |
| `lbW006504`     | `g_players[1].death_counter` | Identifié comme compteur de mort P2                                           |

### 12.3 Données de patch de tuiles (tables `lbL0201xx`–`lbL020Bxx`)

Ces tables contiennent les données de remplacement de tuile utilisées par `patch_tiles`. Format : liste de couples (attribut_destination, index_graphique) terminée par un mot spécial.

| Label ASM          | Usage                                                                     |
|--------------------|---------------------------------------------------------------------------|
| `lbL020196`        | Patch de fermeture de la porte d'entrée boss_nbr=1 (tuile wall)          |
| `lbL0201C2`        | Patch d'ouverture de la sortie boss_nbr=1 (remplace mur par exit)        |
| `lbL0201EE`        | Patch déclenché par tuile 0x15 (self-destruct trigger → floor)           |
| `lbL0200F2`        | Patch tuile hatch non-level_12 (0x0A → floor)                            |
| `lbL020BF6`        | Patch tuile hatch level_12 (0x0A → floor, variante)                      |
| `lbL020B2E`        | Patch fermeture porte boss_nbr=3 (×2 appels)                             |
| `lbL020B0A`        | Patch second groupe de portes boss_nbr=3                                  |
| `lbL020B52`/`86`   | Patches d'ouverture sortie boss_nbr=3                                     |
| `lbL02087E`        | Patch ouverture sortie boss_nbr=2 (@ lbW060644)                          |
| `patch_dat_reactors` | Données de remplacement d'une face réacteur détruite (floor graphique) |
| `lbL020D92`        | Tuile centrale du bouton fire-door (patch)                               |
| `lbL020DE2`/`DBE`  | Tuiles panel fire-door (côtés)                                            |
| `lbL020E3A`        | Tuile panel fire-door (version normale)                                  |
| `lbL020E5E`/`0E`   | Tuile panel fire-door (version avec alarme)                              |
| `lbL020F82`        | Patch crédits 1000 (enlève la tuile crédits)                             |
| `map_reactor_up/left/down/right` | Adresses des faces du réacteur dans la carte (L7MA, niveau 8) |

---

## Annexe A — Récapitulatif des noms à attribuer dans main.asm

Les labels suivants restent non nommés dans l'assembleur et méritent un renommage :

```
; === Variables d'état de la boucle de jeu ===
lbW0004BA  → game_tick_ready          ; flag tick 25 Hz
lbW0004BC  → vbl_sub_counter          ; compteur VBL (0-1) pour générer game_tick_ready
lbW0004B2  → render_ready_flag        ; mis à 1 en fin de boucle
lbW0004D8  → self_destruct_trigger    ; 1 = lancer cinématique destruction
lbW0004EA  → boss_active_flag         ; 1 = boss en cours (verrou anti-double)
lbW0004EE  → boss4_active_flag        ; 1 = boss_nbr=4 activé (niveau 10)
lbW0004E6  → fire_door_activated      ; 1 = portes coupe-feu activées (niveaux 3/12)
lbW0004E0  → (à identifier)
lbW0004E2  → (à identifier)
lbW0004E4  → (à identifier)
lbW0003A2  → music_master_disable     ; 1 = désactiver mode musical (bypass game-over via fire)

; === État des aliens ===
lbW009C62  → boss_retreat_countdown
lbL009C64  → boss_retreat_flag
lbW009CDE  → boss_anim_counter
lbW009CE0  → boss_roar_trigger
lbL00D226  → next_alien_spawn_struct_ptr
lbL00D29A  → alien_spawn_queue_an
lbL00D2AA  → alien_spawn_queue_bo
lbW0097F2  → alien_target_refresh_rate

; === Niveau 9 ===
lbW002E04  → lvl9_auto_destruct_counter

; === Alarme niveau 3 ===
lbW002AC0  → alarm_buttons_pressed
lbW002AC2  → alarm_system_active

; === Cinématique de destruction ===
lbW00DC72  → destruct_sound_playing
lbW00DC74  → explosion_alien_counter
lbW00DF2E  → explosion_random_pos_active
lbW00DF30  → explosion_timer
lbL00DF32  → explosion_alien_list_ptr
lbL00DF36  → explosion_alien_structs_list
lbL01FDAA  → explosion_duration

; === Rendu / copper ===
lbW0113B8  → double_buffer_ctrl_flag
lbW023E9C  → palette_fade_timer_ctr
lbW099FBA  → boss3_hit_palette_color
lbW09A212  → boss12_hit_palette_color

; === Patch de tuiles ===
lbW00ACE2  → patch_pending_flag
lbB00AC9E  → patch_queue_count_and_base
lbW012A6A  → patch_null_sentinel

; === Portes coupe-feu ===
lbL004BE4 … lbL004C00  → fire_door_state_0 … fire_door_state_7

; === Noms de tuile inconnus ===
tile_unknown5  → tile_zone_1_or_3_boundary
tile_unknown6  → tile_zone_2_or_4_boundary
tile_unknown7  → tile_zone_5_boundary
; tile_force_fields_sequence est déjà nommé → tile_zone_6_trigger

; === Offsets non documentés dans la struct joueur ===
; 270(a0)  → player_weapon_penetrating
; 274(a0)  → player_dying_flag
; 280(a0)  → player_death_wall_flag
; 328(a0)  → player_invincibility_flag
; 420(a0)  → player_low_ammo_warned
```

---

*Document généré lors de l'implémentation du portage C d'Alien Breed SE 92.*
*Sources : `src/main/main.asm`, `src/c/game/*.c`, `src/c/game/*.h`*

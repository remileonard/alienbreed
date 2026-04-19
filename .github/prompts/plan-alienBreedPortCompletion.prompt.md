# Plan: Alien Breed Port — Game Logic Completion

## TL;DR

Le portage est structurellement complet mais manque : la logique tile interactions (critique), la variété aliens, le boss AI, le système crédits/scoring, les écrans INTEX, la séquence destruction, la progression difficulté par niveaux, et les effets visuels hardware (copper list → SDL2).

---

## Phase 1 — Système d'interaction tiles (CRITIQUE, tout le reste dépend de ceci)

**1.1** Constantes tiles dans `src/c/game/constants.h` : TILE_EXIT (0x02), TILE_DOOR (0x03), TILE_KEY (0x04), TILE_FIRST_AID (0x05), TILE_AMMO (0x06), TILE_EXTRA_LIFE (0x07), TILE_CREDITS_100 (0x0B), TILE_CREDITS_1000 (0x0C), TILE_ONEWAY_* (0x0D-0x11), TILE_DEADLY_HOLE (0x13), TILE_DESTRUCT_TRIGGER (0x15), TILE_ACID_POOL (0x16), TILE_INTEX (0x17), TILE_BOSS_TRIGGER (0x3D) — Ref: main.asm#L5059

**1.2** `check_tile_interaction(player)` dans `src/c/game/player.c`, appelée dans `player_update()` après chaque mouvement validé. Switch sur type tile :
- Pickups (key, health, ammo, life, credits) → collecter + `tilemap_replace_tile()` vers floor
- TILE_EXIT → si `exit_unlocked || in_destruction` → `level_trigger_end()`
- TILE_DOOR → si `player.keys > 0` → `open_door()`
- TILE_INTEX → `intex_run()`
- TILE_DESTRUCT_TRIGGER → `level_start_destruction()` + tile → floor
- TILE_ACID_POOL → -1 HP toutes les 25 frames
- TILE_DEADLY_HOLE → mort instantanée
- TILE_BOSS_TRIGGER → `boss_trigger()` + tile → floor

**1.3** `tilemap_replace_tile(x, y)` dans `src/c/engine/tilemap.c` : masque `tile &= 0xFFC0` — Ref: open_door @ main.asm#L5242

**1.4** `open_door()` dans `src/c/game/player.c` : scan 4 tiles adjacentes + patch, `player.keys--`, `level.doors_opened++`, play sample 23 — Ref: main.asm#L5215

**1.5** One-way doors dans `try_move()` : bloquer si direction opposée à TILE_ONEWAY_*

**1.6** `exit_unlocked` par niveau dans `level_run()` (level.c) : niveaux 1,4,6,8,9,11 → flag=1 ; niveaux 2,3,5,7,10,12 → flag=0 — Ref: main.asm#L1023-L1219

---

## Phase 2 — Variété aliens & kill tracking (parallèle avec Phase 3)

**2.1** Tableau `alien_type_table[7]` dans `src/c/game/alien.c` : HP base 100,132,164,196,228,260,292 — Ref: main.asm#L5918-L5967

**2.2** Sélection type alien dans `alien_spawn_from_map()` selon `level_number`

**2.3** `aliens_killed` dans structure Player, incrémenter dans `alien_kill()`

**2.4** Corriger sample mort alien dans `alien_kill()` (actuellement joue HATCHING_ALIEN par erreur)

---

## Phase 3 — Boss AI (parallèle avec Phase 2, dépend Phase 1 pour trigger)

**3.1** Créer `src/c/game/boss.c` avec `boss_trigger(level_number)` :
- Boss 1-3 (alien unique) : speed fixe max, HP élevés
- Boss 4 (niveau 6) : spawn 5 aliens simultanément
- Musique boss, VOICE_DANGER (sample 61), patch tiles portes boss
- Ref: main.asm#L5664-L5767

**3.2** Déverrouiller exit (`exit_unlocked = 1`) quand `alien_living_count() == 0` post-boss trigger

---

## Phase 4 — Crédits, scoring, progression difficulté (parallèle avec Phases 2+3)

**4.1** Crédits de départ variables par niveau dans `level_run()` :
- Niveau 1: 0 | Niveau 2: +500,000 | Niveau 4: +1,000,000 | Niveau 6: +1,500,000 | Niveau 8: +2,000,000 | Niveau 10: +2,500,000
- Ref: main.asm#L8347-L8396

**4.2** `global_aliens_extra_strength` dans `level_run()` :
- Niveaux 1-8: 0 | Niveau 9: 5 | Niveau 10: 10 | Niveau 11: 15 | Niveau 12: 20
- Appliquer dans `alien_spawn_from_map()` : `alien.strength = base + extra_strength`
- Ref: main.asm#L429-L465

**4.3** Bonus fin de niveau +30,000 pts dans `level_trigger_end()` — Ref: main.asm#L10191

**4.4** Partage crédits 2-joueurs dans `intex_run()` — Ref: main.asm#L9015

---

## Phase 5 — Écrans INTEX complets (dépend Phase 4)

**5.1** `draw_screen_supplies()` dans `src/c/game/intex.c` — prix exacts (valeurs ÷50 de intex.asm) :

| Item | Prix |
|------|------|
| MAP OVERVIEW | 10,000 CR |
| AMMO CHARGE | 2,000 CR |
| NRG INJECT | 5,000 CR |
| KEY PACK | 5,000 CR |
| EXTRA LIFE | 30,000 CR |

**5.2** `draw_screen_map()` — minimap de la zone

**5.3** Compléter `draw_screen_stats()` avec kills (Phase 2)

**5.4** Logique achats armes avec prix : 200/480/700/960/1,200/1,500 CR (armes 2-7, arme 1 = de base) — Ref: intex.asm#L1022-L1028

---

## Phase 6 — Séquence destruction (dépend Phase 1)

**6.1** `level_start_destruction()` dans `level.c` : flag `self_destruct_initiated`, VOICE_DESTRUCTION_IMMINENT — Ref: main.asm#L1257

**6.2** `level_tick_timer()` : sample toutes les 25 frames, warning bip à 0:01, basculement palette vers `level_palette2` — Ref: main.asm#L1275

---

## Phase 7 — Effets visuels hardware (copper → SDL2)

> Remplacement des effets copper list Amiga par équivalents SDL2 dans le framebuffer indexé 320×256.

**7.1 Split-screen palette HUD** dans `src/c/hal/video.c` + `src/c/game/hud.c` :
- Original : copper change COLOR01/02/03 aux scanlines correspondant aux barres HUD (~lignes 43-50 PAL et miroir bas)
- SDL2 : réserver des index de palette dédiés HUD (ex: indices 28-31), distincts des 27 couleurs tileset
- `hud_render()` utilise ces index → couleurs correctes indépendantes du tileset
- Ref: copper list @ main.asm#L18427-L18612

**7.2 Color cycling HUD** dans `hud_render()` ou via `palette_tick()` :
- Original anime COLOR01 ($111→$222→$333→$444) et COLOR02 ($444→$888→$CCC) en 4 pas cycliques
- SDL2 : varier les entrées palette HUD (indices 28-31) via `hud_cycle_counter % 4` chaque frame
- Appel `video_set_palette_entry()` dans le game loop

**7.3 Damage flash** dans `src/c/game/player.c` + `src/c/hal/video.c` :
- Ajouter `damage_flash_counter` à structure Player (200 frames)
- Quand counter > 0 : écraser temporairement index couleur joueur avec blanc ($FFF = RGB 255,255,255) pour 1 frame puis restaurer
- Décrémenter counter dans `player_update()`
- Ref: main.asm#L6847-L6859, init counter @ main.asm#L3934

---

## Phase 8 — Finitions

**8.1** Animation sprites dans `src/c/engine/sprite.c` : mapper `anim_counter` aux frames par direction/type alien

**8.2** HUD crédits affichage dans `src/c/game/hud.c`

---

## Fichiers clés

- `src/c/game/constants.h` — IDs tiles
- `src/c/game/player.c` / `player.h` — tile interaction, open_door, one-way, damage_flash_counter
- `src/c/engine/tilemap.c` / `tilemap.h` — `tilemap_replace_tile()`
- `src/c/game/alien.c` / `alien.h` — 7 types, kill tracking, extra_strength
- `src/c/game/level.c` / `level.h` — exit_unlocked, destruction, bonus, crédits par niveau
- `src/c/game/intex.c` — 3 écrans manquants, logique achats
- `src/c/game/boss.c` (nouveau) — logique boss
- `src/c/game/hud.c` / `src/c/hal/video.c` — split-screen palette, color cycling
- ASM ref: `src/main/main.asm`, `src/intex/intex.asm`

---

## Vérification

1. Marcher sur chaque type de tile → réaction correcte (pickups, portes, sortie, INTEX, acid, trou mortel)
2. One-way doors bloquent dans un sens, portes s'ouvrent avec clés
3. Déclencher destruction → timer, palette change, sortie possible, game over à 0:00
4. Boss level → trigger, mort boss → exit déverrouillée
5. INTEX → acheter fournitures/armes → crédits débités, items reçus
6. Fin niveau → +30,000 pts, progression
7. Niveaux 9-12 : aliens plus résistants (extra_strength visible)
8. Crédits de départ augmentent aux niveaux 2, 4, 6, 8, 10
9. Barres HUD ont couleurs distinctes du tileset (split-screen palette)
10. Flash blanc visible quand joueur prend des dégâts

---

## Décisions

- **Difficulté** : pas de menu, progression originale par niveaux uniquement
- **exit_unlocked niveaux verrouillés** : boss kills → déverrouillage. Réacteurs (0x2A-0x2D) à investiguer lors de l'implémentation
- **Hors scope** : copper list complète (transitions, overmap), logique Amiga hardware bas niveau non visible par le joueur

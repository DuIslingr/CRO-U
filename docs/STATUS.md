# CRO-U status in detail

What the port does for Omega so far, and from which part of the original - the README has the
summary. Everything here was checked against Omega's own binary (the iPad's 1.0.0 symbols, 3.1.0
and the Vita build by matched names) and, where it shows, the reference playthrough. What isn't
listed is CR1:U's behaviour still (`docs/ENGINE.md` is CR1's notes).

- [x] Omega's tables from its own binary (`tools/gen_tables.py`, by symbol): 11 areas, 215 maps,
      379 NPC models, 429 images, 250 action files, 35 battle stages, 166 battle models, the menu
      pictures (groups 0 and 1), plate icons, treasure, footsteps
- [x] images load as `chk_hd_name` does: `<base>_ipad.mct` on the iPad, else `<base>_hd.mct`
      (the UI sheet is `window_ipad`)
- [x] the title screen as `__menuu_TitlePut` draws it (logo, Please Touch, copyright, the two
      pictures down the left, the plates)
- [x] the menus' coordinates: Omega's iPad build is the iPhone layout doubled and centred
      (`tex2d_add`: screen = 2 x + (32, 64)); menu sounds are CR1's + 0x91
- [x] Select Scenario (`__menuu_Start*`: the scenario's art and banner, one plate, Back) and the
      title's save / load screen over it
- [x] the opening as the reference video plays it: painted rooms' `FrmPanCam` moves from
      `GetScroll`'s unclamped value, camera-wise (up = + y); the leader's model as `Set_Prtyno`
      (f009a / f009b, f010, f011)
- [x] rooms' paintings and overlays as their stage records name them (+60 / +62 of the record)
- [x] scene effects as Omega's `ScnEffectRead` / `Proc`: eleven sets, effects 2..30 (weather, the
      elevator shaft, the space backdrop, the battleship's sky behind its painting) and effect 1
      (syuutyuusen's concentration lines, every other frame); d03's fireflies and d06's smoke
      blend additively
- [x] a new game reaches the opening; with Omega's data the self-test loads all 373 models and
      spawns and walks the player on all 125 fields
- [x] the party as Omega keeps it (`_prty`): ten records (the scenario's three in play, a guest,
      each scenario's block), two scenarios with their own pair, leader, bag and Oz;
      `prty_init` / `prty_init_new`, `__prty_chg`, Omega's `_pcparam`, starting gear and 255-level
      `__exp`
- [x] battles on Omega's data: its 220-byte monster rows (growth, model scale, AI lists, skills,
      counters, drops, steals, genes, plates by species from `_btlanmtbl`), the pair and story
      line-ups of `btl_prty_init`, the party's models and names, cameras on the actors' node 1003;
      the opening's deck battle (group 737) frames as the reference does
- [x] the Camp: main screen (five plates, the Scenarios / Collection buttons, the scenario's
      boards), Omega's eight item categories, the Status page (portrait and state board), the
      equipment's seven slots (the Human Genes slot takes the characters' own plates); the shop
      (Omega's levels, stock and sell lists); the World Select (each scenario's worlds, bands -
      which set the scenario's level byte for enemies and treasure -, the skip question)
- [x] Omega's genes (plates 474..504, skills to 198, `_geneplate`), its 38-byte battle actions,
      the story battles' talks and HP floors (`chk_karaok_*`), the defeat warp, the encounter
      level, battle voices' speakers (3.1.0 / Vita), the talk window's parchment (`window_cutin`)
- [x] native calls 2036..2047 (SetPrtyNo, SetPrtyLv_Add, TBox, SaveEvent, Chk / Chg_BtlFld,
      Chg_MsgSkip, Chg_char, Add_OMU_ITEM, NpcShadow_onoff, puzzle_teleport_cnt, EquipAcce) and
      2015 FireWall
- [x] puzzle rooms' stages (16, maps 153..168) from Omega's `PUZZLE_ROOM_DATA`, played through in
      the self-test on Omega's own rooms (teleport, psychokinesis, toughness, pyrokinesis: each to
      its crystal or its gem; the banner on flag 3090); `--puzzle-dump` lists every stage's objects
- [x] the story plays by itself (`--explore`): the first scenario from the opening through all six
      worlds (100 rooms, 22 battles, the puzzle rooms taken as solved) to its ending and staff roll;
      the second (`--explore n 1`) through its seven worlds to its ending (flag 32) - the Scenarios
      cards it fills from the Camp give the points the last world needs (var 0x16, the presents a
      full collection would hold taken as found once it's out of ideas)
- [x] the Scenarios screen as Omega's bingo cards (`__menuu_Sccnarios*`): the faces and both
      Achievement rates, the cards and their pictures, cells opened from the Camp with presents;
      the Collection (`__menuu_Collection*`: weapons, armor and jewels seen, the rate's rank)
- [x] Select Scenario's second page (flag 31): pulled down by its cord, falling in with its bounce,
      the music changing with the page
- [x] worn equipment stays in the bag, as Omega's `prty_init` / `__ChangeEquipItem` have it
- [x] the bosses' staged actions (`btlact_main`'s kinds 10..22: 430..443, 509) with Omega's
      effects, sounds and camera chains, and the story battles' set moves (`_btlque_special`);
      groups 0x291 / 0x22c's extra model (`read_btlcpl`: m127 at (500, 0, 465), its slot 14 in 440)
- [x] Omega's own battle calcs (38..44: the kiss, Down and the Ward, 437's blow, MP / HP x power),
      its stat formula (`btlpcparam_*` with Down / the Ward) and warrior / sorcerer by character
- [x] a boss's end (`btl_main` 1000 / 0x3de .. 0x3e6): the last boss to fall stays standing
      (`btlcalc_chk_dead`: down for good when size 1 or in groups 0x292 / 0x2a2; gone at once with
      effect 7 and sound 0x42 while others stand); won in any formation but 2, the camera turns on
      it (0x34 / 0x35 / 0x67 past the formation's), the fallen play their down motion, the pair goes
      out of sight; models 0xd .. 0x12, 0, 0x1b, 0x1d .. 0x20 go under a quake and a white-out at
      their own frames, the others play the motion out (sound 0xc6 at `_btlanmtbl` [16]) and fade.
      The story flags it keeps: 0x596 (group 0x294's boss down), 0x791 (0x2a5's), 0x644 (0x295
      won). The victory: "Victory!" (57007) for 60 frames, the camera on the pair (0x47 / 0x45 + the
      one standing), its tune only when not the battle's own nor 997 / 998 (var 59 under 0x26). A
      defeat takes the monsters still standing out of sight. `--battle-view bossdown@n`
- [x] sounds fading as `__effgo`'s flags have them (bits 26 / 27 / 25: the one playing out over 10 /
      20 / 40 frames, `__eff_vol2`; 18 / 19 / 17 in from nothing, `__eff_vol_fadein`) - the scripts'
      ~820 fade-outs started a second copy before; the shop's close fades the room's loops back in
      (0x113 with flag 0x472, 0x3 with 0x473); group 0x294's rounds 1 .. 10 (`btl_main` 800): a
      line, the quake dying away and the rumble (0x11) fading, the 10th's held
- [x] the battle's sounds as Omega plays them (`__effgo`): each attacker's swing
      (`btlact_attack_init` by model, `_btlanmtbl` [15] for the species), the bosses' blows and
      casts at their frames, a gene's / item's start (0x37 / 0x38), a fall (0x41), and every calc's
      own a target at a time (the hits and their elements, 0x47 / 0x48 + 2 x a gene's element,
      fixed damage, heals, revives, status and stat changes, 0429 .. 0435's)
- [x] level-ups as `pcparam_change_lv` has them (from 20: 10 HP and 1 to each other stat a level;
      the level-up genes 30 HP / 3 MP / 5), seeds 10 a seed to 9999 (`use_item`,
      `MuCom_GetUsedItemLimitNum`); group 695's guest takes orders and leaves the enemies' levels to
      Olgar's; the save screen's load flags (31 / 32) and clear data; gold boxes (`Uty_ChkItem`, key 2)
- [x] Omega's text at its own size (PutMsg's lines doubled with the rest of its tex2d; names at
      about three quarters), the item band (`Uty_GetItem_*`), SD full-screen cards (the second
      scenario's notice), Select Scenario's card (flag 0x643)
- [x] the Vita, as an add-on (`--vita`) or alone (`--data <PCSG00499>`): its scripts and eight
      languages, voices (bank order: `_no_silent` then the clips by name) and music, all of its
      textures (P8 with an ARGB palette for the effects), its boot logos; alone it runs as the Vita
      does - the iPhone's Retina path (`_is_iPadDisplay` 0, `_is_RetinaDisplay` 1, the `_hd` sheets)
      on its 960 x 544 screen (Layout "Vita": the 2D at 0.85, the paintings, script pictures and
      portraits at full size, the battle over the whole screen)
- [x] the self-test runs clean on Omega's data (its checks moved onto Omega's rooms, groups,
      scripts and tables)
- [x] the worlds' treasure boxes as Omega's TreasureBoxManager keeps them: drawn again each time
      the World Select leads into one of the first four (`__menuu_WldChoiceClr` sets gac +0x364
      bit 0x40000000, `fldread_evt_init2` -> `_InitDungeon` -> `randomSelect`), the draw kept with
      the scenario in the save, a box taken there (`setOpened`: its item -1); with the band at 2 or
      more the items from pool slots 9..14 go into the boxes' gold counterparts (`AddInfoList`:
      box 100..139 + 700, 140..179 - 80)
- [x] the present packs (flags 101..106) open, as 3.1.0's and the Vita's `_gameapp_prc` set them
      every frame: the second scenario's five trials and their presents after its ending
- [x] Reset Data (the title's Options): asked twice (57078, 57079), Yes to both removes the saves
      (`OptionPrc` 9 / 10: `prty_init_all`, `removeSaveData`, `removeContData`)
- [x] 3.1.0's iPad data runs as the iPad's (its `_ipad` art, the iPad layout) with its voices
- [x] a table NPC below 400 can stand at a room's origin (c101_d07's 142, map 115): only
      NpcMdlRead's story actors (400 on) wait there hidden
- [x] Options' header is Omega's picture 103 (`_stMuPictInfo_Option`)
- [x] `--explore n 1` finds all 80 presents itself (battles won with their EXP, Oz and drops, keys
      bought for the gold boxes, the World Select's worlds taken in turn); `CR_EXPLORE_NO_GIFTS`
      stops instead of taking missing presents as found
- [x] the save prompt in every layout as `__menuu_SaveLoadPut` draws it - the message board at
      (0, 220) and the Yes / No plates (`ComUty_MsgBoardDraw`, `ComUty_YesNoChoiceDraw`); the iPad
      layout used CR1's own iPad panel and buttons
- [x] the Camp's equipment slots as `__MuCampScrListEquipInit` lists them: a scrolling list of seven
      (an empty jewel / gene slot showing its placeholder item, 0x1c6..0x1ca) in four rows of one
      line with the list's scroll bar, and no section bar under the "Equip" title
      (`__MuEquip_CategoryChoiceInit`: the title and the character's name) - it was seven rows at
      70% with the slots' names under them

- [x] the field menu as `__menuf_mainPrc` / `Put` have it: the Map button only in areas 2..5 and 8
      (mask 0x13c), and the battle button at the top left (`__GetBattleDispCondision`: skill 191
      known, the room's encounters there, flag 4076 clear) that starts a fight
      (`_fldencount_encount_debug`: the walked distance 1000, the chance 105; G here)
- [x] the map screen from Omega's `__menuu_MapSet` / `Put` / `Prc` and `ReadMapMenu`: map_D01..D04
      and map_D07 by area, the pair's marker on Omega's 150 spots (`_gkashMapCharPosTbl`) fading in
      and out (3 / 6 / 11 / 6 frames), the story's two markers on D01..D04, D03's two and D04's five
      covers until their flags, and D07's 58 rooms on three pages a tap turns (the warps, the doors
      and bosses with the count, the items with theirs - `_gkastMapIcon`, `_gkashWarpTbl`, 57111 /
      57112 with vars 0..3); it was CR1's, by map number, for the first 64 maps only
- [x] the battle rules' remaining differences from Omega's `btlcalc_*` / `btlque_make_que`:
      monsters' resistance 1 makes them immune (it halved) and an attacker without an element
      meets no element resistance (it counted as element 1), as `_btlcalc_zoku_0203` has it; a
      gene's resistance applies even at 0 damage (0406); the dodge skills (131 blows, 136 genes)
      roll once a target as the action is queued, one success fending off the whole action, and
      the actions with +20 set can't be dodged as genes; Omega's skills 187..198 count in battle -
      187 / 188 a 5% kill on a weak hit, 189 a 5% dodge of either kind that answers with a counter
      (actions with +24 clear), 190 a quarter of HP back at a round's end (5%); the round's HP
      changes added up and applied once (`_btlcalc_status_prc`)
- [x] turn order as Omega's `_btlord_calc_spd` has it: the invincible status no longer acts
      first (CR1's +19998); skill 153's guard also rolled as each round starts
      (`_btlord_enemy_rand`), and group 0x297's two enemies never act as a pair; the AI's lists,
      weights and targets checked against `_btlai_make` / `_btlai_prc` (unchanged)
- [x] the barrier (calc 26) reflects, as `_btlact_gene` / `_btlact_item`'s states 0x1004 / 0x100e
      have it (CR1's too): a hostile gene or item meeting one on any target takes every target's
      barrier and comes back on its users (scope 5: on all of their side), the barriers lighting up
      (effect 0x3a, sound 0x5b) as the users stop, and 40 frames on the barrier's side sends it back on
      them (the camera on each in turn); the Break counts it for the barrier's side and what it fells
      leaves no item or gene (+0xaf) - it was absorbed. A dodge shows its line once (57061
      physical / 57062 genes, 0404 / 0406 / 0410 / 0412 / 0413), which the barrier had borrowed
- [x] the battle results as `__menuu_MbResultSet` / `Prc` / `Put` run them: the windows sliding in
      (the boards from the left, the takings from the right, the message board from below), the
      boards' EXP climbing for (most levels + 1) x 60 frames with the level-up chime and HP filling,
      EXP Gained / OZ Earned / the purse counting up a sixtieth a frame with the tick, a tap to
      finish it; then "Victory!" gives way, a tap at a time, to each member's level-up line (57008 /
      57034) and each item's (57010 / 57035, grouped with how many), and the windows go. A gene
      learned is said in the battle as it's learned (0506's 57049 / 57050) - it was the port's own
      static screen with every line at once and an English "Level up!"
- [x] Options checked against `__menuu_OptionSet` / `Put` / `Prc`: the same settings as CR1's
      (Instruction Manual, BGM, Sound Effects, Language, Environment Effects - the same effects held
      still, `__GetScreenEffectState` -, Lock Screen Orientation, Return to Title / Reset Data); the
      first Options of a game open on 56040 "About Option" (flag 0x43)
- [x] the Instruction Manual as OptionPrc 0xf..0x16 has it: its topics are the manual items held
      (419..473, given as the story goes), by name, each one's page 56000 + (item - 419); a page's
      centred lines (code 0x17) centred, and the header Omega's picture 103 - it listed CR1's fixed
      topics with an introduction and showed 102 ("Sold Out!")
- [x] the staff roll as Omega's `__menuu_StaffRollSet` / `Prc` / `Put` run it: EndBack01 / 02 /
      01_end's picture 12 behind 17 lines of block 9 (20 apart, x 16, from y 352 - the iPhone 320 -,
      316 in all) under a white (first scenario) or black fade clearing over 60 frames; lines 2 a
      frame (the second scenario 1.413) and the picture 0.2 (640 / 4600), the corner button (115)
      held 10 x; then 100 + 120 frames, mode 0's end picture (EndBack01_end's 14 at (176, 128)) in at
      6 a frame, a 120-frame fade to black and the windows go - no tap. It was CR1's (the black
      panel sliding away, 21 lines from y 800, the end card waiting for a tap)
- [x] the message codes checked against Omega's `_PrcMsg` (CR1's too): 0x10 clears the window and
      types on from its first line - after a tap (0x12) that is the next page, which had kept an
      empty first line, and after a timed pause (0x1a) the page turns by itself once the pause is
      out (flag 4075, the page-turn cue, only for a tap's turn, as 0xfeb is); 0x16 0 / 1 sets the text white or black for the rest of the message (four of
      j001_d06's lines and a manual page); 0x11 breaks the line, 0x17 centres one, 0x15 / 0x1c the
      name plate and the speaking side as before
- [x] the music as Omega has it (`_BGM_FILE` 0xdb344 / `_BGM_REPEAT` 0xc9d94): 38 tracks -
      bgm01..30 but 07, then bgm41..49 - where the port had CR1's 30 (bgm31 for track 29, nothing
      for 30..37); a track plays once when its repeat is 0 (bgm30), as `setRepeatFrame:` has it
- [x] the Android release as an add-on (`--android`, docs/ANDROID.md): its textures, its art,
      its Ogg Vorbis music
- [x] the Android release alone as the game (`--data` / `--android` with no iOS data): the
      OBB's files, its event files unscrambled, voices streamed from it; self-test clean

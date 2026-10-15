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

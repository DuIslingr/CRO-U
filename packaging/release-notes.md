# CRO-U v0.4.5

A from-scratch reimplementation of *Chaos Rings Omega* for Windows, macOS, Linux and Android.

## What's new

- **FSR 3.1.6:** the built-in FSR is updated from 3.1.4 to AMD's FSR SDK 2.4 passes.
- **FSR 4 (Linux and Windows, AMD GPUs):** a new Upscaler choice. AMD ships FSR 4 for Vulkan only as
  its own Windows DLLs, so they aren't included: put AMD's two SDK DLLs, the driver's
  `amdvkffx64.dll` and (on Linux) ffx-vk-linux's `libffx_vk_linux.so` into `fsr4/` in the game
  folder - its `README.txt` says where each comes from. On Linux the DLLs run natively through
  ffx-vk-linux, no Wine. Without the files FSR 3.1 stays as it is.
- **Vita music:** Omega's own tracks - the title theme among them - play from a Vita copy again.
  The Vita names them `cr2_m41.at9`..`cr2_m49.at9`, which the game didn't look for, so with a
  Vita copy alone the title was silent.
- **Cutscenes closer to the original** (the opening checked opcode by opcode against the iOS code):
  message windows fade in over 4 frames before their text and out over 5 after it, with the
  original's open / close sounds, and a script's `Open` / `Close` / fade waits / swap-ins take the
  frames the original spends on them (about 350 ms per window - Vieg's bow and "Now, now, Ayuta..."
  no longer come early); line breaks and page turns type at the original's pace; the "next" mark no
  longer blinks and the page counter is gone (the original has neither); `Anim2` starts its action
  over from the frame the script names (the sailor's salute and its ending); portraits sit flush to
  the screen's edge, the "Battleship Napur" strip is centred and the movement tutorial's pad art is
  the original's pieces; a hidden-field `Screen` is no longer mistaken for a 255-frame fade; a
  script's warp within its area no longer runs the room's entry script a second time; a script
  waiting for a tap with `Rept` isn't cut off after 45 s; a `Util` menu call takes its frame. On the deck and in the first dungeon:
  `FrmAdjc2` turns an actor to its record's facing (at once, or over the move) as the original
  does; a scene's effects face the camera on the field too - `FrmPlayEff2`'s turn their Y that way
  and keep their X / Z angles, as do the NPC-effect native's and the fire walls (every effect the
  original starts through `eff3d_set_with_pos`; `mdl_put` puts the camera's direction in the Y
  rotation whenever its byte is set); a camera slide, a move, a scroll
  and a quake take their first step on the frame the script starts them; the original's one-pixel
  tremors show (the view's corner is rounded after the shake is added); `CameraPlayer` leaves a
  room without a painting on its last shot, as the original's camera does.
- **Battles started by a scene:** Escape is refused while a scene runs (the original dims it),
  the battle's music is the one the scene names - random battles play Omega's battle theme
  (track 31) instead of a track the iPad data doesn't have -, the music after a battle follows
  the scene's var 60 (silence after the deck's and the first dungeon's fights, as the original),
  the deck's first-battle tutorial picture (`battle_start`) shows, the scene's own "skip off"
  (`Chg_MsgSkip`) now hides and disables the skip corner, the fade-in is the original's 20
  frames, and a lost scene battle shows `gameover` and
  waits for a tap before the title.
- **Battles, every one:** a first strike or an ambush is announced with the original's text from
  the first frame (there was no "Ambush!" at all); a boss's intro lasts 60 frames with its camera;
  the battle's music fades for ten frames before the victory; every battle ends with the original's
  fade to black and its music fading, and the field comes back out of black (the Ark came back out
  of white after a loss); an ordinary defeat waits for a tap, as the original, instead of ending by
  itself.
- **In a battle:** a round ends with the original's thirty frames on the formation's camera
  before the commands; an attack lets its actors come to rest and waits ten frames, a gene or
  item forty after its hit, a cancelled command sixty on its user; Defend takes the guard pose as
  it is chosen; the top menu's sound (0x22) plays; a party-wide target takes the original's camera;
  Omega's Down and Ward status icons use their own sprites; "Critical!" and its kind slide in
  sideways; the Camp's item and equip sounds are the original's, and a No to using an item goes
  back to the list.
- **Puzzle rooms:** the pyrokinesis stages (12..15) pass a lit switch's light on to the switches it
  links to, with the beam and the five-frame chain, as the original - the port took them for the
  first game's candle-block stages, so each switch had to be tapped on its own; a tap counts only
  twenty frames after the last; a lifted block's sounds are the original's (299 as it is taken,
  300 as it lands); a gem's effects are the original's; a Retry's fades last 500 ms.
- **Options:** Return to Title asks first and leaves through the original's black fade with the
  music stopping; the sound-effects gauge plays the original's test sound; the last battle's header
  is the original's picture.
- **The battle rules** (checked formula by formula against the iOS code): the pair's attack is
  the original's action (power 120, not the first game's 130..150), the Defense Gate skills no
  longer add to max HP and MP (the original's Gates each raise their own stat only), the first
  scenario's level cap is 100 as the original's, Dante's starting record no longer carries growth
  for levels it never gained, a Steal Item catch or a gene a monster teaches goes into the bag at
  once (kept on an escape or defeat, never listed on the result screen); two monsters only act
  together in the two-small-monster formation (not every pair of monsters), the bosses' 243 hits
  six times and 234 / 239 twice as the original's, an Escape resolves before any other action,
  group 664's always succeeds and its animation takes the original's frames, the first scenario's
  group 694 has your partner attack on his own; stat-up genes and items have no effect under the
  Ward ("No effect"), Panacea leaves the Ward, Endure holds against poison, a pair's Invincible
  covers both, a fallen member loses his element, criticals move the Break gauge even when the
  element triangle relabels them, a counter is the original's (all the blow's targets answer all
  its actors with the fighter's own counter action), and speeds are read afresh for every action
  of a round (a Speed Up or Down moves the rest of the round); the AI's fallback targets are the
  original's (a random living opponent).
- **Checked against the newest release (3.1.0):** the game logic of the passes above was re-read in
  the 3.1.0 code; two rules changed there and the port now follows them - the Counter skill (154)
  answers every physical hit it survives (1.0.0 rolled 10 / 20 %), and a pair attack whose partner
  has fallen becomes the survivor's own attack, announced anew. The same re-read caught three
  slips of the port's own: a same-map warp on map 152 no longer reloads the room, the second
  scenario's new game+ keeps items 541 and 631, and a tutorial's dimming takes the menus' 400 ms.
  With 3.1.0's data in French, German, Spanish, Chinese or Korean, the area title cards come in the
  language's own sheet.
- **One save for both scenarios:** the game now keeps a single `data.dat`, as the original's
  `_prty` is - both scenarios' records, bags and Oz, one set of flags and variables, and each
  scenario's own saved place. The second scenario therefore sees what the first earned (the shared
  genes, skills, manual pages and plates, the flags behind the Scenarios chart and the extras), a
  plain new game keeps your Oz and the shared bag as the original's does, and Load / New per
  scenario start from that scenario's own place. Your `save1.dat` / `save2.dat` from earlier
  versions are folded into it once; a Vita import still produces them and they fold the same way.
- **The title's announcements:** the one-time boards the original shows after a clear or when a
  "Breaking the Limit" trial opens ("New fun stuff available after clearing the game!", each
  trial's board, "Check out the Info Board next to Piu-Piu", "Voices added!"), a tap each, once a
  launch, with their marks kept in the save as the original's title keeps them.
- **Scripts across every area** (the opcode and native sets of all eleven files checked against the
  original's handlers): an actor told to face the way it moves turns as the original computes it
  (not a true arctangent - a diagonal walk faces 26 degrees), a degree given with a move is set as
  the move starts, `Anim` cross-fades over two frames, the second scenario's twelve tutorial
  pictures read their four-word form, `Uty_Mode` edits an object's bits (the sealed door of the
  third dungeon can't be examined again), the three "first time you hold it" tutorials and their
  manual pages (items 469..471) come at a scene's end as the original's `chk_tuto_kick` gives them,
  `Screen`'s second channel (op 58) is its own fade with its own wait, a warp to map -1 / -98
  reloads the room at the place / where you stand, a warp within one of the 3D rooms reloads it and
  one elsewhere keeps your facing.
- **The field's systems:** opened chests lose their badge and can't be examined again; a chest's
  or script's gift stops at 99 of an item; the shop's Sexy Pose discount is the original's `price -
  price / 10`, a gene, skill or OMU item you own is greyed and refused, and the second scenario's
  shop has its fifth Buy plate (the OMU items, one a purchase).
- **A new game after clearing:** new game+ wears the starting gear and plate again (the old gear
  waits in the bag) and keeps the manual's pages; the second scenario's new game+ also loses its
  collection and OMU items as the original's does; "Begin new game." over clear data empties the
  scenario's bag (the Oz and the shared genes and skills stay); the scenario's encounter switch is
  reset either way.
- **Battle voices** (the voiced release, checked against its code): Omega's own list of the story
  battles' victory lines (groups 651..737 → 600..620; the first game's list never matched), the
  comeback lines of groups 669..672, an action line for every action of a round (not the first
  actor's alone) and the sides' speakers picked afresh each action, the right line set for every
  monster speaker, no doubled line when an action has its own, only the side's speaker reacting to
  an all-target blow, the victory said by the member who last led the order and "hurt" judged on
  the speaker, the escape line by either member; on the field a clip the script carries over a
  page turn (0x1e ffff) goes on, and the closing tap cuts the clip as the original does.
- **Battle voices, the 3.1.0 code re-read:** a blow's reaction goes by the damage dealt and the HP
  left (one line when nothing was taken, another for a fall - the strong monsters of groups 669..672
  their own), none for a status tick or for an action's cost in its users' HP, the Counter skill's
  answer has its own line, and action 437's gate; the lines the port never said - Steal Oz's by the
  robbed monster, Steal Item's by the thief, a gene learned from the fallen, a barrier turning a
  gene back, 437's 1000-damage form; the attack's result line judged by the fighter's side; the
  victory's variants with a strong-growth monster in the battle, and either member may say it
  though down; the win waits for a line still playing in the five story groups that do; every line
  stops as the commands close; on the field a page's second clip starts where its code sits and the
  typing waits there for the first to finish, as PrcMsg does.
- **The title:** the title's art stands alone until a tap or key (its sound), then the logo goes and
  the plates slide in, as the original's title does; the "resume from last saved point?" offer
  comes after Start, not before the plates, and its Yes stops the music and fades to black before
  the load; "Load complete." holds the original's 20 frames; backing out of the second scenario's
  Load / New screen gives Select Scenario its tune back; the store's pictures answer a tap with
  their sound (there is no store).
- **Scripts' natives:** `NpcFade 0` fades the player (the second scenario's materialisation under
  effect 143); the first dungeon's two fire walls burn (their NPC rows' own native, which the
  original runs as the room is set up); `Uty_Btl_ParamUp` doubles the fighter's stats for the
  battle (battle 657) as the original does, not +25 % for six turns; `Uty_ChkLogo_flg` reads or
  sets as its var says; `puzzle_teleport_cnt` holds the field menu's button while it counts.
- **On the field:** the view glides after the player (30 % of the way each frame, as
  `setScrollPlayer(0.3)`) instead of being bolted to him; a quick tap anywhere examines or talks
  (the original's CheckTap); standing by something, the leader turns to face it; the "!" bubble is
  the original's piece over the object's top; the puzzle rooms' player radius is 4; turned trigger
  boxes are tested turned.
- **The Ark:** a scene's "Save data?" (`Util 6`) leads to the title whether you save or not, asking
  "return to the title without saving?" on a No, as the original - it no longer dropped you back on
  the field; the shop sells the genes' stock and the OMU items one at a time, gives Sexy Pose's
  discount for the pair actually fighting, and plays the original's sounds for a sale's No and for
  equipping.
- **Resuming:** leaving the game during a cutscene no longer makes the title's "resume from last
  saved point?" put you on that cutscene's painting with nothing running. The resume point is only
  what a scene's end (or an encounter) saved, as the original's is - quitting itself saves nothing.
- **Story battles you're meant to lose** (650, 654, 674 ...) now go on with the story instead of
  ending in a game over (reported by [metastigma](https://github.com/metastigma)).
- **Skip:** the skip button skips while you hold it and stops when you let go, as in the original.
- **Animation:** every change of motion blends over two frames, as the original does. The warp
  crystals turn on their own again, and a few models (c151, j009, m009, m122 and others) get
  motions that were missing.
- **Battles:** attacks use the original's animation choice, so monsters like m225 no longer stand
  idle through their attack; attackers only step in when their animation moves them; empty
  animation slots pass to the next one.
- **Fixes contributed by [metastigma](https://github.com/metastigma):** the player stops walking when a scene starts; an item window
  no longer answers the question before it; characters riding another model stay attached;
  turning HD Textures on in settings.ini updates the menus.
- **Windows** (hotfix, same version): the first-start setup can copy and unpack your game from a
  folder whose name uses another alphabet - a Cyrillic user name on a Russian Windows, for example.

Thanks to [metastigma](https://github.com/metastigma) for the report, those fixes and testing. All of it was checked against the original iPad code.
- **The Camp's gene plates, as the original's** (CampMainPrc read in the 3.1.0 code): choosing a plate
  no longer wears it - it opens the plate's known entries, with the board switched from the entry
  names to the plate's picture, and only that list's Equip plate wears it (none while the member
  already wears it); Back returns to the plate list where it was. The plate list is the original's:
  the bag's plates with a known entry (the Human Genes slot lists its 474..480 whatever is known), a
  plate the other member wears listed in its worn colour instead of hidden, and Remove last, only
  while the slot is worn, acting at once; jewels get the same Remove. The slot list refuses (dim,
  with the buzzer) the empty plate slots 2-4 until a common skill is known from a plate in the bag,
  the Human Genes 474..476 in the first scenario, and the empty jewel slot while no jewel is free. The
  Genes page lists every plate in the bag in id order, refuses one with nothing known and opens the
  others' entries the same way. The Status page keeps the pair's plates beside the details and drops
  the small record picture from the board; the Items page leaves out the held-back member's one worn
  jewel, as the original does.

## Downloads

| File | Platform |
|---|---|
| `CRO-U-windows-x64.zip` | Windows 10+ x64 |
| `CRO-U-macos-universal.zip` | macOS 11+ (Apple silicon and Intel) |
| `CRO-U-x86_64.AppImage` / `CRO-U-linux-x86_64.tar.gz` | Linux x86-64 |
| `CRO-U-android.apk` | Android 7+ |

## Getting started

1. Download the file for your system and unzip it.
2. Start `chaosrings`. If it can't find your copy of the game, it asks you for one and sets it up.
3. Play.

- **Windows:** if SmartScreen warns about an unsigned program, choose *More info*, *Run anyway*.
- **macOS:** right-click `chaosrings.app` and choose *Open* (it isn't signed).
- **Linux:** `chmod +x` the AppImage, then run it.
- **Android:** install the APK, then copy your game files over USB into
  `Android/data/local.preservation.chaosringsomega/files/`, in an `ios/`, `android/` or `vita/` folder.

The README's "Your copy" section lists exactly which files each release needs.

## Good to know

- **Android, updating:** from this version on, each release installs over the one before it. Every
  earlier version was signed with a different key, so coming from one, uninstall it first - one
  last time. That deletes the game files you copied in and your saves: keep a copy of the game
  files to copy back.
- **No game data is included.** You need your own copy; the port reads it at run time.
- **Tested on:** Linux (played), Windows (under Wine). The macOS and Android builds are made by CI
  but haven't been run on real hardware yet. Reports are welcome.
- **DLSS** isn't in these builds (NVIDIA's library can't ship with GPL code); you can build it
  yourself (README, Platforms).
- Licence GPL-2.0-or-later (the macOS app is GPL-3.0 because of MoltenVK). A fan preservation
  project, not affiliated with Square Enix or Media.Vision.

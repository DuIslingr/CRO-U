# CRO-U v0.4.5

A from-scratch reimplementation of *Chaos Rings Omega* for Windows, macOS, Linux and Android.

## What's new

- **Field events start as in 3.1.0:** a touch box you arrive in now fires once the scene is over
  when it isn't the one that brought you there - so the Ark's welcome-back event plays again after
  every return from a dungeon. Who is in a room is decided as the room loads, as the original does:
  a flag set by a scene mid-room no longer makes an event box or a person appear on the spot, and
  the story's actors wait hidden until a scene brings them on (no more stray, solid figures with a
  "!" in d04, d06, c101_d07 and the Ark). After a battle the room is read back as it was set up -
  the treasure boxes a scene put away return, and so do people a scene hid. Losing a battle runs the
  Ark's area script, as the original does, not the room's. Examining or talking plays the original's
  sound; with both in reach an object is examined before a person, the nearest by real 3D distance,
  and the "!" stays a moment after you step away; invisible event boxes and walls stand where the
  original puts them (their offset was applied twice).
- **The field HUD as 3.1.0's:** the corner button, the bar, the pad and the "!" vanish the moment a
  scene starts and come back whole - no fade. Closing the Camp, Options or the save screen leaves
  the bar shut; the corner button sits a little lower, where the original's is; a puzzle room's teleport lock
  holds back Camp, Save and the puzzle buttons but not the corner button. The dungeons' key icons
  are back at the bottom left (lit once you have the key, gone once the door is open), and a low
  battery (under 18%) shows its icon in the top left corner on machines that have one.
- **Walking as the original's collision has it:** the field's floor and walls are read from the
  collision file's own tree, as the iOS game does - the floor under the leader is the highest one
  in reach (ramps such as d0207's climb smoothly, no 5-unit step limit), walls push only when faced
  and a move through one is undone, and the floor, NPC bodies and walls are checked every frame, so
  standing still or during a scene the leader stays on the ground. Chests and other solid NPCs are
  hit where they stand now, from their own height up (a box high above lets the leader pass under),
  and j001_p01's floor plates lift the leader onto them. The encounter count uses the distance the
  leader actually moved after the pushes.
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
- **Music and sound, as 3.1.0 mixes and plays them:** a script that fades the music out, warps
  and asks for the same track again now gets it from the start (it went silent); the balance is the
  original's - music at 0.2 of its gauge, effects at 0.3, voices at 1.0 - so voices are no longer
  drowned by the music, and the voice gauge starts at 80 as the others do; the gauges move in steps
  of 10; moving a list's cursor plays the original's light tick (0x92), not the "refused" buzzer;
  an area no longer starts a guessed track of its own before its entry script decides (some rooms
  are meant to be quiet); after a random battle the field's last requested track comes back even
  if the room had faded it out; the track a script reads back after a stop or fade is "none" (the
  shop's music recovery); a sound marked as a stand-in for a muted voice plays only with voices
  off.
- **The field's floors:** a polygon is a floor or a wall by the collision file's own lists, as the
  original reads them, not by its slope - four dungeon ramps (d0207, d0309, d0311, d0408) can be
  climbed again, and the stray squares at some maps' origin are no longer ground.
- **Scenarios and the Info Board:** completing a bingo card plays the original's sequence - the
  card's pieces fly in and bob with the music muted, then a white flash - before it counts as done;
  the Camp marks Scenarios "new" while no cell is open; Collection is offered only in the second
  scenario, as the original's; the Info Board next to Piu-Piu shows the "Breaking the Limit" pages
  (versionup_e / versionup_t), not the puzzle tutorials' pictures.
- **Battles, as they look in 3.1.0:** an attack's camera is the original's side - a monster's blow
  on the party frames the party, your blow on a monster frames the monster, with its random
  variant; the party's HP and MP count down (or up) to the new value over a few frames instead of
  jumping, and MP shows red at 0; the hit labels are the original's - "Miss!" where the port said
  "Guard!", "Block!" where it said "Miss!" - and a hit on an invincible target shows "0".
- **The battle's camera and staging, as 3.1.0's** (btlcamera_set / _prc and btl_prc read in its code):
  the field-of-battle, victory and casting cameras glide after their mark (a fifth of the way a
  frame) and moving the target cursor eases the view at half the way a frame, where the port cut or
  slid evenly; the cameras that tilt do (the attack close-ups by 3-5 degrees, the boss's charge
  from -10 to 5); a gene's or item's casting camera turns over the original's 25 frames and then
  holds instead of crawling for minutes; a turning camera keeps the original's distance; the defeat
  camera circles for as long as the screen shows; the battle opens on the formation's still view; a
  boss's fall turns its camera over the fall; the victory camera frames the member who last led the
  order. A gene or item now cuts to its target as it lands (the whole side for a side-wide one, the
  field of battle for a few), and lands at the original's moment of its casting motion; a member's
  run-up blow sometimes switches to a closer camera as it arrives, as the original's does. Attackers step to the original's fixed spots in front of their target (from the formation
  table, a big monster further back) and face the way they run. A guarding member takes a blow
  without flinching, a miss or a fended-off blow plays no hurt motion (the port played the get-up
  one), a revived member gets up, a fallen member drops and stays down and a beaten monster fades
  as it reels. A miss, a zero or an invincible target still shakes the screen with the hit's effect
  and sound, as the original does; elemental genes do so whenever they deal damage. The damage
  numbers keep to the original's five at a time, stay inside its screen margins, stack as its own
  do when several land, and are drawn at full strength. The HP and MP bars are the original's
  slanted bars with their offset shadow (the enemy's 96 wide), and the Break label pops from the
  badge's corner at full strength.
- **Battle menus:** each menu page slides in from the right and back out, taking taps only once
  it's in, and the round starts as the last page leaves; the party's portraits and bars slide away
  while the characters talk. Escape asks "Run from battle?" on the top banner with Yes and No
  buttons on the right, the party rows sit on the original's dark backing, and the target names
  are drawn at the original's size and place.
- **Analyze** now tells everything the original does, a line per tap: the monster's name, its
  remaining HP, each weakness and each resistance (or "No results").
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
- **The title's "Resume game from last saved point?", as the original's** (TitlePrc read in the
  3.1.0 code): the dark panel is gone. After Start the plates slide out and the interrupted game's
  pair comes in on the left as face boards, its date and play time and the Yes / No plates come in
  from the right, and the two lines sit on the message board at the bottom with its tap mark; an
  answer sends them back out before the load, or before Start goes on. The first time in a launch,
  and only while the save hasn't seen it, "About Progress Tracker" is read through in the message
  window first.
- **The save date board:** the Load screen's date and play-time board shows the original's labels,
  months and slash. It had been drawing the title's store pictures and the wrong months in their
  place.
- **The Camp's Status page and Items, as the original's** (CampMainPrc read in the 3.1.0 code): on a
  member's status the arrows (the left / right keys, or a sideways swipe) now turn from the board to
  the equipment the member wears, then to every skill and gene known on its plates, and back, instead
  of switching members; Back returns to the member choice, and the details no longer show the member
  plates. After a seed or an MP item is used, the member's board stays up - a seed's stat marked with
  how much it rose - until a tap. The Yes / No has no Back; a full MP or a stat already at 9999 is
  refused there with the buzzer, and the seed count reaches as many as the stat can still take. The
  Items list's Back bar has its two arrows for the previous and next category.
- **The Emergency Exit works:** used from the Camp outside the Ark (and before the story closes it),
  the Camp fades to white and the pair is back on the Ark with HP and MP refilled; on the Ark itself
  it is refused.
- **The Camp, the rest:** the face boards show (and take taps) while you choose a member, and no
  status board covers them; the equipment slots' arrows switch to the other member; the item the member
  already wears is refused; changing equipment on the Ark refills the pair; in the second scenario,
  once the story allows it, tapping a face board on the Camp's front makes that character the leader;
  the two one-time help messages (the first gene plate, the first visit to Items) show as in the
  original.
- **The Instruction Manual, as the original's** (Options; read in the 3.1.0 code): a topic opens in
  the game's own message window, a page at a time - tap or confirm turns it - with the window's
  open and close sounds, and when it closes the list is back at that topic, instead of all the text
  at once in a side panel. Solo and Pair, the Break Gauge, Elements, Gene Plates and Statuses show
  their animated tutorial pictures under the text, a picture a page, as the scripts' tutorials do.
  "About Option" on the first Options of a game shows in the same window.
- **Scenarios' cards slide:** < and > slide the card out and the next one in from the other side,
  as the original's, instead of switching at once; while a card not yet done slides, its picture
  stays hidden behind the cells. Choosing Scenarios in the Camp first slides the Camp's windows
  away, and they slide back in when you return.
- **Puzzle rooms, the touches as 3.1.0 reads them:** a lifted block shows the original's arrows on
  the sides it can go (the free cells inside the grid); the one it goes flickers out, a let-go fades
  them all. A swipe sends the block as soon as the finger leaves the original's dead zone, without
  waiting for the finger to lift; lifting it inside the zone lets the block go. A tap for teleport,
  toughness or pyrokinesis is the original's: let go within five frames, not while the leader walks,
  aimed at the object's top within the original's window (a gem's sits lower); a touch that misses
  walks the leader as before. The pad's third button appears once the room's own counter is past
  two (the original's rule) and starts the room's third script; the pad's buttons, Camp and Save wait
  out the original's short lock after a Retry, a teleport or a block's flight. A second finger
  scrolls the view to it on a touchscreen, as in the original. A Retry puts the leader back facing
  the way it came in, standing. A cleared room is left as the map has it (no Retry or Help buttons),
  and the psychokinesis rooms' blocks start as the map places them; a sliding or falling block
  squashes the try by its own box.
- **Options, "About Option":** it shows from the title's Options too, as the original's, alone in
  the message window - the Options page and its dimming come once it's read.

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

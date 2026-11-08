# Reference-video checks

Decompilation readings are hypotheses about what players saw; the reference videos decide what is
on screen. This file records which of the port's code-read behaviours were checked against the
reference playlist, and how.

Reference: the user's Omega playlist (YouTube PLS0s-p_HBqrsXpoFNEqmwz4UIkDdo29aU, "Chaos Rings Omega
FULL GAMEPLAY"), the Android release at 1280 x 720. It has its own 16:9 layout, so compare content,
sequence and timing, and layout only within a screen - never absolute positions against the iPad
or Vita builds. Method: the built-in browser, the video paused, frames drawn onto a canvas at chosen
times, brightness / frame differences measured in the page; or, frame-exact, ffmpeg on the local
copies (Archive/refvideos/omega, named by part and video id). The game draws 20 frames a second;
Part 1 runs at 30 fps and Parts 2-4 at 50, so a game frame spans 1.5 or 2.5 video frames. A playing
video moves under a seek: pause it first, and label each grab with the frame callback's media time.

## Checked 2026-11-08

| Behaviour (commit) | Video, time | Result |
|---|---|---|
| A tap followed by a line break keeps the lines above (`0x12 0x11`, 174094b) | Part 3 (SGuslIKuXyI) 1040.8 s: "Ungh... *pant pant*" then "Vieg..." under it (message 3204); 1078.6 s: "Don't worry... I can do this. I'll be waiting for / you, Vieg. / Our baby is fighting, too..." (3209) | Confirmed |
| A page cleared by `0x10` replaces the page | Part 3 2100-2103 s: "Do for her?" replaced by "No, the only "remedy"..." (3403) | Confirmed |
| `0x12` followed by text continues on the same line at once (174094b) | - | Not found in the videos checked: code reading only |
| The player's field shadow (8e8c370) | Part 2 (bMgJjj3q8GQ) 390 s: a dark oval under the running leader in the first dungeon | Confirmed |
| Battle floor shadows (54a299d) | Part 3 1111.2-1112.4 s: dark ovals under the flying monster and the pair on the library floor | Confirmed (snow battles in Part 2 hide them) |
| Field HUD cut, no fade (37a73cf) | Part 3 2079.557 -> 2079.594 s: the corner button fully drawn, then gone on the next frame; 1098.445 -> 1098.514 s: absent, then fully drawn | Confirmed both ways |
| Battle fade-in over 20 frames (pass 2 / 54a299d) | Part 3 1110.79-1111.75 s: linear from white over about 0.96 s | Confirmed |
| Solid white before the fade (54a299d: about 5 frames) | Part 3 1110.30-1110.75 s: about 9 frames | Longer in the video; the original holds white while the battle loads, so the length depends on the device - the port's is the minimum |
| The intro camera: the formation's camera cut on frame 1 of the fade, a 20° turn over 15 frames eased 1/5 a frame, then the command camera (54a299d, 9a4731b) | Part 3 1110.82-1112.34 s, measured by phase correlation of the far background, per game frame (a new image every 2-3 video frames at 50 fps; the cut re-checked with ffmpeg at 1112.34 s): the turn's speed profile (peak at frame 15, settled under 5% by frame 29) matches the port's captures frame for frame; the cut to the command camera comes at frame 32 | Confirmed after a correction: the port cut to the command camera at frame 30, now 32 (9a4731b). (A first look at this clip misread it as contradicting the port - its frame differences were washed out by the fade, and the port's behaviour was taken from a commit message instead of its own frames.) |
| The ambush and formation-2 intros (9a4731b) | - | Code reading only |
| The party's HP / MP roll toward the new value (f2ead07) | Part 3 1116.0-1118.5 s: 1320 -> 1213 -> 1113 -> 1099 and 1570 -> 1460 -> 1358 -> 1344 over about a second | Confirmed |
| Slanted HP / MP bars (34d96cb) | Part 3 battle HUD frames | Consistent |
| Ark arrival plays the return scene at once (event scripts) | Part 2 (bMgJjj3q8GQ) 1696 s: the warp's white; 1697-1698 s the Ark hall; 1699 s the scene, with no walking first | Confirmed |
| Battle pages slide in and out over 4 frames (893363e) | Part 2 1248.72-1248.82 s (Solo / Pair in), 1249.24-1249.32 s (out after Pair), 1249.54-1249.78 s (Attack in and out): each way, frames with no plate, one frame with the plates 240 px right (cut by the screen's edge), then settled / gone. Rows 96 px apart for the port's 48: the game's 2x screen | Timing confirmed. The distance differs by platform: iOS 3.1.0 moves 320 at 80 a frame (the port's iPhone and iPad layouts); Android 1.1.6's _menub_mainPrc moves 480 at 120 on a Retina or 720 x 480 screen (its iPhone screen is centred on a wider phone). The port's Android layout now does that; `--battle-view top@n` captures there show nothing at 360 and 240, the plates cut at 120, settled at 0 - as the video |
| The Break badge's label pops when a side takes the Break: 5x down to 1x over 7 frames (btlbreak_set) | Part 3 (SGuslIKuXyI) 1117.02-1117.40 s: "Enemy" appears large over the ring and settles in about 0.36 s | Confirmed |
| The badge's ring fills a tick at a time toward the gauge (menub_mainPut) | Part 3 1117.02-1117.38 s, ffmpeg frame by frame: one red tick at 1117.02, two at 1117.12, four by 1117.38 - a tick each 0.1 s, every other game frame | Contradicted: the port added a tick every frame. 3.1.0 steps only while prc's frame count (+0x918c4, raised each frame) is even; the port now does the same |
| Damage digits hop one after another, left to right, each a frame behind, settled 7 frames in (kDigitHop, __dmg_set's timer 16 a frame) | Part 3 1116.96-1117.32 s: "226" and "221" rise digit by digit from the left and settle in about 0.36 s; both targets' numbers start together | Consistent (order and length; the hop heights not measured - the hit's flash washes them out) |
| The action's name on the top banner: 40 frames, cut in and out (kBannerFrames, font_set_utf16's 0x28) | Part 3 1115.24-1117.24 s: "Migrate Bite" at full strength from its first frame, gone at 1117.26 s - 2.0 s | Confirmed |
| A failed hit roll shows "Miss!" (type 6, window.mct's label 6; f2ead07) | Part 2 (bMgJjj3q8GQ) 986.72-987.88 s: the Snow Tiger's blow on Vieg, "Miss!" with its blue splash and yellow streak - the atlas's picture 6; it hops 3, 5-6, 12, 6, 3, 0 (game px; video / 2) and stays 1.16 s | Confirmed (kLabelHop 3, 6, 12, 6, 3; 24 frames) |
| Blowout / Critical / Weak slide in from the right and out to the left (18 a frame, alpha by quarters) | Part 3 888.9-889.9 s: "Critical!" first seen 38 px right of its place, settled the next frame, one frame 40 px left at lower strength, then gone while "530" stays a few frames more | Consistent (the faint first and last steps fall under the measure) |
| Escape: "Run from battle?" on the top banner while asked; No clears it and the first page comes back (893363e) | Part 3 1321.75-1323.0 s: the question and Yes / No; after No the banner is gone at once, the plates slide out, Solo / Pair / Escape / Options slide in | Confirmed |
| A first strike's banner (btl_main 200: sys 0x36 / 0x37, 60 frames) | Part 3 1319.3-1321.5 s: "Vieg's team attacks first!" from the fade-in | Contradicted, fixed: the port left out the name (var 0, the lead's: +0xaa, as the defeat banner); the self-test now prints the banner |
| Analyze's lines (893363e) | Parts 2-4, OCR at 2 frames a second: Analyze is only seen in gene lists and its help, never cast | Code reading only |
| The Break effect: stripes, "BREAK!!" and "The battle situation changed" slide in over 5 frames (480 sin t, 18 degrees a frame), hold 30, the badge pops to the new side at 15, out over 5 (btlbreak_effect_prc) | Part 2 625.86-627.70 s: in by 625.96, held to 627.52 (about 31 frames), the badge pops to "Player" at 626.86 - about 13.5 frames before the out-slide - and all gone by 627.70 | Consistent: the pop lands 1-2 frames off the port's 15-of-30, within the measure; 3.1.0's state 10 is the port's (t == 30 / 2) |
| Queued battle messages: each 60 frames, after the action's name has run its 40 (kMessageFrames, menub_mainPut's queue) | Part 2 1261.08-1269.05 s: "Poison Hand" 2.0 s, then "Vieg was poisoned!" 1263.1-1266.05 s, then "Olgar was poisoned!" 1266.1-1269.05 s, each cut in and out | Confirmed |
| The battle's cursor plate lit as a page arrives (port) | Part 2 1248.82-1249.20 s: Solo and Pair both dark until Pair is tapped (1249.22 s lit) | Differs: 3.1.0 lights the touched plate only (+0x92 reset to -1 every frame in state 2); the port lights the cursor's, which pad and keyboard play need. Open: hide it after touch input |

Everything else from the 3.1.0 audit rounds rests on code reading; check it here before claiming it
is what players saw.

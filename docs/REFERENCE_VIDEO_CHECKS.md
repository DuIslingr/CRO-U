# Reference-video checks

Decompilation readings are hypotheses about what players saw; the reference videos decide what is
on screen. This file records which of the port's code-read behaviours were checked against the
reference playlist, and how.

Reference: the user's Omega playlist (YouTube PLS0s-p_HBqrsXpoFNEqmwz4UIkDdo29aU, "Chaos Rings Omega
FULL GAMEPLAY"), the Android release at 1280 x 720. It has its own 16:9 layout, so compare content,
sequence and timing, and layout only within a screen - never absolute positions against the iPad
or Vita builds. Method: the built-in browser, the video paused, frames drawn onto a canvas at chosen
times, brightness / frame differences measured in the page (20 game frames a second; the video runs
at 30).

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
| The intro camera: the formation's camera cut on frame 1 of the fade, a 20° turn over 15 frames eased 1/5 a frame, then the command camera (54a299d, 9a4731b) | Part 3 1110.82-1112.34 s, measured by phase correlation of the far background, each video frame one game frame: the turn's speed profile (peak at frame 15, settled under 5% by frame 29) matches the port's captures frame for frame; the cut to the command camera comes at frame 32 | Confirmed after a correction: the port cut to the command camera at frame 30, now 32 (9a4731b). (A first look at this clip misread it as contradicting the port - its frame differences were washed out by the fade, and the port's behaviour was taken from a commit message instead of its own frames.) |
| The ambush and formation-2 intros (9a4731b) | - | Code reading only |
| The party's HP / MP roll toward the new value (f2ead07) | Part 3 1116.0-1118.5 s: 1320 -> 1213 -> 1113 -> 1099 and 1570 -> 1460 -> 1358 -> 1344 over about a second | Confirmed |
| Slanted HP / MP bars (34d96cb) | Part 3 battle HUD frames | Consistent |

Everything else from the 3.1.0 audit rounds rests on code reading; check it here before claiming it
is what players saw.

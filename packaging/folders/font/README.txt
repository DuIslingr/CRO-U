Chaos Rings Omega (CRO-U): your own font, if you want one - a .ttf, .otf or .ttc file put here.

Leave this folder empty and the game draws its text with the system's font, as the iOS game does:
Noto Sans CJK (its Japanese, Chinese or Korean face for each language), Hiragino on macOS, Yu
Gothic on Windows - or, with the Vita copy as the game, the Vita's own fonts.

A font here takes their place:
- for Japanese, Chinese and Korean, the first file (by name) that has their characters;
- for English, French, German and Spanish, the first file.
A font without Japanese / Chinese / Korean characters is used for the Latin languages only. A font
far wider than the original's can run long lines off the message window. The CR_FONT environment
variable still comes first. The log (chaosrings.log in the profile folder) names the file used.

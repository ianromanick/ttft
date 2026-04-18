Many years ago, I wrote an implementation of Tetris for VT100/VT220 terminals. I was inspired to write it after seeing John Tromp's [entry in the 1989 International Obfuscated C Code Contest](https://github.com/ioccc-src/winner/tree/master/1989/tromp) that implemented a fully playable version of VT100 Tetris.

I eventually ported the core mechanics of my VT220 Tetris to run on a 8051 microcontroller connected to a Sega Master System game pad and a VT220 terminal (with no keyboard) for a project at Portland State University. I believe this was in 1997. Unfortunately that hardware (both the terminal and the 8051 board) and source code are lost to time.

After seeing some of Adrian Black's videos about the [Plexus P/20](https://youtu.be/lBprWU9cHXs?si=81Ji5NabZubfZgmB) inspired me to revive this long dead project. What would an implementation of terminal Tetris look like for a very low powered Unix system? How many modern Tetris quality-of-life improvements could be added? That is this project.

Still to be done:

- As completed lines are removed, the graphics used for the remaining blocks should stay the same. The only data that is explicitly stored now is whether a block is occupied.
- Provide modern "shuffle" RNG mode. Classis NES and Game Boy Tetris would select each tetromino at random with equal probability. This could lead to long bursts of the same piece in a row and long droughts of other pieces (seee below). Modern Tetris implementations instead shufle the 7 possible tetrominos. Once all 7 have been provided, they are shuffled again. This means that a particular piece can only occur twice in a row (i.e., when it is last in one shuffle and first in the next). It also means that there can be at most 12 pieces between occurances of any particular piece (i.e., when that piece is first in one shuffle and last in the next). If you've ever had 17 Z or S pieces while waiting for an I, you know how much less irritating this RNG mode is.
- In classic RNG mode, a drought counter. When it has been more than 12 tetrominos since the last I, the game is in drought. Display the running tally.
- Allow the RNG seed to be specified somehow.
- Two-player mode in the spirit of Game Boy Tetris. The intention would be to use more modern [garbage rules](https://tetris.fandom.com/wiki/Garbage).
- Add a "Tetris rate" display.

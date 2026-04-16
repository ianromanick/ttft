#!/usr/bin/env python
#
# Copyright © 2026 Ian D. Romanick
# SPDX-License-Identifier: GPL-3.0-only

O = (("        ",
      "  lqqk  ",
      "  mqqj  ",
      "        "),
     ("        ",
      "  lqqk  ",
      "  mqqj  ",
      "        "),
     ("        ",
      "  lqqk  ",
      "  mqqj  ",
      "        "),
     ("        ",
      "  lqqk  ",
      "  mqqj  ",
      "        "))

I = (("    []  ",
      "    []  ",
      "    []  ",
      "    []  "),
     ("        ",
      "        ",
      "[][][][]",
      "        "),
     ("    []  ",
      "    []  ",
      "    []  ",
      "    []  "),
     ("        ",
      "        ",
      "[][][][]",
      "        "))

S = (("        ",
      "        ",
      "  lwqq  ",
      "qqvj    "),
     ("        ",
      "  xx    ",
      "  mvwk  ",
      "    xx  "),
     ("        ",
      "        ",
      "  lwqq  ",
      "qqvj    "),
     ("        ",
      "  xx    ",
      "  mvwk  ",
      "    xx  "))

Z = (("        ",
      "        ",
      "qqwk    ",
      "  mvqq  "),
     ("        ",
      "    xx  ",
      "  lwvj  ",
      "  xx    "),
     ("        ",
      "        ",
      "qqwk    ",
      "  mvqq  "),
     ("        ",
      "    xx  ",
      "  lwvj  ",
      "  xx    "))

L = (("        ",
      "  aa    ",
      "  aa    ",
      "  aaaa  "),
     ("        ",
      "        ",
      "aaaaaa  ",
      "aa      "),
     ("        ",
      "aaaa    ",
      "  aa    ",
      "  aa    "),
     ("        ",
      "    aa  ",
      "aaaaaa  ",
      "        "))

J = (("        ",
      "  aa    ",
      "  aa    ",
      "aaaa    "),
     ("        ",
      "aa      ",
      "aaaaaa  ",
      "        "),
     ("        ",
      "  aaaa  ",
      "  aa    ",
      "  aa    "),
     ("        ",
      "        ",
      "aaaaaa  ",
      "    aa  "))

T = (("        ",
      "        ",
      "aaaaaa  ",
      "  aa    "),
     ("        ",
      "  aa    ",
      "aaaa    ",
      "  aa    "),
     ("        ",
      "  aa    ",
      "aaaaaa  ",
      "        "),
     ("        ",
      "  aa    ",
      "  aaaa  ",
      "  aa    "))

all_pieces = (('O', O),
              ('I', I),
              ('S', S),
              ('Z', Z),
              ('L', L),
              ('J', J),
              ('T', T))

def get_shift_for_frame(f):
    for i in range(4):
        s = f[0][2 * i] + f[1][2 * i] + f[2][2 * i] + f[3][2 * i]

        if s != '    ':
            return i

    return 0

def generate_mask(t):
    piece_mask = []
    for frame in t:
        tmp = []

        for line in frame:
            mask = 0

            skip = True
            for c in line:
                skip = not skip
                if skip:
                    continue

                mask <<= 1
                if c != ' ':
                    mask |= 0x1000

            tmp.append(mask)

        shift = get_shift_for_frame(frame)
        frame_mask = [shift]
        for x in tmp:
            frame_mask.append(x << shift)

        piece_mask.append(frame_mask)

    return piece_mask

def generate_draw(t, erase=False):
    frames = []
    for frame in t:
        # There are no gaps in a tetromino. It will transition from
        # zero or more empty to one or more non-empty back to zero or
        # more empty. This applies to rows (zero or more blank rows
        # followed by one or more non-blank rows, etc.) and columns
        # (zero or more blank "pixels," etc.)

        shift = 2 * get_shift_for_frame(frame)
        last_x = 1
        last_y = 1
        y = 1
        s = ''
        for line in frame:
            if line == "        ":
                y += 1
                continue

            x = 1
            for c in line[shift:]:
                if y != last_y:
                    if c == " ":
                        x += 1
                        continue
                    else:
                        if y - last_y == 1:
                            s += '\\x1b[B'
                        else:
                            s += f'\\x1b[{y-last_y}B'

                        if x == last_x:
                            pass
                        elif x - last_x == 1:
                            s += '\\x1b[C'
                        elif x - last_x == -1:
                            s += '\\x1b[D'
                        elif x > last_x:
                            s += f'\\x1b[{x-last_x}C'
                        else:
                            s += f'\\x1b[{last_x-x}D'

                        last_y = y

                x += 1

                if c != ' ':
                    if erase:
                        s += ' '
                    else:
                        s += c

                    last_x = x

            y += 1

        frames.append(s)

    return frames

def emit(name, draw, erase, mask):
    print("    {")
    print(f"        '{name}', 4,")
    print("        {")
    for i in range(4):
        print( "            {")
        print(f"                {mask[i][0]},")
        print( "                {")
        print(f"                    {mask[i][1]}, {mask[i][2]}, {mask[i][3]}, {mask[i][4]}")
        print( "                },")
        print(f"                \"{draw[i]}\",")
        print(f"                \"{erase[i]}\"")
        print( "            },")

    print("        }")
    print("    },")

print("""/*
 * Copyright © 2026 Ian D. Romanick
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef TETROMINOS_H
#define TETROMINOS_H

static const struct tetromino all_pieces[] = {""")

for name, piece in all_pieces:
    draw = generate_draw(piece)
    erase = generate_draw(piece, True)
    mask = generate_mask(piece)

    emit(name, draw, erase, mask)

print("""};
#endif /* TETROMINOS_H */""")

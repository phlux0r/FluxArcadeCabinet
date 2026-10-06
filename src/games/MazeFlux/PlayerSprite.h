#ifndef MAZE_PLAYER_SPRITE_H
#define MAZE_PLAYER_SPRITE_H

#include <Adafruit_GFX.h>
#include "PlayerMaze.h"

// The explorer: a 10x10 helmeted figure with a visor, drawn facing the way
// it last moved, its feet swapping each step. One character a pixel:
//   . clear  K outline  C suit  L suit highlight  B suit shadow
//   V visor  E visor shine  F boots
// Facing left is facing right mirrored.
namespace mazesprite {

static const char* const DOWN[2][10] = {
    { "...KKKK...",
      "..KLCCCK..",
      ".KLCCCCCK.",
      ".KVVVVVVK.",
      ".KVEVVEVK.",
      ".KCCCCCCK.",
      "..KCBBCK..",
      ".KCCCCCCK.",
      ".KFK..KFK.",
      "..K....K.." },
    { "...KKKK...",
      "..KLCCCK..",
      ".KLCCCCCK.",
      ".KVVVVVVK.",
      ".KVEVVEVK.",
      ".KCCCCCCK.",
      "..KCBBCK..",
      ".KCCCCCCK.",
      "..KFKKFK..",
      "...K..K..." },
};
static const char* const UP[2][10] = {
    { "...KKKK...",
      "..KLCCCK..",
      ".KLCCCCCK.",
      ".KCCCCCCK.",
      ".KCBCCBCK.",
      ".KCCCCCCK.",
      "..KCBBCK..",
      ".KCCCCCCK.",
      ".KFK..KFK.",
      "..K....K.." },
    { "...KKKK...",
      "..KLCCCK..",
      ".KLCCCCCK.",
      ".KCCCCCCK.",
      ".KCBCCBCK.",
      ".KCCCCCCK.",
      "..KCBBCK..",
      ".KCCCCCCK.",
      "..KFKKFK..",
      "...K..K..." },
};
static const char* const RIGHT[2][10] = {
    { "...KKKK...",
      "..KLCCCK..",
      ".KLCCCCCK.",
      ".KCCCVVVK.",
      ".KCCCVEVK.",
      ".KCCCCCCK.",
      "..KCBBCK..",
      "..KCCCCK..",
      "..KFKKFK..",
      "...K..K..." },
    { "...KKKK...",
      "..KLCCCK..",
      ".KLCCCCCK.",
      ".KCCCVVVK.",
      ".KCCCVEVK.",
      ".KCCCCCCK.",
      "..KCBBCK..",
      "..KCCCCK..",
      ".KFK..KFK.",
      ".K......K." },
};

inline uint16_t colourOf(char c) {
    switch (c) {
        case 'K': return 0x0841;   // near-black outline
        case 'C': return 0xFFFF;   // white suit
        case 'L': return 0xFFFF;
        case 'B': return 0x9CF3;   // grey shadow
        case 'V': return 0x05FF;   // cyan-blue visor
        case 'E': return 0xFFFF;   // visor shine
        case 'F': return 0xFD20;   // orange boots
        default:  return 0;
    }
}

// The sprite centred on (cx, cy) on screen, above clipTop.
inline void draw(GFXcanvas16 &c, int cx, int cy, PlayerMaze::Facing f, uint8_t frame, int clipTop) {
    const char* const* rows = f == PlayerMaze::FACE_DOWN ? DOWN[frame & 1]
                            : f == PlayerMaze::FACE_UP   ? UP[frame & 1] : RIGHT[frame & 1];
    const bool mirror = f == PlayerMaze::FACE_LEFT;
    const int x0 = cx - 5, y0 = cy - 5;
    for (int j = 0; j < 10; j++) {
        const int y = y0 + j;
        if (y < clipTop || y >= c.height()) continue;
        for (int i = 0; i < 10; i++) {
            const char ch = rows[j][mirror ? 9 - i : i];
            if (ch == '.') continue;
            const int x = x0 + i;
            if (x < 0 || x >= c.width()) continue;
            c.drawPixel(x, y, colourOf(ch));
        }
    }
}

} // namespace mazesprite

#endif // MAZE_PLAYER_SPRITE_H

// Host simulation of the whole cabinet: src/main.cpp itself (setup() and
// loop()), with the real launcher, input handling and every game, driven
// through the input pins the stubs expose. It launches each game from the
// menu, lets it run, quits it by holding B, and checks the game was built
// and freed again (ASan reports any leak or double free). Then it checks the
// menu scrolls, with more games than fit.
#include <new>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <vector>
#include <memory>
#include <array>
#include <functional>

unsigned long g_fakeMillis = 1000;
static uint32_t g_rng = 12345;
static uint32_t lcg() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
long random(long hi) { return hi <= 0 ? 0 : (long)(lcg() % (uint32_t)hi); }
long random(long lo, long hi) { return hi <= lo ? lo : lo + (long)(lcg() % (uint32_t)(hi - lo)); }
void randomSeed(unsigned long s) { g_rng = (uint32_t)s; }

#define private public
#include "../src/main.cpp"
#undef private

static void frames(int n) { for (int i = 0; i < n; ++i) { g_fakeMillis += 17; loop(); } }
static void setPin(int pin, bool down) { g_pinLevel[pin] = down ? LOW : HIGH; }

// Scroll the menu to entry `idx` the way a player does: nudge the stick,
// let it return to centre, repeat (the menu's "up" is the next entry).
static void selectGame(int idx) {
    for (int guard = 0; launcher._selection != idx && guard < 40; ++guard) {
        g_analogLevel[ArcadeConfig::JOY_Y] = 0;
        frames(8);
        g_analogLevel[ArcadeConfig::JOY_Y] = 2048;
        frames(8);
    }
}

int main() {
    bool ok = true;
    // What each game takes while it runs (its object, built on launch; the
    // 3D games' scenes come on top, from the heap, and go on exit).
    printf("game object sizes: Asteroids %zu, Lander %zu, Maze %zu, Runner %zu, Tank %zu, Tube %zu bytes\n",
           sizeof(AsteroidFluxGame), sizeof(LanderFluxGame), sizeof(MazeFluxGame),
           sizeof(PlatformFluxGame), sizeof(TankFluxGame), sizeof(TubeFluxGame));
    setup();
    frames(30);

    for (int i = 0; i < GAME_COUNT; ++i) {
        selectGame(i);
        const bool selected = launcher._selection == i;
        const bool visible = i >= launcher._top && i < launcher._top + LauncherMenu::VISIBLE_ROWS;
        setPin(ArcadeConfig::BUTTON_A, true);  frames(3);
        setPin(ArcadeConfig::BUTTON_A, false); frames(3);
        const bool launched = cabinetState == STATE_IN_GAME && activeGame && activeGameMem;
        const long pushes0 = tft.frames;
        frames(900);                                     // ~15s: title, info, into a demo
        const long pushed = tft.frames - pushes0;
        // Quit: hold B (Tank's attract screen and every title take it).
        setPin(ArcadeConfig::BUTTON_B, true);  frames(160);
        setPin(ArcadeConfig::BUTTON_B, false); frames(10);
        const bool back = cabinetState == STATE_LAUNCHER_MENU && !activeGame && !activeGameMem &&
                          launcher._selection == i;
        const bool pass = selected && visible && launched && pushed >= 850 && pushed <= 950 && back;
        printf("%-10s selected %d visible %d launched %d frames pushed %ld back %d -> %s\n",
               gameRegistry[i].name, selected, visible, launched, pushed, back, pass ? "PASS" : "FAIL");
        ok &= pass;
    }

    // Scrolling: 10 entries, 6 rows. Walking down then wrapping round must
    // keep the selection on screen and the window within the list.
    static const GameEntry many[10] = {
        {"G0", nullptr}, {"G1", nullptr}, {"G2", nullptr}, {"G3", nullptr}, {"G4", nullptr},
        {"G5", nullptr}, {"G6", nullptr}, {"G7", nullptr}, {"G8", nullptr}, {"G9", nullptr} };
    LauncherMenu menu;
    menu.setGames(many, 10);
    GFXcanvas16 c(128, 160);
    InputState idle{};
    bool scrollOk = true;
    int maxTop = 0;
    for (int step = 0; step < 25; ++step) {
        InputState up{}; up.joyUp = true;
        menu.update(c, up, audio);
        menu.update(c, idle, audio);
        const bool inView = menu._selection >= menu._top && menu._selection < menu._top + 6;
        scrollOk &= inView && menu._top >= 0 && menu._top <= 4;
        maxTop = std::max(maxTop, menu._top);
    }
    scrollOk &= maxTop == 4;
    printf("scrolling: 10 entries, window reached %d (want 4), selection always on screen -> %s\n",
           maxTop, scrollOk ? "PASS" : "FAIL");
    ok &= scrollOk;
    return ok ? 0 : 1;
}

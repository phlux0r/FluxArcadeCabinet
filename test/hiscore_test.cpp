// Host test for the cabinet's high-score tables and name entry
// (src/cabinet/HighScores.h): storage and carrying over old scores,
// ranking, the entry controls in both orientations, and the timeout.

#include "harness_common.h"
#include "cabinet/HighScores.h"

using namespace hiscore;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

// Feeds one frame of input to the entry; the fake clock moves 33ms.
bool frame(ScoreBoard &s, uint8_t rot, bool up = false, bool down = false, bool left = false, bool right = false,
           bool a = false, bool b = false) {
    InputState in{};
    if (rot == 1) { in.joyLeft = up; in.joyRight = down; in.joyUp = left; in.joyDown = right; }
    else          { in.joyDown = up; in.joyUp = down;    in.joyLeft = left; in.joyRight = right; }
    in.btnA = a; in.btnB = b;
    bool done = s.update(in, rot);
    g_fakeMillis += 33;
    return done;
}
// A push of the stick or a button: on for a frame, then off for one.
bool tap(ScoreBoard &s, uint8_t rot, int what) {
    bool done = frame(s, rot, what == 0, what == 1, what == 2, what == 3, what == 4, what == 5);
    return frame(s, rot) || done;
}
enum { UP, DOWN, LEFT, RIGHT, A, B };
}  // namespace

int main() {
    // An old single high score carries over as the table's first entry.
    {
        Preferences old;
        old.begin("tb_data", false);
        old.putInt("highscore", 5000);
        old.end();
        Table t;
        load("tube", t);
        check(t.e[0].score == 5000 && strcmp(t.e[0].name, "---") == 0 && t.e[1].score == 0,
              "old high score carried over as ---");
        Table again;
        load("tube", again);
        check(memcmp(&t, &again, sizeof(t)) == 0, "table saved and read back the same");
    }

    // Ranking: must beat an entry outright; ties stay below.
    {
        Table t;
        clear(t);
        insert(t, "AAA", 300);
        insert(t, "BBB", 500);
        insert(t, "CCC", 100);
        check(t.e[0].score == 500 && t.e[1].score == 300 && t.e[2].score == 100, "inserted in order");
        check(rankFor(t, 300) == 2, "a tie goes below the score it ties");
        check(rankFor(t, 0) == -1, "zero never makes the table");
        insert(t, "DDD", 50); insert(t, "EEE", 40);
        check(rankFor(t, 40) == -1 && rankFor(t, 41) == 4, "full table: must beat the last");
        insert(t, "FFF", 1000);
        check(t.e[0].score == 1000 && t.e[4].score == 50, "top score pushes the last off");
    }

    // Entry, landscape: up twice (C), A, A, down (wraps to '.'), A.
    {
        ScoreBoard s;
        s.begin("star");
        check(s.offer(1234) && s.entering() && strcmp(s.name(), "AAA") == 0, "offer starts entry on AAA");
        tap(s, 1, UP); tap(s, 1, UP);
        tap(s, 1, A); tap(s, 1, A);
        tap(s, 1, DOWN);
        bool done = tap(s, 1, A);
        check(done && !s.entering() && strcmp(s.table().e[0].name, "CA.") == 0 && s.table().e[0].score == 1234,
              "landscape entry: CA. saved at #1");
        char last[4];
        loadLastName(last);
        check(strcmp(last, "CA.") == 0, "last name remembered");
    }

    // Entry, portrait: starts on the last name; right, up, B back, up, A x3.
    {
        ScoreBoard s;
        s.begin("maze");
        s.offer(77);
        check(strcmp(s.name(), "CA.") == 0, "entry starts on the last name");
        tap(s, 2, RIGHT);          // slot 1
        tap(s, 2, UP);             // A -> B
        tap(s, 2, B);              // back to slot 0
        tap(s, 2, UP);             // C -> D
        tap(s, 2, A); tap(s, 2, A);
        bool done = tap(s, 2, A);
        check(done && strcmp(s.table().e[0].name, "DB.") == 0, "portrait entry with B back: DB.");
    }

    // Holding the stick runs the letters.
    {
        ScoreBoard s;
        s.begin("lander");
        s.offer(10);
        for (int i = 0; i < 30; ++i) frame(s, 1, true);   // ~1s held up
        frame(s, 1);
        const char* at = strchr(CHARS, s.name()[0]);
        int moved = (int)(at - strchr(CHARS, 'D'));
        check(moved >= 5 && moved <= 8, "held stick repeats (5-8 steps in ~1s)");
    }

    // Timeout: nothing touched, the last name goes in.
    {
        ScoreBoard s;
        s.begin("runner");
        s.offer(999);
        bool done = false;
        long frames = 0;
        while (!done && frames < 2000) { done = frame(s, 1); ++frames; }
        const long ms = frames * 33;
        check(done && strcmp(s.table().e[0].name, "DB.") == 0, "timeout saves the last name");
        check(ms >= (long)ENTRY_TIMEOUT_MS && ms < (long)ENTRY_TIMEOUT_MS + 100, "timeout after ENTRY_TIMEOUT_MS idle");
    }

    // Quitting mid-game: in quietly, under the last name; low scores ignored.
    {
        ScoreBoard s;
        s.begin("asteroids");
        s.record(400);
        check(s.table().e[0].score == 400 && strcmp(s.table().e[0].name, "DB.") == 0, "record() uses the last name");
        check(!s.offer(0), "no entry for zero");
    }

    // The buttons that ended a game don't confirm anything.
    {
        ScoreBoard s;
        s.begin("tank");
        s.offer(50);
        frame(s, 1, false, false, false, false, true);   // A still held from play
        check(s.entering() && strcmp(s.name(), "DB.") == 0, "held A from the game ignored");
    }

    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

// PocketLife - a little life sim for the Nintendo DS
//   top screen:    3D lot, switchable between top-down and 2.5D isometric
//   bottom screen: needs, actions, buy and build modes (touch + buttons)
#include "game.h"
#include <stdlib.h>

int ui_title(void);
void ui_cas(void);
void ui_game(void);

int main(void) {
    defaultExceptionHandler();
    lcdMainOnTop();
    videoSetMode(MODE_0_3D);
    render_init();
    ui_init();
    save_init();
    srand(1234);

    world_newGame();
    int choice = ui_title();
    srand(W.minutes + choice * 77 + REG_VCOUNT);
    if (choice == 1 && save_read()) {
        world_rebuildOcc();
        notify("Welcome back!");
    } else {
        world_newGame();
        ui_cas();
    }
    OPT.speed = 1;
    ui_game();
    return 0;
}

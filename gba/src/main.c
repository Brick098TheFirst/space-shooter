#include <tonc.h>
#include "types.h"
#include "renderer.h"
#include "audio.h"
#include "save.h"
#include "starfield.h"
#include "game.h"
#include "menu.h"

int main(void) {
    // Enable interrupts
    irq_init(NULL);
    irq_enable(II_VBLANK);

    // Initialize systems
    gfx_init();
    audio_init();
    save_load();
    starfield_init();
    game_init();
    menu_init();

    // Main Game Loop
    while (1) {
        menu_update();
        menu_draw();
        audio_update();
        gfx_flip();
    }

    return 0;
}

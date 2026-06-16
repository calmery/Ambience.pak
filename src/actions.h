/* User actions on the App state, dispatched from the input loop. */
#ifndef AMBIENCE_ACTIONS_H
#define AMBIENCE_ACTIONS_H

#include "app.h"

enum {
    ACT_UP = 0, ACT_DOWN, ACT_DEC, ACT_INC,
    ACT_PAUSE, ACT_SLEEP, ACT_QUIT, ACT_MUTE
};

/* Run one action. ACT_QUIT sets *confirm instead of quitting directly. */
void act_apply(App *a, int code, int *confirm);

#endif /* AMBIENCE_ACTIONS_H */

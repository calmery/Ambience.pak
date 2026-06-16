/* Presets + persistence (ambience.cfg).
 *
 * The file holds one or more named presets; only `active` is editable today.
 * A future tab strip in ui.c will let the user switch `a->active`, then call
 * config_apply_active() to load that preset's mix onto the channels.
 *
 * File format:
 *   active <index>
 *   preset <name>
 *   vol <level> <muted> <channel-name>
 *   ... (more vol lines, then more preset blocks)
 */
#ifndef AMBIENCE_CONFIG_H
#define AMBIENCE_CONFIG_H

#include "app.h"

void config_load(App *a);          /* read presets; ensures a "Default" */
void config_apply_active(App *a);  /* apply active preset onto channels  */
void config_save(App *a);          /* capture channels into active, write */

#endif /* AMBIENCE_CONFIG_H */

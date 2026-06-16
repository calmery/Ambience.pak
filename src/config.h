/* Presets + persistence (ambience.cfg).
 *
 * The file holds one or more named presets. The active preset is editable;
 * volume/mute edits are captured into it. The tab strip in ui.c switches the
 * active preset.
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

void config_load(App *a);            /* read presets; ensures a "Default" */
void config_apply_active(App *a);    /* apply active preset onto channels  */
void config_capture_active(App *a);  /* live channel mix -> active preset  */
void config_save(App *a);            /* capture + write file               */

/* preset operations (mark the app dirty; persisted on save) */
void config_switch(App *a, int idx);                 /* capture, activate, apply */
int  config_new(App *a, const char *name);           /* copy current mix; -1 if full */
void config_rename(App *a, int idx, const char *name);
void config_delete(App *a, int idx);                 /* keeps at least one preset */

#endif /* AMBIENCE_CONFIG_H */

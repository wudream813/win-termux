#ifndef WIN_TERMUX_CONFIG_H
#define WIN_TERMUX_CONFIG_H

#include "common.h"
#include "types.h"

extern ChooserItem g_chooser_items[MAX_CHOOSER_ITEMS];
extern int g_chooser_item_count;

extern int g_settings_nav;
extern int g_settings_field;
extern int g_settings_table_sel;
extern int g_default_startup;
extern int g_settings_show_presets;
extern int g_preset_sel;

extern char g_edit_name[32];
extern int g_edit_name_len, g_edit_name_pos;
extern char g_edit_cmd[256];
extern int g_edit_cmd_len, g_edit_cmd_pos;
extern char g_edit_dir[256];
extern int g_edit_dir_len, g_edit_dir_pos;

extern const ChooserItem g_presets[];
extern const int g_preset_count;

void init_default_config(void);
void load_config(void);
void save_config(void);
void load_item_to_editor(int idx);
void save_editor_to_item(int idx);

#endif // WIN_TERMUX_CONFIG_H

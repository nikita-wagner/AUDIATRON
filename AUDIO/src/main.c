// ═══════════════════════════════════════════════════════════════════════════
//  main.c — AUDIATRON entry point
//
//  Parses the command line and hands over to one of the modes in app/.
//  There is no logic here on purpose; see app/app.h for the map of the
//  program and README.md for the map of the folders.
//
//  audiatron --help lists the options.
// ═══════════════════════════════════════════════════════════════════════════

#include "app/app.h"
#include "ui/console_text/console_text.h"

static AT_App g_app;        // tens of KB (engine + audio thread scratch) — static, not on the stack

int main(int argc, char** argv) {
    console_text_init();

    if (!app_parse_args(&g_app.options, argc, argv)) return 1;

    switch (g_app.options.mode) {
    case AT_APP_MODE_LIST:    return app_run_list(&g_app);
    case AT_APP_MODE_INFO:    return app_run_info(&g_app);
    case AT_APP_MODE_OFFLINE: return app_run_offline(&g_app);
    default:                  return app_run_live(&g_app);
    }
}

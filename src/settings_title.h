/*
 * settings_title.h -- Sid Meier's Pirates!' own lines in pirates.ini.
 *
 * Included by the toolkit's settings writer (xboxrecomp/src/config/
 * recomp_config.c, RECOMP_TITLE_SETTINGS_HEADER from CMakeLists.txt), which
 * appends these after its own lines and keeps the value each key already
 * has in the file, so a save from the launcher does not drop them. The game
 * reads them with recomp_config_bool/_lookup, and the environment variable
 * named in each comment still overrides the file.
 */
#ifndef PIRATES_SETTINGS_TITLE_H
#define PIRATES_SETTINGS_TITLE_H

static const struct {
    const char *key;
    const char *def;
    const char *comment;
} recomp_title_settings[] = {
    { "skip_intro", "1",
      "# ---- Sid Meier's Pirates! ----\n"
      "#\n"
      "# Movies. 1 skips every movie (the 2K and Firaxis logos, the title movie\n"
      "# and the story movies of a new game); intro skips only the logos and\n"
      "# the title movie; 0 plays them all, as the disc does.\n"
      "#                                         [RECOMP_PIRATES_SKIP_INTRO]\n" },
    { "minigames", "0",
      "# The developers' MINIGAMES (*DEBUG!*) entry in the main menu: start a\n"
      "# sword duel, dancing, the town sneak, a land battle and the rest\n"
      "# directly. 0 hides it, as the retail game does.\n"
      "#                                          [RECOMP_PIRATES_MINIGAMES]\n" },
    { "mute", "0",
      "# Play with no sound (the game still runs its audio).  [RECOMP_MUTE]\n" },
    { "guest_lock", "1",
      "# ---- Debugging: leave these alone unless asked ----\n"
      "#\n"
      "# Run one game thread at a time, as on the console's single CPU. 0 lets\n"
      "# them run in parallel, which froze or crashed about half of the visits\n"
      "# to a governor.                                   [RECOMP_GUEST_LOCK]\n" },
    { "log_buffer", "1",
      "# Buffer the log. 0 writes every line at once: much slower loading, but\n"
      "# a crash keeps every line before it.       [RECOMP_PIRATES_LOG_BUFFER]\n" },
    { "cache_pause", "0",
      "# 1 pauses the disc-to-cache copy while movies would play, as the\n"
      "# console does; with movies skipped that froze a first load on a fresh\n"
      "# save folder.                              [RECOMP_PIRATES_CACHE_PAUSE]\n" },
};

#endif

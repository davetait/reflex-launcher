// Stand-ins for globals defined in launcher.c, so library code can be unit tested
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include "launcher.h"

Config config;
FILE *log_file = NULL;
Hotkey *hotkeys = NULL;
GamepadControl *gamepad_controls = NULL;
#ifdef _WIN32
SDL_SysWMinfo wm_info;
#endif

void quit(int status)
{
    exit(status);
}

void print_version(FILE *stream)
{
    fputs("reflex-tests\n", stream);
}

#include "../serial_menu.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { assert(condition); ++checks; } while (0)

static serial_menu_result_t input(serial_menu_t *menu, const char *line)
{
    static char buffer[128];
    CHECK(strlen(line) < sizeof(buffer));
    strcpy(buffer, line);
    return serial_menu_route(menu, serial_menu_normalise(buffer));
}

static void expect_command(serial_menu_t *menu, const char *line, const char *command)
{
    serial_menu_t before = *menu;
    serial_menu_result_t result = input(menu, line);
    CHECK(result.action == SERIAL_MENU_COMMAND);
    CHECK(result.text && !strcmp(result.text, command));
    CHECK(*menu == before);
}

static void shortcuts_and_return(void)
{
    const char *shortcuts[] = {"M", "P", "A", "W", "K", "D", "L"};
    // Every shortcut works from every section, including lower case input.
    for (int from = 0; from < SERIAL_MENU_COUNT; ++from) {
        for (int to = 0; to <= SERIAL_MENU_LOGS; ++to) {
            serial_menu_t menu = (serial_menu_t)from;
            serial_menu_result_t result = input(&menu, shortcuts[to]);
            CHECK(result.action == SERIAL_MENU_SHOW);
            CHECK(from == SERIAL_MENU_INSPECTION ? result.text && !strcmp(result.text, "screen live") : !result.text);
            CHECK(menu == to);
            CHECK(strlen(serial_menu_prompt(menu)) > 0);
            result = input(&menu, "0");
            CHECK(result.action == SERIAL_MENU_SHOW && menu == SERIAL_MENU_MAIN);
        }
    }
    serial_menu_t menu = SERIAL_MENU_AUDIO;
    CHECK(input(&menu, "\t \xc2\xa0 w \t").action == SERIAL_MENU_SHOW);
    CHECK(menu == SERIAL_MENU_WIFI);
    // Opening W must never run config or consume credentials.
    CHECK(input(&menu, "w").action == SERIAL_MENU_SHOW);
    CHECK(input(&menu, "m").action == SERIAL_MENU_SHOW && menu == SERIAL_MENU_MAIN);
}

static void existing_commands_in_every_section(void)
{
    const struct { const char *line, *expected; } cases[] = {
        {"help", "help"}, {"H", "h"}, {"status", "status"}, {"CONFIG", "config"},
        {"WiFi\tCONFIG", "config"}, {"WiFi", "help"}, {"wifi status", "status"},
        {"scan", "scan"}, {"connect", "connect"}, {"check", "check"},
        {"reconnect-test", "reconnect-test"}, {"forget", "forget"},
        {"moonraker", "moonraker"}, {"I", "i"},
        {"  Moonraker   SET Printer.Local  7125  ", "moonraker set printer.local 7125"},
        {"diag on", "diag on"}, {"diag off", "diag off"},
        {"panel help", "panel help"}, {"panel status", "panel status"},
        {"panel files", "panel files"}, {"panel macros", "panel macros"},
        {"panel objects", "panel objects"}, {"panel metadata", "panel metadata"},
        {"panel test normal", "panel test normal"}, {"panel test uncertain", "panel test uncertain"},
        {"audio status", "audio status"}, {"audio on", "audio on"}, {"audio off", "audio off"},
        {"audio volume 75", "audio volume 75"}, {"audio test", "audio test"},
        {"audio stop", "audio stop"}, {"audio sounds", "audio sounds"},
        {"audio play wakeup", "audio play wakeup"}, {"audio events", "audio events"},
    };
    for (int from = 0; from < SERIAL_MENU_COUNT; ++from) {
        serial_menu_t menu = (serial_menu_t)from;
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            serial_menu_result_t result = input(&menu, cases[i].line);
            CHECK(result.action == SERIAL_MENU_PASS);
            CHECK(result.text && !strcmp(result.text, cases[i].expected));
            CHECK(menu == from);
        }
    }
}

static void numbered_flows(void)
{
    serial_menu_t menu = SERIAL_MENU_MAIN;
    input(&menu, "P");
    expect_command(&menu, "1", "status");
    expect_command(&menu, "2", "diag on");
    expect_command(&menu, "3", "diag off");
    CHECK(input(&menu, "4").action == SERIAL_MENU_SHOW && menu == SERIAL_MENU_INSPECTION);
    CHECK(input(&menu, "0").action == SERIAL_MENU_SHOW && menu == SERIAL_MENU_DISPLAY);
    CHECK(input(&menu, "5").action == SERIAL_MENU_NOTICE);

    input(&menu, "A");
    expect_command(&menu, "1", "audio status");
    expect_command(&menu, "2", "audio on");
    expect_command(&menu, "3", "audio off");
    CHECK(input(&menu, "4").action == SERIAL_MENU_NOTICE);
    expect_command(&menu, "5", "audio test");
    expect_command(&menu, "6", "audio sounds");
    CHECK(input(&menu, "7").action == SERIAL_MENU_NOTICE);
    expect_command(&menu, "8", "audio stop");
    expect_command(&menu, "9", "audio events");

    input(&menu, "W");
    expect_command(&menu, "1", "status");
    expect_command(&menu, "2", "scan");
    expect_command(&menu, "3", "config");
    expect_command(&menu, "4", "connect");
    expect_command(&menu, "5", "check");
    expect_command(&menu, "6", "reconnect-test");
    serial_menu_result_t result = input(&menu, "7");
    CHECK(result.action == SERIAL_MENU_CONFIRM_FORGET && !result.text);
    CHECK(menu == SERIAL_MENU_WIFI);
    CHECK(input(&menu, "M").action == SERIAL_MENU_SHOW && menu == SERIAL_MENU_MAIN);

    input(&menu, "K");
    expect_command(&menu, "1", "status");
    expect_command(&menu, "2", "moonraker");
    expect_command(&menu, "3", "panel status");

    input(&menu, "D");
    CHECK(input(&menu, "1").action == SERIAL_MENU_NOTICE);
    expect_command(&menu, "2", "status");
    expect_command(&menu, "3", "panel status");
    expect_command(&menu, "4", "audio status");

    input(&menu, "L");
    expect_command(&menu, "1", "diag on");
    expect_command(&menu, "2", "diag off");
    CHECK(input(&menu, "3").action == SERIAL_MENU_NOTICE);
    CHECK(input(&menu, "0").action == SERIAL_MENU_SHOW && menu == SERIAL_MENU_MAIN);
}

static void inspection_flow(void)
{
    const char *names[] = {"startup", "standby", "printing", "paused", "toolhead",
        "files", "macros", "exclude", "settings", "error", "details"};
    serial_menu_t menu = SERIAL_MENU_DISPLAY;
    CHECK(input(&menu, "4").action == SERIAL_MENU_SHOW && menu == SERIAL_MENU_INSPECTION);
    for (unsigned i = 0; i < 11; ++i) {
        char key[8], command[64];
        snprintf(key, sizeof(key), "%u", i + 1);
        snprintf(command, sizeof(command), "screen show %s", names[i]);
        expect_command(&menu, key, command);
    }
    expect_command(&menu, "12", "screen live");
    expect_command(&menu, "13", "screen status");
    serial_menu_result_t result = input(&menu, "0");
    CHECK(menu == SERIAL_MENU_DISPLAY && result.action == SERIAL_MENU_SHOW);
    CHECK(result.text && !strcmp(result.text, "screen live"));
    result = input(&menu, "screen show printing");
    CHECK(result.action == SERIAL_MENU_PASS && menu == SERIAL_MENU_INSPECTION);
    result = input(&menu, "M");
    CHECK(menu == SERIAL_MENU_MAIN && result.text && !strcmp(result.text, "screen live"));
}

static void invalid_and_empty_input(void)
{
    const char *invalid[] = {"00", "01", "14", "999999999999999999", "1 x", "3config"};
    for (int from = 0; from < SERIAL_MENU_COUNT; ++from) {
        serial_menu_t menu = (serial_menu_t)from;
        for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            serial_menu_result_t result = input(&menu, invalid[i]);
            CHECK(result.action == SERIAL_MENU_INVALID && !result.text);
            CHECK(menu == from);
        }
        CHECK(input(&menu, " \t ").action == SERIAL_MENU_SHOW && menu == from);
        serial_menu_result_t result = input(&menu, "unknown command");
        CHECK(result.action == SERIAL_MENU_PASS && menu == from);
        CHECK(!strcmp(result.text, "unknown command"));
    }
    serial_menu_t menu = SERIAL_MENU_MAIN;
    CHECK(input(&menu, "1").action == SERIAL_MENU_INVALID);
    CHECK(serial_menu_route(NULL, "m").action == SERIAL_MENU_INVALID);
    CHECK(serial_menu_route(&menu, NULL).action == SERIAL_MENU_INVALID);
    menu = SERIAL_MENU_COUNT;
    CHECK(serial_menu_route(&menu, "m").action == SERIAL_MENU_INVALID);
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--menus")) {
        for (int i = 0; i < SERIAL_MENU_COUNT; ++i) {
            serial_menu_print((serial_menu_t)i);
            printf("%s\n", serial_menu_prompt((serial_menu_t)i));
        }
        return 0;
    }
    shortcuts_and_return();
    existing_commands_in_every_section();
    numbered_flows();
    inspection_flow();
    invalid_and_empty_input();
    printf("PASS: %u checks; navigation, numbered flows, legacy commands and invalid input.\n", checks);
    return 0;
}

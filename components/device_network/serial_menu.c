#include "serial_menu.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *key, *label;
    serial_menu_action_t action;
    const char *text;
} menu_entry_t;

typedef struct {
    char shortcut;
    const char *title, *prompt;
    const menu_entry_t *entries;
    size_t count;
} menu_definition_t;

static const menu_entry_t display_entries[] = {
    {"1", "Consultar brillo y reposo guardados", SERIAL_MENU_COMMAND, "status"},
    {"2", "Activar trazas de pantalla y tactil", SERIAL_MENU_COMMAND, "diag on"},
    {"3", "Desactivar trazas de pantalla y tactil", SERIAL_MENU_COMMAND, "diag off"},
    {"4", "Inspeccionar vistas (solo lectura)", SERIAL_MENU_OPEN_INSPECTION, NULL},
    {"5", "Prueba de coordenadas del tactil [pendiente]", SERIAL_MENU_NOTICE,
        "Prueba dedicada del tactil pendiente. Por ahora se pueden activar las trazas con la opcion 2."},
};
static const menu_entry_t inspection_entries[] = {
    {"1", "Arranque", SERIAL_MENU_COMMAND, "screen show startup"},
    {"2", "Espera / completado / cancelado (estado real)", SERIAL_MENU_COMMAND, "screen show standby"},
    {"3", "Impresion", SERIAL_MENU_COMMAND, "screen show printing"},
    {"4", "Pausa", SERIAL_MENU_COMMAND, "screen show paused"},
    {"5", "Toolhead", SERIAL_MENU_COMMAND, "screen show toolhead"},
    {"6", "Archivos G-code", SERIAL_MENU_COMMAND, "screen show files"},
    {"7", "Macros", SERIAL_MENU_COMMAND, "screen show macros"},
    {"8", "Objetos", SERIAL_MENU_COMMAND, "screen show exclude"},
    {"9", "Ajustes", SERIAL_MENU_COMMAND, "screen show settings"},
    {"10", "Error", SERIAL_MENU_COMMAND, "screen show error"},
    {"11", "Detalles de impresion", SERIAL_MENU_COMMAND, "screen show details"},
    {"12", "Volver al funcionamiento normal", SERIAL_MENU_COMMAND, "screen live"},
    {"13", "Estado de la inspeccion", SERIAL_MENU_COMMAND, "screen status"},
};
static const menu_entry_t audio_entries[] = {
    {"1", "Estado del audio", SERIAL_MENU_COMMAND, "audio status"},
    {"2", "Activar sonido", SERIAL_MENU_COMMAND, "audio on"},
    {"3", "Desactivar sonido", SERIAL_MENU_COMMAND, "audio off"},
    {"4", "Como ajustar el volumen", SERIAL_MENU_NOTICE, "Escriba: audio volume N (0..100)."},
    {"5", "Reproducir tono de prueba", SERIAL_MENU_COMMAND, "audio test"},
    {"6", "Listar voces disponibles", SERIAL_MENU_COMMAND, "audio sounds"},
    {"7", "Como reproducir una voz", SERIAL_MENU_NOTICE,
        "Escriba: audio play NOMBRE. Nombres: wakeup, printing, pause, cancel, error, complete, standby."},
    {"8", "Detener audio y vaciar pendientes", SERIAL_MENU_COMMAND, "audio stop"},
    {"9", "Diagnostico de eventos de voz", SERIAL_MENU_COMMAND, "audio events"},
};
static const menu_entry_t wifi_entries[] = {
    {"1", "Estado de red e IP local", SERIAL_MENU_COMMAND, "status"},
    {"2", "Contar redes disponibles (sin mostrar SSID)", SERIAL_MENU_COMMAND, "scan"},
    {"3", "Configurar red (lista y clave oculta)", SERIAL_MENU_COMMAND, "config"},
    {"4", "Conectar con la red guardada", SERIAL_MENU_COMMAND, "connect"},
    {"5", "Comprobar gateway y DNS/TCP externo", SERIAL_MENU_COMMAND, "check"},
    {"6", "Probar desconexion y reconexion WiFi", SERIAL_MENU_COMMAND, "reconnect-test"},
    {"7", "Borrar red guardada (pide confirmacion)", SERIAL_MENU_CONFIRM_FORGET, NULL},
};
static const menu_entry_t klipper_entries[] = {
    {"1", "Consultar host/IP y puerto guardados", SERIAL_MENU_COMMAND, "status"},
    {"2", "Configurar host/IP y puerto de Moonraker", SERIAL_MENU_COMMAND, "moonraker"},
    {"3", "Estado de Moonraker y Klipper", SERIAL_MENU_COMMAND, "panel status"},
};
static const menu_entry_t system_entries[] = {
    {"1", "Version, memoria y motivo de reinicio [pendiente]", SERIAL_MENU_NOTICE,
        "Diagnostico del sistema pendiente de implementar en la siguiente etapa."},
    {"2", "Estado de red y configuracion", SERIAL_MENU_COMMAND, "status"},
    {"3", "Estado de Klipper", SERIAL_MENU_COMMAND, "panel status"},
    {"4", "Estado del audio", SERIAL_MENU_COMMAND, "audio status"},
};
static const menu_entry_t log_entries[] = {
    {"1", "Activar trazas GUI/tactil", SERIAL_MENU_COMMAND, "diag on"},
    {"2", "Desactivar trazas GUI/tactil (nivel normal)", SERIAL_MENU_COMMAND, "diag off"},
    {"3", "Niveles por modulo [pendiente]", SERIAL_MENU_NOTICE,
        "Control de niveles por modulo pendiente. Las opciones 1 y 2 usan las trazas GUI existentes."},
};

#define MENU(shortcut_, title_, prompt_, entries_) \
    {shortcut_, title_, prompt_, entries_, sizeof(entries_) / sizeof((entries_)[0])}
static const menu_definition_t menus[SERIAL_MENU_COUNT] = {
    [SERIAL_MENU_MAIN] = {'m', "MENU PRINCIPAL", "panel> ", NULL, 0},
    [SERIAL_MENU_DISPLAY] = MENU('p', "PANTALLAS Y TACTIL", "pantallas> ", display_entries),
    [SERIAL_MENU_AUDIO] = MENU('a', "AUDIO", "audio> ", audio_entries),
    [SERIAL_MENU_WIFI] = MENU('w', "WIFI", "wifi> ", wifi_entries),
    [SERIAL_MENU_KLIPPER] = MENU('k', "IP Y CONEXION DE KLIPPER", "klipper> ", klipper_entries),
    [SERIAL_MENU_SYSTEM] = MENU('d', "DIAGNOSTICO DEL SISTEMA", "diagnostico> ", system_entries),
    [SERIAL_MENU_LOGS] = MENU('l', "LOGS", "logs> ", log_entries),
    [SERIAL_MENU_INSPECTION] = MENU(0, "INSPECCION DE PANTALLAS", "inspeccion> ", inspection_entries),
};
#undef MENU

const char *serial_menu_normalise(char *command)
{
    // Credential and host prompts continue to use the raw USB reader directly.
    size_t written = 0;
    for (size_t read = 0; command[read]; ++read) {
        unsigned char byte = (unsigned char)command[read];
        if (byte == 0xc2 && (unsigned char)command[read + 1] == 0xa0) {
            byte = ' ';
            ++read;
        }
        if (byte == ' ' || byte == '\t') {
            if (written && command[written - 1] != ' ') command[written++] = ' ';
        } else {
            if (byte >= 'A' && byte <= 'Z') byte += 'a' - 'A';
            command[written++] = (char)byte;
        }
    }
    if (written && command[written - 1] == ' ') --written;
    command[written] = 0;
    if (!strcmp(command, "wifi")) return "help";
    return !strncmp(command, "wifi ", 5) ? command + 5 : command;
}

serial_menu_result_t serial_menu_route(serial_menu_t *menu, const char *command)
{
    if (!menu || !command || *menu < SERIAL_MENU_MAIN || *menu >= SERIAL_MENU_COUNT)
        return (serial_menu_result_t){SERIAL_MENU_INVALID, NULL};
    if (!*command) return (serial_menu_result_t){SERIAL_MENU_SHOW, NULL};
    if (!strcmp(command, "0")) {
        if (*menu == SERIAL_MENU_INSPECTION) {
            *menu = SERIAL_MENU_DISPLAY;
            return (serial_menu_result_t){SERIAL_MENU_SHOW, "screen live"};
        }
        *menu = SERIAL_MENU_MAIN;
        return (serial_menu_result_t){SERIAL_MENU_SHOW, NULL};
    }
    if (!command[1]) {
        for (size_t i = 0; i <= SERIAL_MENU_LOGS; ++i) {
            if (command[0] == menus[i].shortcut) {
                bool leaving = *menu == SERIAL_MENU_INSPECTION;
                *menu = (serial_menu_t)i;
                return (serial_menu_result_t){SERIAL_MENU_SHOW, leaving ? "screen live" : NULL};
            }
        }
    }
    const menu_definition_t *definition = &menus[*menu];
    for (size_t i = 0; i < definition->count; ++i) {
        const menu_entry_t *entry = &definition->entries[i];
        if (!strcmp(command, entry->key)) {
            if (entry->action == SERIAL_MENU_OPEN_INSPECTION) {
                *menu = SERIAL_MENU_INSPECTION;
                return (serial_menu_result_t){SERIAL_MENU_SHOW, NULL};
            }
            return (serial_menu_result_t){entry->action, entry->text};
        }
    }
    // An invalid numeric selection must never become a service command.
    if (command[0] >= '0' && command[0] <= '9')
        return (serial_menu_result_t){SERIAL_MENU_INVALID, NULL};
    if (!strncmp(command, "screen show ", 12)) *menu = SERIAL_MENU_INSPECTION;
    return (serial_menu_result_t){SERIAL_MENU_PASS, command};
}

void serial_menu_print(serial_menu_t menu)
{
    if (menu < SERIAL_MENU_MAIN || menu >= SERIAL_MENU_COUNT) return;
    const menu_definition_t *definition = &menus[menu];
    printf("\n========== %s ==========\n", definition->title);
    if (menu == SERIAL_MENU_MAIN) {
        for (size_t i = 0; i <= SERIAL_MENU_LOGS; ++i)
            printf("%c  %s\n", menus[i].shortcut - 'a' + 'A', menus[i].title);
    } else {
        for (size_t i = 0; i < definition->count; ++i)
            printf("%s  %s\n", definition->entries[i].key, definition->entries[i].label);
        printf("0  %s\n", menu == SERIAL_MENU_INSPECTION
            ? "Volver a Pantallas y terminar inspeccion" : "Volver al menu principal");
    }
    if (menu == SERIAL_MENU_INSPECTION)
        printf("Datos reales; controles tactiles bloqueados. 0/M u otra seccion termina inspeccion.\n");
    printf("Letra o numero + Enter. M/P/A/W/K/D/L cambian de menu.\n"
           "Comandos completos disponibles en todos los menus; help muestra ayuda.\n"
           "ESC cancela la entrada; Enter vacio muestra el menu actual.\n");
}

const char *serial_menu_prompt(serial_menu_t menu)
{
    return menu >= SERIAL_MENU_MAIN && menu < SERIAL_MENU_COUNT ? menus[menu].prompt : "panel> ";
}

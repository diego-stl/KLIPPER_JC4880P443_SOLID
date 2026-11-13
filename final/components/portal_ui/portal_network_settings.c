#include "portal_network_settings.h"
#include "audio_service.h"
#include "device_network.h"
#include "portal_surfaces.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { ACT_CLOSE=1,ACT_WIFI,ACT_ENDPOINT,ACT_SCAN,ACT_WIFI_SAVE,ACT_ENDPOINT_SAVE,ACT_TEST };
static lv_obj_t *s_overlay,*s_body,*s_wifi,*s_endpoint,*s_keyboard,*s_status,*s_link;
static lv_obj_t *s_networks,*s_ssid,*s_password,*s_host,*s_port;
static bool (*s_guard)(void);
static bool s_selecting,s_raw_selection,s_closing;
static char s_selected_ssid[33];
static bool s_requires_password;
static uint32_t s_generation,s_revision;
static device_network_settings_snapshot_t s_state;
static device_network_operation_t s_audio_operation;

static void erase(void *memory,size_t length)
{ volatile unsigned char *p=memory;while(length--) *p++=0; }
static bool blocked(void)
{ return s_guard && s_guard(); }
static void surface(lv_obj_t *object,uint32_t color)
{
    lv_obj_set_style_bg_color(object,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(object,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(object,lv_color_hex(0xf5f8fc),0);
    lv_obj_set_style_border_color(object,lv_color_hex(0x284b66),0);
    lv_obj_set_style_border_width(object,1,0);
    lv_obj_set_style_radius(object,7,0);
}
static lv_obj_t *label(lv_obj_t *parent,const char *text,int x,int y,int width)
{
    lv_obj_t *object=lv_label_create(parent);lv_label_set_text(object,text);
    lv_obj_set_pos(object,x,y);lv_obj_set_width(object,width);
    lv_obj_set_style_text_font(object,portal_font(&lv_font_montserrat_16),0);
    lv_obj_set_style_text_color(object,lv_color_hex(PORTAL_TEXT_COLOR),0);
    return object;
}
static void keyboard_hide(void)
{
    if (!s_overlay) return;
    lv_keyboard_set_textarea(s_keyboard,NULL);
    lv_obj_add_flag(s_keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_body,282);
    lv_obj_scroll_to_y(s_body,0,LV_ANIM_OFF);
    lv_obj_remove_flag(s_status,LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_link,LV_OBJ_FLAG_HIDDEN);
}
static void keyboard_event(lv_event_t *event)
{
    lv_event_code_t code=lv_event_get_code(event);
    // LV_EVENT_ALL also delivers DELETE while the overlay is being destroyed.
    // Never inspect the guard or re-enter teardown for lifecycle events.
    if (code!=LV_EVENT_READY && code!=LV_EVENT_CANCEL) return;
    if (s_closing || !s_overlay) return;
    if (blocked()) { portal_network_settings_close();return; }
    keyboard_hide();
}
static void focus_event(lv_event_t *event)
{
    if (blocked()) { portal_network_settings_close();return; }
    lv_obj_t *textarea=lv_event_get_target(event);
    lv_keyboard_set_mode(s_keyboard,(textarea==s_host || textarea==s_port)?
        LV_KEYBOARD_MODE_NUMBER:LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_keyboard,textarea);
    lv_obj_remove_flag(s_keyboard,LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_height(s_body,134);
    lv_obj_add_flag(s_status,LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_link,LV_OBJ_FLAG_HIDDEN);
    // Fields are children of a non-scrolling form; scroll its body ancestor.
    lv_obj_scroll_to_view_recursive(textarea,LV_ANIM_OFF);
}
static void ssid_changed(lv_event_t *event)
{
    (void)event;
    if (blocked()) { portal_network_settings_close();return; }
    if (!s_selecting) { s_raw_selection=false;s_requires_password=false;erase(s_selected_ssid,sizeof(s_selected_ssid)); }
}
static void select_event(lv_event_t *event)
{
    (void)event;
    if (blocked()) { portal_network_settings_close();return; }
    unsigned selected=lv_dropdown_get_selected(s_networks);
    if (selected==0) {
        s_raw_selection=s_requires_password=false;erase(s_selected_ssid,sizeof(s_selected_ssid));
        s_selecting=true;lv_textarea_set_text(s_ssid,"");s_selecting=false;
        lv_textarea_set_text(s_password,"");
        lv_label_set_text(s_status,"Introduce el SSID manual y la clave, si existe");
        return;
    }
    if (selected>s_state.count) return;
    const device_network_ap_t *ap=&s_state.networks[selected-1];
    memcpy(s_selected_ssid,ap->ssid,sizeof(s_selected_ssid));
    s_requires_password=ap->secured;
    char display[33];
    for (unsigned i=0;i<sizeof(display);++i) {
        unsigned char c=(unsigned char)ap->ssid[i];
        display[i]=(c && (c<32 || c>126))?'?':(char)c;
        if (!c) break;
    }
    display[32]=0;
    s_selecting=true;lv_textarea_set_text(s_ssid,display);s_selecting=false;
    s_raw_selection=true;
    lv_textarea_set_text(s_password,"");
    lv_label_set_text(s_status,ap->secured?"Red seleccionada: introduce la clave":"Red abierta: deja la clave vacia");
}
static void play_network_result(void)
{
    if(s_audio_operation==DEVICE_NETWORK_OP_NONE || s_state.busy ||
       s_state.operation!=s_audio_operation)return;
    device_network_operation_t operation=s_audio_operation;
    s_audio_operation=DEVICE_NETWORK_OP_NONE;
    if(operation==DEVICE_NETWORK_OP_ENDPOINT_TEST || s_state.result==ESP_OK ||
       (operation==DEVICE_NETWORK_OP_WIFI_SAVE && s_state.result==ESP_ERR_TIMEOUT))
        audio_service_play(AUDIO_CLIP_NETWORK);
}
static void submitted(esp_err_t result,device_network_operation_t operation)
{
    keyboard_hide();
    if (result==ESP_OK) {
        if(operation!=DEVICE_NETWORK_OP_SCAN)s_audio_operation=operation;
        lv_label_set_text(s_status,"Solicitud pendiente...");
    }
    else if (result==ESP_ERR_INVALID_ARG)
        lv_label_set_text(s_status,"Revisa SSID, clave (8-63 bytes o 64 hex), IPv4 y puerto (1-65535)");
    else lv_label_set_text(s_status,"Red iniciando u ocupada; espera y vuelve a intentar");
}
static void action_event(lv_event_t *event)
{
    if (blocked()) { portal_network_settings_close();return; }
    unsigned action=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (action==ACT_CLOSE) { portal_network_settings_close();return; }
    if (action==ACT_WIFI || action==ACT_ENDPOINT) {
        keyboard_hide();
        if (action==ACT_WIFI) { lv_obj_remove_flag(s_wifi,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(s_endpoint,LV_OBJ_FLAG_HIDDEN); }
        else { lv_obj_remove_flag(s_endpoint,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(s_wifi,LV_OBJ_FLAG_HIDDEN); }
        return;
    }
    if(s_audio_operation!=DEVICE_NETWORK_OP_NONE){
        device_network_settings_snapshot(&s_state);
        play_network_result();
    }
    if (action==ACT_SCAN) submitted(device_network_scan_async(),DEVICE_NETWORK_OP_SCAN);
    else if (action==ACT_WIFI_SAVE) {
        const char *ssid=s_raw_selection?s_selected_ssid:lv_textarea_get_text(s_ssid);
        esp_err_t result=device_network_save_wifi_async(ssid,lv_textarea_get_text(s_password),s_requires_password);
        if (result==ESP_OK) lv_textarea_set_text(s_password,"");
        submitted(result,DEVICE_NETWORK_OP_WIFI_SAVE);
    } else if (action==ACT_ENDPOINT_SAVE)
        submitted(device_network_save_endpoint_async(lv_textarea_get_text(s_host),lv_textarea_get_text(s_port)),DEVICE_NETWORK_OP_ENDPOINT_SAVE);
    else if (action==ACT_TEST)
        submitted(device_network_test_endpoint_async(lv_textarea_get_text(s_host),lv_textarea_get_text(s_port)),DEVICE_NETWORK_OP_ENDPOINT_TEST);
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int width,unsigned action)
{
    lv_obj_t *object=lv_button_create(parent);lv_obj_set_pos(object,x,y);lv_obj_set_size(object,width,44);
    lv_obj_set_ext_click_area(object,2);
    surface(object,0x192735);lv_obj_add_event_cb(object,action_event,LV_EVENT_CLICKED,(void *)(uintptr_t)action);
    lv_obj_t *caption=lv_label_create(object);lv_label_set_text(caption,text);
    lv_obj_set_style_text_font(caption,portal_font(&lv_font_montserrat_20),0);lv_obj_center(caption);
    return object;
}
static lv_obj_t *field(lv_obj_t *parent,const char *caption,const char *placeholder,int x,int y,int width,unsigned maximum)
{
    label(parent,caption,x,y,width);
    lv_obj_t *object=lv_textarea_create(parent);lv_obj_set_pos(object,x,y+25);lv_obj_set_size(object,width,44);
    lv_obj_set_ext_click_area(object,2);
    lv_obj_set_style_text_font(object,portal_font(&lv_font_montserrat_20),0);
    surface(object,0x0c151e);lv_textarea_set_one_line(object,true);
    lv_textarea_set_max_length(object,maximum);lv_textarea_set_placeholder_text(object,placeholder);
    lv_obj_add_event_cb(object,focus_event,LV_EVENT_FOCUSED,NULL);
    lv_obj_add_event_cb(object,focus_event,LV_EVENT_CLICKED,NULL);
    return object;
}
static lv_obj_t *form(lv_obj_t *parent)
{
    lv_obj_t *object=lv_obj_create(parent);lv_obj_remove_style_all(object);
    lv_obj_set_size(object,756,286);lv_obj_remove_flag(object,LV_OBJ_FLAG_SCROLLABLE);
    portal_preserve_text_backdrops(object);
    return object;
}
void portal_network_settings_open(lv_obj_t *parent,bool (*input_blocked)(void))
{
    if (input_blocked && input_blocked()) return;
    portal_network_settings_close();s_guard=input_blocked;
    device_network_snapshot_t network;device_network_snapshot(&network);
    s_generation=network.endpoint_generation;s_revision=UINT32_MAX;
    s_overlay=lv_obj_create(parent?parent:lv_layer_top());lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay,800,480);surface(s_overlay,PORTAL_BACKGROUND_COLOR);
    lv_obj_set_style_border_color(s_overlay,lv_color_hex(PORTAL_BACKGROUND_COLOR),0);
    lv_obj_set_style_text_font(s_overlay,&lv_font_montserrat_20,0);
    lv_obj_set_style_radius(s_overlay,0,0);lv_obj_remove_flag(s_overlay,LV_OBJ_FLAG_SCROLLABLE);
    portal_background(s_overlay);
    lv_obj_t *backdrop=lv_obj_create(s_overlay);lv_obj_remove_style_all(backdrop);
    lv_obj_set_pos(backdrop,12,12);lv_obj_set_size(backdrop,776,456);surface(backdrop,PORTAL_PANEL_COLOR);
    lv_obj_set_style_border_color(backdrop,lv_color_hex(0x929DA2),0);
    portal_preserve_text_backdrops(backdrop);
    lv_obj_set_style_bg_grad_dir(backdrop,LV_GRAD_DIR_NONE,0);lv_obj_set_style_shadow_width(backdrop,0,0);
    lv_obj_remove_flag(backdrop,LV_OBJ_FLAG_SCROLLABLE);
    label(s_overlay,"RED Y MOONRAKER",24,24,540);
    button(s_overlay,"Cerrar",648,12,120,ACT_CLOSE);
    button(s_overlay,"Wi-Fi",20,52,170,ACT_WIFI);
    button(s_overlay,"Moonraker",202,52,200,ACT_ENDPOINT);
    s_body=lv_obj_create(s_overlay);lv_obj_remove_style_all(s_body);
    lv_obj_set_pos(s_body,20,108);lv_obj_set_size(s_body,760,282);
    lv_obj_set_scroll_dir(s_body,LV_DIR_VER);
    s_wifi=form(s_body);s_endpoint=form(s_body);lv_obj_add_flag(s_endpoint,LV_OBJ_FLAG_HIDDEN);
    s_networks=lv_dropdown_create(s_wifi);lv_obj_set_pos(s_networks,0,0);lv_obj_set_size(s_networks,544,44);
    lv_obj_set_ext_click_area(s_networks,2);
    lv_obj_set_style_text_font(s_networks,&lv_font_montserrat_20,0);
    surface(s_networks,0x0c151e);lv_dropdown_set_options(s_networks,"SSID manual / selecciona red");
    lv_obj_add_event_cb(s_networks,select_event,LV_EVENT_VALUE_CHANGED,NULL);
    button(s_wifi,"Escanear",556,0,180,ACT_SCAN);
    s_ssid=field(s_wifi,"SSID (manual u oculto)","Nombre de red",0,58,736,32);
    lv_obj_add_event_cb(s_ssid,ssid_changed,LV_EVENT_VALUE_CHANGED,NULL);
    s_password=field(s_wifi,"Clave Wi-Fi (oculta)","Vacía para red abierta",0,140,736,64);
    lv_textarea_set_password_mode(s_password,true);
    lv_textarea_set_password_show_time(s_password,0);
    button(s_wifi,"Guardar y conectar Wi-Fi",0,222,736,ACT_WIFI_SAVE);
    s_host=field(s_endpoint,"IP de Moonraker","192.168.1.10",0,0,548,15);
    s_port=field(s_endpoint,"Puerto","7125",566,0,170,5);
    lv_textarea_set_accepted_chars(s_host,"0123456789.");
    lv_textarea_set_accepted_chars(s_port,"0123456789");
    // Never filter a saved DNS name into misleading partial numeric text.
    // Console-configured DNS endpoints remain active until an IPv4 is saved.
    if (strlen(network.moonraker_host)<=15 &&
        strspn(network.moonraker_host,"0123456789.")==strlen(network.moonraker_host))
        lv_textarea_set_text(s_host,network.moonraker_host);
    char port[8];snprintf(port,sizeof(port),"%u",network.moonraker_port);lv_textarea_set_text(s_port,port);
    label(s_endpoint,"Introduce una IPv4; los nombres DNS se configuran por consola.\nProbar consulta /server/info sin guardar la IP.",0,90,736);
    button(s_endpoint,"Probar conexión",0,152,356,ACT_TEST);
    button(s_endpoint,"Guardar Moonraker",376,152,360,ACT_ENDPOINT_SAVE);
    s_status=label(s_overlay,"Red iniciando...",20,398,760);
    s_link=label(s_overlay,"",20,446,760);
    s_keyboard=lv_keyboard_create(s_overlay);lv_obj_set_align(s_keyboard,LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(s_keyboard,0,254);lv_obj_set_size(s_keyboard,800,226);
    lv_obj_set_style_bg_color(s_keyboard,lv_color_hex(PORTAL_STATUS_COLOR),LV_PART_MAIN);
    lv_obj_set_style_text_font(s_keyboard,&lv_font_montserrat_20,LV_PART_ITEMS);
    lv_obj_add_event_cb(s_keyboard,keyboard_event,LV_EVENT_ALL,NULL);lv_obj_add_flag(s_keyboard,LV_OBJ_FLAG_HIDDEN);
    portal_network_settings_tick();
}
void portal_network_settings_close(void)
{
    if (s_closing) return;
    s_closing=true;
    lv_obj_t *overlay=s_overlay,*password=s_password,*keyboard=s_keyboard;
    // Retire the published object graph before LVGL emits DELETE callbacks.
    // Teardown uses only these local pointers, whose objects are still alive.
    s_overlay=s_body=s_wifi=s_endpoint=s_keyboard=s_status=s_link=NULL;
    s_networks=s_ssid=s_password=s_host=s_port=NULL;s_guard=NULL;
    s_selecting=s_raw_selection=s_requires_password=false;
    erase(s_selected_ssid,sizeof(s_selected_ssid));erase(&s_state,sizeof(s_state));
    if (overlay) {
        if (keyboard) lv_keyboard_set_textarea(keyboard,NULL);
        if (password) lv_textarea_set_text(password,"");
        lv_obj_delete(overlay);
    }
    s_closing=false;
}
bool portal_network_settings_is_open(void) { return s_overlay!=NULL; }
void portal_network_settings_tick(void)
{
    if (!s_overlay && s_audio_operation==DEVICE_NETWORK_OP_NONE) return;
    device_network_settings_snapshot(&s_state);
    play_network_result();
    if (!s_overlay) return;
    if (blocked()) { portal_network_settings_close();return; }
    device_network_snapshot_t network;device_network_snapshot(&network);
    if (network.endpoint_generation!=s_generation) { portal_network_settings_close();return; }
    if (s_state.revision!=s_revision) {
        s_revision=s_state.revision;
        lv_label_set_text(s_status,s_state.message[0]?s_state.message:s_state.ready?"Listo para configurar":"Red iniciando...");
        if (!s_state.busy && s_state.result==ESP_OK && s_state.operation==DEVICE_NETWORK_OP_SCAN) {
            char options[1280]="SSID manual / selecciona red";
            size_t used=strlen(options);
            for (unsigned i=0;i<s_state.count && i<DEVICE_NETWORK_SCAN_MAX;++i) {
                char display[33];
                for (unsigned j=0;j<32;++j) {
                    unsigned char c=(unsigned char)s_state.networks[i].ssid[j];
                    display[j]=(c && (c<32 || c>126))?'?':(char)c;if (!c) break;
                }
                display[32]=0;
                int length=snprintf(options+used,sizeof(options)-used,"\n%s  %s %d dBm",display,
                    s_state.networks[i].secured?"[clave]":"[abierta]",s_state.networks[i].rssi);
                if (length<0 || (size_t)length>=sizeof(options)-used) break;
                used+=(size_t)length;
            }
            lv_dropdown_set_options(s_networks,options);
        }
    }
    char link[160];snprintf(link,sizeof(link),"Wi-Fi: %s%s%s%s",network.wifi_ready?"listo":"iniciando",
        network.has_ip?" | IP ":" | sin IP",network.has_ip?network.ip:"",s_state.busy?" | trabajando...":"");
    lv_label_set_text(s_link,link);
}

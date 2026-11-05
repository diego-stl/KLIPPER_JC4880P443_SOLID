#include "orientation_test.h"
#include "gui_port.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "orientation";
static const uint32_t BG=0x050b10, PANEL=0x0c151e, CYAN=0x00c9f4, MUTED=0xabc3df;
static QueueHandle_t s_commands;
static lv_obj_t *s_title, *s_raw, *s_logical, *s_state, *s_count, *s_gate_label, *s_uptime;
static lv_obj_t *s_corner_raw[4], *s_targets[9], *s_mode_buttons[2], *s_native_left, *s_native_right;
static lv_obj_t *s_cursor;
static lv_obj_t *s_native_label;
static uint32_t s_clicks[2], s_target_mask[2], s_down[2], s_up[2];
static uint16_t s_raw_x, s_raw_y, s_x, s_y;
static bool s_pressed, s_have_sample;
static bool s_touch_ready;
static bool s_previous_blocked;
static unsigned s_last_second;
static const uint16_t TARGET_X[]={24,775,24,775,400,400,400,24,775};
static const uint16_t TARGET_Y[]={24,24,455,455,240,24,455,240,240};
static const char *TARGET_NAMES[]={"A","B","C","D","CENTRO","SUPERIOR","INFERIOR","IZQUIERDA","DERECHA"};

static unsigned mode_index(void) { return orientation_current()==ORIENTATION_R90?0:1; }
static lv_obj_t *label(const char *value, int x, int y, int width,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj=lv_label_create(lv_screen_active());
    lv_label_set_text(obj,value);
    lv_obj_set_pos(obj,x,y); lv_obj_set_width(obj,width);
    lv_obj_set_style_text_font(obj,font,0); lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
static lv_obj_t *box(int x,int y,int w,int h,uint32_t color)
{
    lv_obj_t *obj=lv_obj_create(lv_screen_active()); lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_bg_color(obj,lv_color_hex(color),0);lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_remove_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}
static void print_status(void)
{
    ESP_LOGI(TAG,"ORIENTATION_STATUS rot=%u blocked=%u raw=(%u,%u) UI=(%u,%u) pressed=%u sample=%u",
             (unsigned)orientation_current(),gui_port_touch_blocked(),s_raw_x,s_raw_y,s_x,s_y,s_pressed,s_have_sample);
    for(unsigned i=0;i<2;++i)
        ESP_LOGI(TAG,"TOUCH_COUNTS rot=%u clicks=%"PRIu32" targets=0x%03"PRIx32" down=%"PRIu32" up=%"PRIu32,
                 i?270:90,s_clicks[i],s_target_mask[i],s_down[i],s_up[i]);
}
static void refresh_mode(void)
{
    unsigned rotation=orientation_current(),m=mode_index();
    lv_label_set_text_fmt(s_title,"ARRIBA - HORIZONTAL R%u",rotation);
    lv_label_set_text_fmt(s_native_label,"Borde nativo Y=0: %s (marca naranja)",rotation==90?"IZQUIERDA":"DERECHA");
    lv_label_set_text_fmt(s_count,"Pulsaciones: %"PRIu32,s_clicks[m]);
    if(s_have_sample){
        orientation_map_touch(rotation,s_raw_x,s_raw_y,&s_x,&s_y);
        lv_label_set_text_fmt(s_logical,"UI:   %u , %u",s_x,s_y);
    }
    for(unsigned i=0;i<4;++i){
        unsigned raw_x=rotation==90?479-TARGET_Y[i]:TARGET_Y[i];
        unsigned raw_y=rotation==90?TARGET_X[i]:799-TARGET_X[i];
        lv_label_set_text_fmt(s_corner_raw[i],"RAW (%u, %u)",raw_x,raw_y);
    }
    for(unsigned i=0;i<9;++i)
        lv_obj_set_style_bg_opa(s_targets[i],s_target_mask[m]&(1U<<i)?LV_OPA_40:LV_OPA_TRANSP,0);
    for(unsigned i=0;i<2;++i)
        lv_obj_set_style_border_color(s_mode_buttons[i],lv_color_hex((i==m)?CYAN:0x284b66),0);
    if(rotation==90){lv_obj_remove_flag(s_native_left,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(s_native_right,LV_OBJ_FLAG_HIDDEN);}
    else {lv_obj_add_flag(s_native_left,LV_OBJ_FLAG_HIDDEN);lv_obj_remove_flag(s_native_right,LV_OBJ_FLAG_HIDDEN);}
    lv_obj_add_flag(s_cursor,LV_OBJ_FLAG_HIDDEN);
}
static void enqueue(unsigned command)
{
    if(xQueueSend(s_commands,&command,0)!=pdTRUE) ESP_LOGW(TAG,"Cola ocupada; repetir comando");
}
static void mode_event(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_CLICKED && !gui_port_touch_blocked())
        enqueue((unsigned)(uintptr_t)lv_event_get_user_data(event));
}
static void count_event(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_CLICKED && !gui_port_touch_blocked()){
        unsigned m=mode_index();++s_clicks[m];
        lv_label_set_text_fmt(s_count,"Pulsaciones: %"PRIu32,s_clicks[m]);
        ESP_LOGI(TAG,"COUNT_CLICK rot=%u total=%"PRIu32,(unsigned)orientation_current(),s_clicks[m]);
    }
}
static void target_event(lv_event_t *event)
{
    if(lv_event_get_code(event)==LV_EVENT_CLICKED && !gui_port_touch_blocked()){
        unsigned i=(unsigned)(uintptr_t)lv_event_get_user_data(event);
        s_target_mask[mode_index()]|=1U<<i;
        lv_obj_set_style_bg_opa(s_targets[i],LV_OPA_40,0);
        ESP_LOGI(TAG,"TARGET_CLICK rot=%u target=%s raw=(%u,%u) UI=(%u,%u)",
                 (unsigned)orientation_current(),TARGET_NAMES[i],s_raw_x,s_raw_y,s_x,s_y);
    }
}
static lv_obj_t *button(const char *caption,int x,int y,int w,int h,
                         lv_event_cb_t callback,void *context)
{
    lv_obj_t *obj=lv_button_create(lv_screen_active());
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,w,h);
    lv_obj_set_style_radius(obj,7,0);lv_obj_set_style_bg_color(obj,lv_color_hex(PANEL),0);
    lv_obj_set_style_bg_color(obj,lv_color_hex(0x073447),LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj,2,0);lv_obj_set_style_border_color(obj,lv_color_hex(0x284b66),0);
    lv_obj_set_style_shadow_width(obj,0,0);lv_obj_set_style_text_color(obj,lv_color_hex(CYAN),0);
    lv_obj_set_style_text_font(obj,&lv_font_montserrat_20,0);
    lv_obj_add_event_cb(obj,callback,LV_EVENT_CLICKED,context);
    lv_obj_t *caption_obj=lv_label_create(obj);lv_label_set_text(caption_obj,caption);lv_obj_center(caption_obj);
    return obj;
}
static void observe_touch(const gt911_sample_t *sample,esp_err_t result)
{
    static int64_t last_log;
    if(result!=ESP_OK){lv_label_set_text_fmt(s_state,"ERROR: %s",esp_err_to_name(result));return;}
    if(!sample->fresh)return;
    unsigned m=mode_index();bool changed=!s_have_sample||s_pressed!=sample->pressed;
    if(changed){if(sample->pressed)++s_down[m];else ++s_up[m];}
    s_have_sample=true;s_pressed=sample->pressed;
    s_raw_x=sample->raw_x;s_raw_y=sample->raw_y;s_x=sample->x;s_y=sample->y;
    lv_label_set_text_fmt(s_raw,"RAW: %u , %u",s_raw_x,s_raw_y);
    lv_label_set_text_fmt(s_logical,"UI:   %u , %u",s_x,s_y);
    lv_label_set_text(s_state,s_pressed?"PULSADO":"LIBERADO");
    if(s_pressed)lv_obj_remove_flag(s_cursor,LV_OBJ_FLAG_HIDDEN);
    if(changed||esp_timer_get_time()-last_log>250000){
        ESP_LOGI(TAG,"TOUCH_%s rot=%u raw=(%u,%u) UI=(%u,%u) blocked=%u",
                 s_pressed?"DOWN":"UP",(unsigned)orientation_current(),s_raw_x,s_raw_y,s_x,s_y,gui_port_touch_blocked());
        last_log=esp_timer_get_time();
    }
}
static void create_ui(void)
{
    lv_obj_t *screen=lv_screen_active();lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0);lv_obj_set_style_pad_all(screen,0,0);
    lv_obj_t *frame=box(0,0,800,480,BG);lv_obj_set_style_bg_opa(frame,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(frame,2,0);lv_obj_set_style_border_color(frame,lv_color_hex(0x284b66),0);
    s_title=label("",170,57,480,&lv_font_montserrat_28,CYAN);
    lv_obj_set_style_text_align(s_title,LV_TEXT_ALIGN_CENTER,0);
    label("800 x 480 - toca las dianas y arrastra el marcador",140,99,600,&lv_font_montserrat_20,MUTED);
    s_native_label=label("",210,127,560,&lv_font_montserrat_14,0xff9d00);
    s_raw=label("RAW: --- , ---",70,157,280,&lv_font_montserrat_20,0xf5f8fc);
    s_logical=label("UI:   --- , ---",70,190,280,&lv_font_montserrat_20,0xf5f8fc);
    s_state=label("SIN MUESTRAS",70,264,280,&lv_font_montserrat_20,MUTED);
    s_count=label("Pulsaciones: 0",520,160,240,&lv_font_montserrat_20,0xf5f8fc);
    button("SUMAR +1",520,200,210,62,count_event,NULL);
    s_uptime=label("Activo: 0 s",520,274,240,&lv_font_montserrat_20,MUTED);
    s_gate_label=label("ENTRADA ACTIVA",80,316,640,&lv_font_montserrat_20,CYAN);
    lv_obj_set_style_text_align(s_gate_label,LV_TEXT_ALIGN_CENTER,0);
    label("Si sigue bloqueado: toca y suelta una vez.\nConsola: 90 / 270 / status / clear / help",230,340,560,&lv_font_montserrat_14,MUTED);
    s_mode_buttons[0]=button("R90",265,380,124,54,mode_event,(void*)(uintptr_t)90);
    s_mode_buttons[1]=button("R270",411,380,124,54,mode_event,(void*)(uintptr_t)270);
    const int positions_x[]={50,550,50,550},positions_y[]={5,5,400,400};
    for(unsigned i=0;i<4;++i){
        char value[60];snprintf(value,sizeof(value),"%s - UI (%u, %u)",TARGET_NAMES[i],TARGET_X[i],TARGET_Y[i]);
        label(value,positions_x[i],positions_y[i],225,&lv_font_montserrat_20,MUTED);
        s_corner_raw[i]=label("",positions_x[i],positions_y[i]+25,225,&lv_font_montserrat_14,MUTED);
    }
    label("CENTRO (400, 240)",302,275,205,&lv_font_montserrat_14,MUTED);
    label("ABAJO",440,450,140,&lv_font_montserrat_14,MUTED);
    const uint32_t colors[]={0xff304d,0x00e676,0x00c9f4,0xffd740,0x00c9f4,0xabc3df,0xabc3df,0xabc3df,0xabc3df};
    for(unsigned i=0;i<9;++i){
        lv_obj_t *obj=lv_button_create(screen);lv_obj_remove_style_all(obj);
        lv_obj_set_pos(obj,TARGET_X[i]-20,TARGET_Y[i]-20);lv_obj_set_size(obj,40,40);
        lv_obj_set_style_radius(obj,LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(obj,2,0);
        lv_obj_set_style_border_color(obj,lv_color_hex(colors[i]),0);lv_obj_set_style_bg_color(obj,lv_color_hex(colors[i]),0);
        lv_obj_add_event_cb(obj,target_event,LV_EVENT_CLICKED,(void*)(uintptr_t)i);
        s_targets[i]=obj;
        lv_obj_t *cross=lv_label_create(obj);lv_label_set_text(cross,"+");
        lv_obj_set_style_text_font(cross,&lv_font_montserrat_20,0);lv_obj_set_style_text_color(cross,lv_color_hex(colors[i]),0);lv_obj_center(cross);
    }
    s_native_left=box(2,150,6,64,0xff9d00);
    s_native_right=box(792,150,6,64,0xff9d00);
    s_cursor=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(s_cursor);
    lv_obj_set_size(s_cursor,14,14);lv_obj_set_style_radius(s_cursor,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(s_cursor,lv_color_hex(0xff63ce),0);lv_obj_set_style_bg_opa(s_cursor,LV_OPA_COVER,0);
    lv_obj_set_style_border_width(s_cursor,2,0);lv_obj_set_style_border_color(s_cursor,lv_color_white(),0);
    lv_obj_set_style_translate_x(s_cursor,-7,0);lv_obj_set_style_translate_y(s_cursor,-7,0);
    lv_obj_remove_flag(s_cursor,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_cursor,LV_OBJ_FLAG_HIDDEN);
    refresh_mode();
}
static void cycle(void)
{
    unsigned command;
    while(xQueueReceive(s_commands,&command,0)==pdTRUE){
        if(orientation_valid(command)){
            ESP_LOGI(TAG,"ORIENTATION_REQUEST rot=%u",command);
            esp_err_t result=gui_port_set_rotation(command);
            if(result==ESP_OK)refresh_mode();
        }else if(command==1)print_status();
        else if(command==2){memset(s_clicks,0,sizeof(s_clicks));memset(s_target_mask,0,sizeof(s_target_mask));refresh_mode();print_status();}
    }
    bool blocked=gui_port_touch_blocked();
    if(blocked!=s_previous_blocked){
        lv_label_set_text(s_gate_label,!s_touch_ready?"TACTIL INACTIVO - REVISAR ERROR":
                          blocked?"LEVANTA EL DEDO PARA ACTIVAR":"ENTRADA ACTIVA");
        s_previous_blocked=blocked;
    }
    unsigned second=(unsigned)(esp_timer_get_time()/1000000);
    if(second!=s_last_second){lv_label_set_text_fmt(s_uptime,"Activo: %u s",second);s_last_second=second;}
}
static void console_task(void *arg)
{
    usb_serial_jtag_driver_config_t cfg={.tx_buffer_size=4096,.rx_buffer_size=256};
    if(!usb_serial_jtag_is_driver_installed()&&usb_serial_jtag_driver_install(&cfg)!=ESP_OK){ESP_LOGE(TAG,"No se pudo iniciar consola USB");vTaskDelete(NULL);return;}
    usb_serial_jtag_vfs_use_driver();
    ESP_LOGI(TAG,"ORIENTATION_CONSOLE_READY: 90 | 270 | status | clear | help");
    char line[24];unsigned n=0;bool overflow=false;
    for(;;){
        uint8_t byte;
        if(usb_serial_jtag_read_bytes(&byte,1,pdMS_TO_TICKS(100))!=1)continue;
        if(byte=='\r'||byte=='\n'){
            if(!n&&!overflow)continue;
            line[n]=0;
            if(!overflow&&(!strcmp(line,"90")||!strcmp(line,"r90")||!strcmp(line,"R90")))enqueue(90);
            else if(!overflow&&(!strcmp(line,"270")||!strcmp(line,"r270")||!strcmp(line,"R270")))enqueue(270);
            else if(!overflow&&!strcmp(line,"status"))enqueue(1);
            else if(!overflow&&!strcmp(line,"clear"))enqueue(2);
            else ESP_LOGI(TAG,"Comandos: 90 | 270 | status | clear | help");
            n=0;overflow=false;
        }else if(byte==8||byte==127){if(n)--n;}
        else if(byte>=32&&byte<127){if(n<sizeof(line)-1)line[n++]=(char)byte;else overflow=true;}
    }
}
void orientation_test_run(void)
{
    s_commands=xQueueCreate(8,sizeof(unsigned));
    if(!s_commands){ESP_LOGE(TAG,"Sin memoria para comandos");return;}
    lv_display_t *display=NULL;
    ESP_ERROR_CHECK(gui_port_init(&display));create_ui();
    lv_indev_t *indev=NULL;esp_err_t touch=gui_port_add_touch(display,observe_touch,&indev);
    if(touch==ESP_OK){s_touch_ready=true;lv_indev_set_cursor(indev,s_cursor);}
    else {lv_label_set_text_fmt(s_state,"ERROR TACTIL: %s",esp_err_to_name(touch));ESP_LOGE(TAG,"Tactil inactivo: %s",esp_err_to_name(touch));}
    if(xTaskCreate(console_task,"orientation_console",4096,NULL,2,NULL)!=pdPASS)ESP_LOGE(TAG,"Sin tarea de consola");
    ESP_LOGI(TAG,"ORIENTATION_TEST_READY; inicial R90; sin Wi-Fi/Klipper/audio/LED; validacion visual pendiente");
    gui_port_run(cycle);
}

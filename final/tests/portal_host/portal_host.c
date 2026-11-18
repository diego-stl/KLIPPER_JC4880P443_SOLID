/* Real firmware UI + real LVGL; only device/backend services are replaced. */
#include "lvgl.h"
#include "portal_ui.h"
#include "portal_assets.h"
#include "portal_network_settings.h"
#include "fixtures_png.h"
#include "audio_service.h"
#include "audio_events.h"
#include "device_network.h"
#include "klipper_client.h"
#include "board_display.h"
#include "display_settings_mailbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>

static uint16_t framebuffer[800 * 480];
static uint16_t *host_draw_buffer;
static uint32_t rendered_frames,last_damage_pixels,frame_damage_pixels;
static uint8_t panel_brightness;
static lv_indev_t *host_input;
static lv_indev_data_t host_pointer;
static klipper_snapshot_t snapshot;
static char mock_error_detail[1024];
static klipper_metadata_t metadata;
static klipper_metadata_t last_metadata;
static klipper_last_job_t last_job;
static device_network_snapshot_t network;
static device_network_settings_snapshot_t network_settings;
static char pending_host[64],pending_port[8];
static unsigned print_calls,reset_calls;
static unsigned tuning_calls[3],tuning_last[3];
static bool print_reject;
static int print_result_pending=-1;
static lv_obj_t *saved_button;
static bool record(const char *kind,const char *value);
static unsigned backend_calls, events_sent, clipped_labels;
static unsigned display_writes,volume_writes;
static display_settings_mailbox_t display_mailbox={.brightness=80,.saver_brightness=DEVICE_NETWORK_DEFAULT_SAVER_BRIGHTNESS,.sleep_minutes=10};
void device_network_display_status(device_network_display_status_t *out) {*out=display_mailbox.status;}
static bool inspect_colors=true;
static bool protected_styles_only;
static const char *output_dir;
static bool result_unknown_pending,result_reset_pending;
static klipper_command_result_t feedback_result;
static bool feedback_result_ready;
/* Routing-only audio mock: no PCM, DSP, worker or physical speaker. */
static audio_service_status_t audio_mock={.ready=true,.enabled=true,.volume=50,.last_clip=AUDIO_CLIP_COUNT};
static unsigned voice_accepted[AUDIO_CLIP_COUNT];
static unsigned inspection_voices_before[AUDIO_CLIP_COUNT];
static audio_events_status_t inspection_audio_before;
const char *audio_service_clip_name(audio_clip_t clip) { return audio_clip_name_value(clip); }
esp_err_t audio_service_play(audio_clip_t clip) {
    if((unsigned)clip>=AUDIO_CLIP_COUNT)return ESP_ERR_INVALID_ARG;
    if(!audio_mock.ready||!audio_mock.enabled||!audio_mock.volume){audio_mock.requests_dropped++;printf("CALL audio_play %s REJECTED\n",audio_service_clip_name(clip));return ESP_ERR_INVALID_STATE;}
    voice_accepted[clip]++;audio_mock.queued++;
    printf("CALL audio_play %s ACCEPTED queued=%u volume=%u\n",audio_service_clip_name(clip),audio_mock.queued,audio_mock.volume);return ESP_OK;
}
esp_err_t audio_service_play_printer(audio_clip_t clip) { return audio_service_play(clip); }
esp_err_t audio_service_start(void) { audio_mock.ready=true;return ESP_OK; }
void audio_service_get_status(audio_service_status_t *out) { *out=audio_mock; }
esp_err_t audio_service_set_enabled(bool enabled) {
    audio_mock.enabled=enabled;audio_mock.generation++;
    if(!enabled){audio_mock.queued=0;audio_mock.playing=false;}
    printf("CALL audio_enabled %u queued=%u\n",enabled,audio_mock.queued);return ESP_OK;
}
esp_err_t audio_service_set_volume(uint8_t volume) {
    if(volume>100)return ESP_ERR_INVALID_ARG;
    volume_writes++;audio_mock.volume=volume;printf("CALL audio_volume %u\n",volume);return ESP_OK;
}
esp_err_t audio_service_test_tone(void) {
    if(!audio_mock.ready||!audio_mock.enabled||!audio_mock.volume){audio_mock.requests_dropped++;printf("CALL audio_test REJECTED\n");return ESP_ERR_INVALID_STATE;}
    audio_mock.queued++;printf("CALL audio_test ACCEPTED queued=%u volume=%u\n",audio_mock.queued,audio_mock.volume);return ESP_OK;
}
esp_err_t audio_service_stop(void) { audio_mock.queued=0;audio_mock.playing=false;audio_mock.generation++;return ESP_OK; }
void audio_service_stop_printer(void) { audio_service_stop(); }
bool audio_service_console_command(const char *command) { (void)command;return false; }
static const char *views[] = {"startup", "standby", "printing", "paused", "toolhead", "files", "macros", "exclude", "settings", "error", "details"};
int64_t esp_timer_get_time(void) { return (int64_t)lv_tick_get() * 1000; }
esp_err_t device_network_start(void) { return ESP_OK; }
bool device_network_console_active(void) { return false; }
void device_network_snapshot(device_network_snapshot_t *out) { *out = network; }
void device_network_settings_snapshot(device_network_settings_snapshot_t *out) { *out=network_settings; }
static esp_err_t network_submit(device_network_operation_t operation) {
    if(!network_settings.ready||network_settings.busy)return ESP_ERR_INVALID_STATE;
    network_settings.busy=true;network_settings.operation=operation;network_settings.revision++;
    strcpy(network_settings.message,"Solicitud pendiente...");network_settings.result=ESP_OK;return ESP_OK;
}
esp_err_t device_network_scan_async(void) {
    esp_err_t result=network_submit(DEVICE_NETWORK_OP_SCAN);if(result==ESP_OK)record("network_scan","");return result;
}
esp_err_t device_network_save_wifi_async(const char *ssid,const char *password,bool secured) {
    if(!ssid||!*ssid||strlen(ssid)>32||!password||(secured&&strlen(password)<8))return ESP_ERR_INVALID_ARG;
    esp_err_t result=network_submit(DEVICE_NETWORK_OP_WIFI_SAVE);
    if(result==ESP_OK){backend_calls++;printf("CALL network_wifi ssid=%s secured=%u password_redacted=1\n",ssid,secured);}return result;
}
static esp_err_t endpoint_submit(const char *host,const char *port,device_network_operation_t operation) {
    if(!host||!*host||strstr(host,":")||strstr(host,"/")||strchr(host,' ')||!port||!*port)return ESP_ERR_INVALID_ARG;
    char *end;long number=strtol(port,&end,10);if(*end||number<1||number>65535)return ESP_ERR_INVALID_ARG;
    esp_err_t result=network_submit(operation);
    if(result==ESP_OK){snprintf(pending_host,sizeof(pending_host),"%s",host);snprintf(pending_port,sizeof(pending_port),"%s",port);
        backend_calls++;printf("CALL network_%s host=%s port=%s\n",operation==DEVICE_NETWORK_OP_ENDPOINT_TEST?"test":"endpoint",host,port);}return result;
}
esp_err_t device_network_save_endpoint_async(const char *host,const char *port) { return endpoint_submit(host,port,DEVICE_NETWORK_OP_ENDPOINT_SAVE); }
esp_err_t device_network_test_endpoint_async(const char *host,const char *port) { return endpoint_submit(host,port,DEVICE_NETWORK_OP_ENDPOINT_TEST); }
esp_err_t device_network_set_display(uint8_t brightness, uint8_t saver_brightness, uint16_t sleep_minutes) {
    display_settings_submit(&display_mailbox,brightness,saver_brightness,sleep_minutes);
    network.brightness = brightness; network.saver_brightness=saver_brightness; network.sleep_minutes = sleep_minutes;
    backend_calls++; printf("CALL settings_submit brightness=%u sleep=%u\n", brightness, sleep_minutes); return ESP_OK;
}
esp_err_t board_display_set_brightness(uint8_t brightness) {
    panel_brightness=brightness;backend_calls++; printf("CALL brightness %u\n", brightness); return ESP_OK;
}
esp_err_t board_display_set_backlight(bool enabled) { backend_calls++; printf("CALL backlight %s\n",enabled?"on":"off"); return ESP_OK; }
esp_err_t klipper_client_start(void) { return ESP_OK; }
void klipper_client_snapshot(klipper_snapshot_t *out) { *out = snapshot; }
char *klipper_client_copy_error_detail(uint32_t generation,uint32_t revision)
{
    if(generation!=snapshot.endpoint_generation||revision!=snapshot.error_revision||!*mock_error_detail)return NULL;
    size_t length=strlen(mock_error_detail)+1;
    char *copy=malloc(length);
    if(copy)memcpy(copy,mock_error_detail,length);
    return copy;
}
bool klipper_client_metadata(klipper_metadata_t *out) { *out = metadata; return metadata.valid; }
bool klipper_client_last_job(klipper_last_job_t *out) { *out=last_job;return last_job.valid; }
bool klipper_client_last_job_metadata(klipper_metadata_t *out) { *out=last_metadata;return last_metadata.valid; }
bool klipper_client_pop_result(klipper_command_result_t *out) {
    if(feedback_result_ready){*out=feedback_result;feedback_result_ready=false;return true;}
    if(print_result_pending>=0){
        memset(out,0,sizeof(*out));out->id=103;out->status=(klipper_command_status_t)print_result_pending;print_result_pending=-1;
        strcpy(out->command,"SDCARD_PRINT_FILE");strcpy(out->message,out->status==KLIPPER_REJECTED?"Impresion rechazada por Moonraker":"Impresion pendiente de confirmar");
        printf("RESULT print status=%u popped\n",out->status);return true;
    }
    if(!result_unknown_pending){
        if(!result_reset_pending)return false;result_reset_pending=false;memset(out,0,sizeof(*out));out->id=102;out->status=KLIPPER_COMPLETED;
        strcpy(out->command,"SDCARD_RESET_FILE");strcpy(out->message,"Estado limpiado");return true;
    }
    result_unknown_pending=false;memset(out,0,sizeof(*out));out->id=101;out->status=KLIPPER_UNKNOWN;
    strcpy(out->command,"G1 X10");strcpy(out->message,"Resultado incierto: se perdio la conexion antes de confirmar la orden");
    printf("RESULT unknown popped\n");return true;
}
static bool record(const char *kind, const char *value) { backend_calls++; printf("CALL %s %s\n", kind, value ? value : ""); return true; }
bool klipper_client_action(klipper_action_t action,const char *value,uint32_t expected_generation) {
    static const char *names[]={"gcode","emergency","shutdown","print","macro","exclude","reset_job"};
    if(expected_generation!=network.endpoint_generation) { printf("REJECT_GENERATION %u != %u\n",expected_generation,network.endpoint_generation);return false; }
    if(action==K_ACTION_PRINT_FILE){print_calls++;if(print_reject){printf("REJECT print queue\n");return false;}}
    if(action==K_ACTION_RESET_JOB){
        if(!last_job.valid||!value||(unsigned)atoi(value)!=last_job.job_id||snapshot.raw_state==KLIPPER_PRINTING||snapshot.raw_state==KLIPPER_PAUSED)return false;
        reset_calls++;result_reset_pending=true;snapshot.state=snapshot.raw_state=KLIPPER_STANDBY;snapshot.filename[0]=0;snapshot.revision++;
    }
    return record(names[action],value);
}
bool klipper_client_send_gcode_at(const char *script,uint32_t expected_generation,uint32_t context) {
    if(!klipper_client_action(K_ACTION_GCODE,script,expected_generation))return false;
    feedback_result=(klipper_command_result_t){.id=104,.generation=expected_generation,.context=context};
    snprintf(feedback_result.command,sizeof(feedback_result.command),"%s",script);
    return true;
}
bool klipper_client_send_gcode(const char *script) { return record("gcode", script); }
bool klipper_client_emergency_stop(void) { return record("emergency", ""); }
bool klipper_client_shutdown_host(void) { return record("shutdown", ""); }
bool klipper_client_print_file(const char *name) { return record("print", name); }
bool klipper_client_run_macro(const char *name) { return record("macro", name); }
bool klipper_client_exclude_object(const char *name) { return record("exclude", name); }
bool klipper_client_set_print_tuning(klipper_tuning_t control,unsigned percent,uint32_t expected_generation,uint32_t expected_job_id) {
    if((unsigned)control>=3)return false;
    tuning_calls[control]++;tuning_last[control]=percent;
    backend_calls++;printf("CALL tuning control=%u percent=%u generation=%u job=%u\n",control,percent,expected_generation,expected_job_id);
    unsigned minimum=control==K_TUNING_SPEED?10:control==K_TUNING_FLOW?50:0;
    unsigned maximum=control==K_TUNING_SPEED?200:control==K_TUNING_FLOW?150:100;
    return expected_generation==network.endpoint_generation&&expected_job_id==snapshot.job_id&&
        (snapshot.raw_state==KLIPPER_PRINTING||snapshot.raw_state==KLIPPER_PAUSED)&&percent>=minimum&&percent<=maximum;
}
bool klipper_client_set_print_tuning_with_context(klipper_tuning_t control,unsigned percent,uint32_t generation,uint32_t job,uint32_t context) {
    if(!klipper_client_set_print_tuning(control,percent,generation,job))return false;
    feedback_result=(klipper_command_result_t){.id=104,.generation=generation,.context=context};
    static const char *const commands[]={"M220","M221","M106"};
    snprintf(feedback_result.command,sizeof(feedback_result.command),"%s",commands[control]);
    return true;
}
void klipper_client_fetch_files(void) { record("fetch_files", ""); }
void klipper_client_fetch_macros(void) { record("fetch_macros", ""); }
void klipper_client_fetch_objects(void) { record("fetch_objects", ""); }
void klipper_client_fetch_metadata(const char *name) { record("fetch_metadata", name); }

static void mock_view(unsigned view) {
    memset(&snapshot, 0, sizeof(snapshot)); memset(&network, 0, sizeof(network)); memset(&metadata, 0, sizeof(metadata));
    network.wifi_ready = true; network.has_ip = true; network.brightness = 80; network.saver_brightness=DEVICE_NETWORK_DEFAULT_SAVER_BRIGHTNESS; network.sleep_minutes = 10;
    memset(&network_settings,0,sizeof(network_settings));network_settings.ready=true;network_settings.revision=1;
    strcpy(network_settings.message,"Listo para configurar");
    memset(&last_job,0,sizeof(last_job));memset(&last_metadata,0,sizeof(last_metadata));
    strcpy(network.ip, "192.168.1.80"); strcpy(network.moonraker_host, "192.168.1.120"); network.moonraker_port=7125;
    snapshot.available=true; snapshot.socket_connected=true; snapshot.initial_snapshot=true; snapshot.revision=view+1;
    snapshot.metadata_revision=view+1; snapshot.endpoint_generation=network.endpoint_generation=1;
    snapshot.state = view==2 ? KLIPPER_PRINTING : view==3 ? KLIPPER_PAUSED : view==9 ? KLIPPER_ERROR : KLIPPER_STANDBY;
    snapshot.raw_state=snapshot.state;snapshot.job_id=42;
    snapshot.speed_factor_valid=snapshot.flow_factor_valid=snapshot.fan_speed_valid=true;
    snapshot.speed_percent=100;snapshot.flow_percent=100;snapshot.fan_percent=50;
    snapshot.hotend_temperature=207.4f; snapshot.hotend_target=210; snapshot.bed_temperature=59.8f; snapshot.bed_target=60;
    snapshot.progress=63.0f; snapshot.print_duration=6240; snapshot.time_remaining=3540;
    strcpy(snapshot.filename, "aperture_companion_cube.gcode"); strcpy(snapshot.connection_detail,"Moonraker conectado");
    strcpy(snapshot.error,"Extrude below minimum temp: revisa el hotend antes de continuar");
    mock_error_detail[0]=0;
    snapshot.homed_x=snapshot.homed_y=snapshot.homed_z=true; snapshot.position_x=110; snapshot.position_y=95; snapshot.position_z=12.4f;
    snapshot.bed_bounds_valid=true; snapshot.bed_max_x=220; snapshot.bed_max_y=220;
    snapshot.files_count=3; strcpy(snapshot.files[0],"aperture_companion_cube.gcode"); strcpy(snapshot.files[1],"portal_turret.gcode"); strcpy(snapshot.files[2],"calibration_cube.gcode");
    snapshot.macros_count=3; strcpy(snapshot.macros[0],"START_PRINT"); strcpy(snapshot.macros[1],"BED_MESH_CALIBRATE"); strcpy(snapshot.macros[2],"CLEAN_NOZZLE");
    snapshot.objects_count=3;
    for(unsigned i=0;i<3;i++) { klipper_object_t *object=&snapshot.objects[i]; snprintf(object->name,sizeof(object->name),"cube_%u",i+1); object->bounds_valid=true; object->min_x=20+50*i; object->max_x=55+50*i; object->min_y=40; object->max_y=100; object->center_x=(object->min_x+object->max_x)/2; object->center_y=70; object->active=i==1; }
    metadata.valid=true; metadata.revision=view+1; strcpy(metadata.filename,snapshot.filename);
    metadata.endpoint_generation=network.endpoint_generation;
    metadata.has_estimated_time=true; metadata.estimated_seconds=9780; metadata.has_weight=true; metadata.weight_grams=42.6f; metadata.has_length=true; metadata.filament_length_mm=14000;
    metadata.colors_count=2; strcpy(metadata.colors[0],"#FFAA22"); strcpy(metadata.colors[1],"#EEEEEE");
    metadata.tools_count=2;
    for(unsigned i=0;i<2;i++){metadata.tools[i].tool_index=i;metadata.tools[i].referenced=true;metadata.tools[i].has_color=true;metadata.tools[i].has_weight=true;metadata.tools[i].weight_grams=21.3f;strcpy(metadata.tools[i].color,metadata.colors[i]);}
    if(view==0) { snapshot.available=false; snapshot.socket_connected=false; snapshot.initial_snapshot=false; snapshot.state=KLIPPER_STARTUP; network.has_ip=false; }
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels) {
    (void)pixels;frame_damage_pixels+=lv_area_get_size(area);
    if(lv_display_flush_is_last(display)){
        memcpy(framebuffer,host_draw_buffer,sizeof(framebuffer));rendered_frames++;
        last_damage_pixels=frame_damage_pixels;frame_damage_pixels=0;
    }
    lv_display_flush_ready(display);
}
static void idle_read(lv_indev_t *input, lv_indev_data_t *data) { (void)input; data->state=host_pointer.state;data->point=host_pointer.point; }
static void refresh(lv_display_t *display) { lv_obj_update_layout(lv_screen_active()); lv_tick_inc(40); lv_timer_handler();lv_refr_now(display); }
static bool hidden(lv_obj_t *object) { for(lv_obj_t *p=object;p;p=lv_obj_get_parent(p)) if(lv_obj_has_flag(p,LV_OBJ_FLAG_HIDDEN)) return true; return false; }
static bool clipped_by_scroll(lv_obj_t *object,const lv_area_t *area) {
    for(lv_obj_t *parent=lv_obj_get_parent(object);parent;parent=lv_obj_get_parent(parent)) {
        lv_area_t viewport;lv_obj_get_coords(parent,&viewport);
        if(lv_obj_has_flag(parent,LV_OBJ_FLAG_SCROLLABLE)&&viewport.x1>=0&&viewport.y1>=0&&viewport.x2<800&&viewport.y2<480&&
           (area->x1<viewport.x1||area->y1<viewport.y1||area->x2>viewport.x2||area->y2>viewport.y2))return true;
    }
    return false;
}
static void inspect(lv_obj_t *object, unsigned view) {
    if(hidden(object)) return;
    if(lv_obj_check_type(object,&lv_label_class)) {
        lv_area_t a; lv_obj_get_coords(object,&a);
        printf("LABEL %s (%d,%d)-(%d,%d) %s\n",views[view],a.x1,a.y1,a.x2,a.y2,lv_label_get_text(object));
        if((a.x1<0||a.y1<0||a.x2>=800||a.y2>=480)&&!clipped_by_scroll(object,&a)) { clipped_labels++; printf("CLIP %s (%d,%d)-(%d,%d) %s\n",views[view],a.x1,a.y1,a.x2,a.y2,lv_label_get_text(object)); }
        const char *text=lv_label_get_text(object);unsigned expected=0;
        if(!strcmp(text,"IMPRIMIENDO"))expected=0xfff0d8;
        if(!strcmp(text,"EN PAUSA"))expected=0xfff0d8;
        if(!strcmp(text,"ERROR DE KLIPPER"))expected=0xd94a3a;
        if(expected&&inspect_colors) { unsigned actual=lv_color_to_u32(lv_obj_get_style_text_color(object,0))&0xffffff;printf("TITLE_COLOR %s %06x expected=%06x\n",views[view],actual,expected);if(actual!=expected)exit(10); }
    }
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++) inspect(lv_obj_get_child(object,i),view);
}
static lv_obj_t *find_label(lv_obj_t *object,const char *text) {
    if(hidden(object)) return NULL;
    if(lv_obj_check_type(object,&lv_label_class)&&strcmp(lv_label_get_text(object),text)==0) {
        for(lv_obj_t *parent=lv_obj_get_parent(object);parent;parent=lv_obj_get_parent(parent)) if(lv_obj_check_type(parent,&lv_button_class)) return object;
    }
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++) { lv_obj_t *found=find_label(lv_obj_get_child(object,i),text); if(found) return found; }
    return NULL;
}
static bool click_text(const char *text) {
    char decoded[512];
    if(strncmp(text,"hex:",4)==0) {
        unsigned index=0, value; const char *hex=text+4;
        while(hex[0]&&hex[1]&&index+1<sizeof(decoded)) { if(sscanf(hex,"%2x",&value)!=1) return false; decoded[index++]=(char)value; hex+=2; }
        decoded[index]=0; text=decoded;
    }
    lv_obj_t *object=find_label(lv_layer_top(),text);
    if(!object) object=find_label(lv_screen_active(),text);
    while(object&&!lv_obj_check_type(object,&lv_button_class)) object=lv_obj_get_parent(object);
    if(!object||lv_obj_has_state(object,LV_STATE_DISABLED)) { printf("MISSING_BUTTON %s\n",text); return false; }
    events_sent++; printf("CLICK %s\n",text); lv_obj_send_event(object,LV_EVENT_CLICKED,NULL); return true;
}
static lv_obj_t *find_type(lv_obj_t *object,const lv_obj_class_t *type,unsigned *index) {
    if(hidden(object)) return NULL;
    if(lv_obj_check_type(object,type)) { if(!*index) return object; (*index)--; }
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++) { lv_obj_t *found=find_type(lv_obj_get_child(object,i),type,index); if(found) return found; }
    return NULL;
}
static lv_obj_t *visible_widget(const lv_obj_class_t *type,unsigned index) {
    unsigned remaining=index;lv_obj_t *object=find_type(lv_layer_top(),type,&remaining);
    if(object)return object;return find_type(lv_screen_active(),type,&remaining);
}
static lv_obj_t *image_for_source(lv_obj_t *object,const lv_image_dsc_t *source) {
    if(hidden(object))return NULL;
    if(lv_obj_check_type(object,&lv_image_class)&&lv_image_get_src(object)==source)return object;
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++){lv_obj_t *found=image_for_source(lv_obj_get_child(object,i),source);if(found)return found;}return NULL;
}
static bool artwork_visible(lv_obj_t *image,unsigned expected,const char *name) {
    if(!image||hidden(image))return false;
    const lv_image_dsc_t *source=lv_image_get_src(image);if(!source||!source->data_size)return false;
    unsigned width=source->header.w*lv_image_get_scale_x(image)/256,height=source->header.h*lv_image_get_scale_y(image)/256;
    lv_area_t area,parent;lv_obj_get_coords(image,&area);lv_obj_get_coords(lv_obj_get_parent(image),&parent);
    area.x2=area.x1+(int)width-1;area.y2=area.y1+(int)height-1;
    bool pass=width>0&&height>0&&area.x1>=0&&area.y1>=0&&area.x2<800&&area.y2<480&&
        area.x1>=parent.x1&&area.y1>=parent.y1&&area.x2<=parent.x2&&area.y2<=parent.y2;
    if(expected)pass=pass&&abs((int)width-(int)expected)<=1&&abs((int)height-(int)expected)<=1;
    uint16_t background=lv_color_to_u16(lv_obj_get_style_bg_color(lv_obj_get_parent(image),0));unsigned visible=0;
    bool tinted=lv_obj_get_style_image_recolor_opa(image,0)==LV_OPA_COVER;
    uint16_t tint=lv_color_to_u16(lv_obj_get_style_image_recolor(image,0));
    if(pass)for(int y=area.y1;y<=area.y2;y++)for(int x=area.x1;x<=area.x2;x++){
        uint16_t pixel=framebuffer[y*800+x];if(tinted?pixel==tint:pixel!=background)visible++;
    }
    pass=pass&&visible>=8;
    printf("ARTWORK %s visual=%ux%u pixels=%u bounds=(%d,%d)-(%d,%d) %s\n",name,width,height,visible,area.x1,area.y1,area.x2,area.y2,pass?"PASS":"FAIL");return pass;
}
static bool button_artwork(const char *caption,const char *semantic,unsigned size) {
    lv_obj_t *label=find_label(lv_screen_active(),caption),*button=label;
    while(button&&!lv_obj_check_type(button,&lv_button_class))button=lv_obj_get_parent(button);
    const lv_image_dsc_t *source=portal_icon_for_symbol(semantic);if(!button||!source)return false;
    lv_obj_t *image=image_for_source(button,source);bool pass=artwork_visible(image,size,semantic);
    if(image&&label){
        lv_area_t a,b;lv_obj_get_coords(image,&a);lv_obj_get_coords(label,&b);
        a.x2=a.x1+source->header.w*lv_image_get_scale_x(image)/256-1;a.y2=a.y1+source->header.h*lv_image_get_scale_y(image)/256-1;
        if(!(a.x2<b.x1||a.x1>b.x2||a.y2<b.y1||a.y1>b.y2))pass=false;
    }
    unsigned second=1;if(button&&find_type(button,&lv_image_class,&second))pass=false;
    printf("EXPECT_BUTTON_ARTWORK %s icon=%s size=%u text_clear=1 %s\n",caption,semantic,size,pass?"PASS":"FAIL");return pass;
}
static bool grid_artwork(void) {
    static const char *captions[3][6]={{"IMPRIMIR","Control manual","ÚLTIMO TRABAJO","AJUSTES","MACROS","PARADA DE EMERGENCIA"},
        {"PAUSAR","CANCELAR IMPRESIÓN","MACROS","EXCLUIR OBJETOS","AJUSTES","PARADA DE EMERGENCIA"},
        {"REANUDAR","CANCELAR IMPRESIÓN","EXTRUIR 50 mm\nde filamento","RETRAER 50 mm\nde filamento","AJUSTES","PARADA DE EMERGENCIA"}};
    static const unsigned sizes[3][6]={{75,61,56,62,56,70},{56,56,56,56,62,70},{56,56,56,56,62,70}};
    static const char *semantics[3][6]={{"print","move","last_job","settings","macro","stop"},
        {"pause","cancel","macro","exclude","settings","stop"},{"play","cancel","extrude","retract","settings","stop"}};
    unsigned mode=find_label(lv_screen_active(),"PAUSAR")?1:find_label(lv_screen_active(),"REANUDAR")?2:0;
    bool pass=true;for(unsigned i=0;i<6;i++)if(!button_artwork(captions[mode][i],semantics[mode][i],sizes[mode][i]))pass=false;
    printf("EXPECT_GRID_ARTWORK six visible semantic icons %s\n",pass?"PASS":"FAIL");return pass;
}
static unsigned matching_images(lv_obj_t *object,unsigned source,unsigned visual) {
    if(hidden(object))return 0;unsigned count=0;
    if(lv_obj_check_type(object,&lv_image_class)) {
        const lv_image_dsc_t *image=lv_image_get_src(object);
        if(image&&image->header.magic==LV_IMAGE_HEADER_MAGIC&&image->header.cf==LV_COLOR_FORMAT_RAW_ALPHA&&image->data_size) {
            unsigned width=image->header.w*lv_image_get_scale_x(object)/256;
            unsigned height=image->header.h*lv_image_get_scale_y(object)/256;
            printf("THUMBNAIL source=%ux%u visual=%ux%u scale=%d\n",image->header.w,image->header.h,width,height,lv_image_get_scale(object));
            if(image->header.w==source&&image->header.h==source&&abs((int)width-(int)visual)<=1&&abs((int)height-(int)visual)<=1)count++;
        }
    }
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++)count+=matching_images(lv_obj_get_child(object,i),source,visual);
    return count;
}
static unsigned thumbnail_count(lv_obj_t *object) {
    if(hidden(object))return 0;unsigned count=0;
    if(lv_obj_check_type(object,&lv_image_class)){const lv_image_dsc_t *image=lv_image_get_src(object);if(image&&image->header.magic==LV_IMAGE_HEADER_MAGIC&&image->header.cf==LV_COLOR_FORMAT_RAW_ALPHA&&image->data_size)count++;}
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++)count+=thumbnail_count(lv_obj_get_child(object,i));return count;
}
static bool expect_core(const char *state) {
    const char *const names[]={"base","complete","cancelled","offline","error","warning"};
    const lv_image_dsc_t *const images[]={&portal_core_base,&portal_core_complete,&portal_core_cancelled,
        &portal_core_offline,&portal_core_error,&portal_core_warning};
    for(unsigned i=0;i<sizeof(images)/sizeof(images[0]);++i){
        if(strcmp(state,names[i]))continue;
        bool warning=images[i]==&portal_core_warning;
        lv_obj_t *object=image_for_source(warning?lv_layer_top():lv_screen_active(),images[i]);
        if(!object)return false;
        lv_area_t bounds;lv_obj_get_coords(object,&bounds);
        bool pass=bounds.x1==(warning?155:40)&&bounds.y1==(warning?77:11)&&
            artwork_visible(object,0,state);
        printf("EXPECT_CORE %s aligned and visible %s\n",state,pass?"PASS":"FAIL");return pass;
    }
    return false;
}
static unsigned swatch_count(lv_obj_t *object,unsigned desired,bool filter_color) {
    if(hidden(object))return 0;unsigned count=0,width=lv_obj_get_width(object),height=lv_obj_get_height(object);
    if(width>=8&&width<=48&&height>=8&&height<=48&&lv_obj_get_style_bg_opa(object,0)==LV_OPA_COVER){
        unsigned color=lv_color_to_u32(lv_obj_get_style_bg_color(object,0))&0xffffff;bool match=false;
        for(unsigned i=0;i<metadata.tools_count;i++)if(metadata.tools[i].has_color&&color==strtoul(metadata.tools[i].color+1,NULL,16)){match=true;break;}
        if(match&&(!filter_color||color==desired))count++;
    }
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++)count+=swatch_count(lv_obj_get_child(object,i),desired,filter_color);return count;
}
static lv_obj_t *swatch_at(lv_obj_t *object,unsigned *index) {
    if(hidden(object))return NULL;unsigned width=lv_obj_get_width(object),height=lv_obj_get_height(object);
    if(width>=8&&width<=48&&height>=8&&height<=48&&lv_obj_get_style_bg_opa(object,0)==LV_OPA_COVER){
        unsigned color=lv_color_to_u32(lv_obj_get_style_bg_color(object,0))&0xffffff;bool match=false;
        for(unsigned i=0;i<metadata.tools_count;i++)if(metadata.tools[i].has_color&&color==strtoul(metadata.tools[i].color+1,NULL,16)){match=true;break;}
        if(match){if(!*index)return object;(*index)--;}
    }
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++){lv_obj_t *found=swatch_at(lv_obj_get_child(object,i),index);if(found)return found;}return NULL;
}
static bool grid_geometry(void) {
    lv_obj_t *screen=lv_screen_active();unsigned count=0,slots=0;
    for(unsigned i=0;i<lv_obj_get_child_count(screen);i++) {
        lv_obj_t *button=lv_obj_get_child(screen,i);if(!lv_obj_check_type(button,&lv_button_class)||hidden(button))continue;
        lv_area_t a;lv_obj_get_coords(button,&a);int x=lv_obj_get_x(button),y=lv_obj_get_y(button);
        if(x!=360&&x!=580)continue;
        if(y!=12&&y!=168&&y!=324)continue;
        if(lv_obj_get_width(button)!=208||lv_obj_get_height(button)!=144)return false;
        if(a.x1<0||a.y1<0||a.x2>=800||a.y2>=480)return false;
        unsigned row=y==12?0:y==168?1:2;slots|=1u<<(row*2+(x==580));count++;
    }
    printf("EXPECT_GRID six equal buttons=%u slots=%u %s\n",count,slots,count==6&&slots==63?"PASS":"FAIL");return count==6&&slots==63;
}
static void set_thumbnail(unsigned size) {
    metadata.thumbnail_size=0;metadata.thumbnail_width=metadata.thumbnail_height=0;
    if(size){const unsigned char *data=size==100?fixture_png_100:fixture_png_300;
        metadata.thumbnail_size=size==100?sizeof(fixture_png_100):sizeof(fixture_png_300);
        memcpy(metadata.thumbnail_png,data,metadata.thumbnail_size);metadata.thumbnail_width=metadata.thumbnail_height=size;}
    metadata.revision++;snapshot.metadata_revision=metadata.revision;snapshot.revision++;
}
static bool any_label(lv_obj_t *object,const char *text) {
    if(hidden(object))return false;
    if(lv_obj_check_type(object,&lv_label_class)&&!strcmp(lv_label_get_text(object),text))return true;
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++)if(any_label(lv_obj_get_child(object,i),text))return true;
    return false;
}
static lv_obj_t *plain_label(lv_obj_t *object,const char *text) {
    if(hidden(object))return NULL;
    if(lv_obj_check_type(object,&lv_label_class)&&!strcmp(lv_label_get_text(object),text))return object;
    for(unsigned i=0;i<lv_obj_get_child_count(object);i++){lv_obj_t *found=plain_label(lv_obj_get_child(object,i),text);if(found)return found;}
    return NULL;
}
static lv_obj_t *tuning_row(unsigned index) {
    static const char *captions[]={"VELOCIDAD","FLUJO","VENTILADOR CAPA"};
    if(index>=3)return NULL;
    lv_obj_t *label=plain_label(lv_layer_top(),captions[index]);
    return label?lv_obj_get_parent(label):NULL;
}
static lv_obj_t *tuning_button(unsigned row,const char *operation) {
    unsigned index=!strcmp(operation,"minus")?0:!strcmp(operation,"plus")?1:!strcmp(operation,"apply")?2:3;
    lv_obj_t *panel=tuning_row(row);return panel&&index<3?find_type(panel,&lv_button_class,&index):NULL;
}
/* Resolve the original control palette in LVGL, including interaction states
 * and every part of sliders, keyboards, dropdowns and scrollbars. The visual
 * regression compares these records against the pre-change firmware UI. */
static void style_record(lv_obj_t *obj,unsigned control,unsigned node,unsigned state,unsigned part) {
    printf("STYLE %u %u %u %u %06x %u %06x %u %u %06x %06x %u %06x %u %u %06x\n",
        control,node,state,part,
        lv_color_to_u32(lv_obj_get_style_bg_color(obj,part))&0xffffff,lv_obj_get_style_bg_opa(obj,part),
        lv_color_to_u32(lv_obj_get_style_border_color(obj,part))&0xffffff,lv_obj_get_style_border_opa(obj,part),lv_obj_get_style_border_width(obj,part),
        lv_color_to_u32(lv_obj_get_style_text_color(obj,part))&0xffffff,
        lv_color_to_u32(lv_obj_get_style_image_recolor(obj,part))&0xffffff,lv_obj_get_style_image_recolor_opa(obj,part),
        lv_color_to_u32(lv_obj_get_style_bg_grad_color(obj,part))&0xffffff,lv_obj_get_style_bg_grad_dir(obj,part),
        lv_obj_get_style_shadow_width(obj,part),lv_color_to_u32(lv_obj_get_style_shadow_color(obj,part))&0xffffff);
}
static void control_nodes(lv_obj_t *obj,unsigned control,unsigned *node,unsigned state) {
    if(hidden(obj))return;
    const unsigned parts[]={LV_PART_MAIN,LV_PART_INDICATOR,LV_PART_KNOB,LV_PART_ITEMS,LV_PART_CURSOR,LV_PART_SELECTED,LV_PART_SCROLLBAR};
    unsigned current=(*node)++;
    for(unsigned i=0;i<sizeof(parts)/sizeof(parts[0]);i++)style_record(obj,control,current,state,parts[i]);
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)control_nodes(lv_obj_get_child(obj,i),control,node,state);
}
static void control_styles(lv_obj_t *obj,unsigned *control,unsigned *scrollbar) {
    if(hidden(obj))return;
    bool widget=lv_obj_check_type(obj,&lv_button_class)||lv_obj_check_type(obj,&lv_dropdown_class)||
        lv_obj_check_type(obj,&lv_dropdownlist_class)||lv_obj_check_type(obj,&lv_textarea_class)||
        lv_obj_check_type(obj,&lv_keyboard_class)||lv_obj_check_type(obj,&lv_slider_class)||lv_obj_check_type(obj,&lv_switch_class);
    if(protected_styles_only&&lv_obj_check_type(obj,&lv_button_class)){
        for(unsigned i=0;i<lv_obj_get_child_count(obj);i++){
            lv_obj_t *child=lv_obj_get_child(obj,i);
            if(lv_obj_check_type(child,&lv_image_class)&&lv_image_get_src(child)!=portal_icon_for_symbol("backspace"))return;
        }
    }
    /* The sole removed control is the old, icon-only header back button. */
    if(widget&&lv_obj_check_type(obj,&lv_button_class)&&lv_obj_get_width(obj)==48&&lv_obj_get_height(obj)==44&&lv_obj_get_y(obj)==12)return;
    if(widget){
        unsigned id=(*control)++;lv_state_t saved=lv_obj_get_state(obj);
        const lv_state_t states[]={0,LV_STATE_PRESSED,LV_STATE_FOCUSED,LV_STATE_DISABLED,LV_STATE_CHECKED,LV_STATE_CHECKED|LV_STATE_PRESSED,LV_STATE_CHECKED|LV_STATE_DISABLED};
        for(unsigned s=0;s<sizeof(states)/sizeof(states[0]);s++){
            lv_obj_remove_state(obj,LV_STATE_ANY);lv_obj_add_state(obj,states[s]);lv_tick_inc(400);lv_timer_handler();
            unsigned node=0;control_nodes(obj,id,&node,states[s]);
        }
        lv_obj_remove_state(obj,LV_STATE_ANY);lv_obj_add_state(obj,saved);lv_tick_inc(400);lv_timer_handler();return;
    }
    if(lv_obj_has_flag(obj,LV_OBJ_FLAG_SCROLLABLE))style_record(obj,10000+(*scrollbar)++,0,0,LV_PART_SCROLLBAR);
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)control_styles(lv_obj_get_child(obj,i),control,scrollbar);
}
/* Snapshot the entire tree, including hidden controls, without changing states.
 * Drawing callbacks add no objects and must not affect any of these records. */
static void geometry_record(lv_obj_t *obj,const char *path) {
    lv_area_t a,hit;lv_obj_get_coords(obj,&a);lv_obj_get_click_area(obj,&hit);
    const lv_font_t *font=lv_obj_get_style_text_font(obj,0);
    printf("GEOMETRY %s %d %d %d %d hit=%d,%d,%d,%d flags=%d,%d,%d font=%u,%d,%d pad=%d,%d,%d,%d border=%d",
        path,a.x1,a.y1,lv_obj_get_width(obj),lv_obj_get_height(obj),hit.x1,hit.y1,hit.x2,hit.y2,
        lv_obj_has_flag(obj,LV_OBJ_FLAG_CLICKABLE),lv_obj_has_flag(obj,LV_OBJ_FLAG_SCROLLABLE),lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN),
        font?lv_font_get_line_height(font):0,lv_obj_get_style_text_letter_space(obj,0),lv_obj_get_style_text_line_space(obj,0),
        lv_obj_get_style_pad_left(obj,0),lv_obj_get_style_pad_right(obj,0),lv_obj_get_style_pad_top(obj,0),lv_obj_get_style_pad_bottom(obj,0),lv_obj_get_style_border_width(obj,0));
    if(lv_obj_check_type(obj,&lv_label_class)){
        printf(" label=");const unsigned char *p=(const unsigned char*)lv_label_get_text(obj);while(*p)printf("%02x",*p++);
    }
    if(lv_obj_check_type(obj,&lv_image_class)){
        const lv_image_dsc_t *src=lv_image_get_src(obj);
        printf(" image=%u,%u,%d,%d",src?src->header.w:0,src?src->header.h:0,lv_image_get_scale_x(obj),lv_image_get_scale_y(obj));
    }
    printf("\n");
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++){
        char child[256];snprintf(child,sizeof(child),"%s/%u",path,i);geometry_record(lv_obj_get_child(obj,i),child);
    }
}
static bool laboratory_buttons(lv_obj_t *obj,unsigned *count,bool startup) {
    if(hidden(obj))return true;
    if(lv_obj_check_type(obj,&lv_button_class)){
        if(lv_obj_get_width(obj)<=0||lv_obj_get_height(obj)<=0)return false;
        unsigned first=0;
        lv_obj_t *image=find_type(obj,&lv_image_class,&first);
        if(image){
            const lv_image_dsc_t *src=lv_image_get_src(image);
            uint32_t fill=lv_color_to_u32(lv_obj_get_style_bg_color(obj,0))&0xffffff;
            uint32_t ink=lv_color_to_u32(lv_obj_get_style_image_recolor(image,0))&0xffffff;
            if(src==portal_icon_for_symbol("stop")&&(fill!=0xfbe5e8||ink!=0xa32632)&&
                !lv_obj_has_state(obj,LV_STATE_DISABLED))return false;
            if(src==portal_icon_for_symbol("cancel")&&(fill!=0xf3eaed||ink!=0x753544))return false;
            (*count)++;
        }
    }
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)if(!laboratory_buttons(lv_obj_get_child(obj,i),count,startup))return false;
    return true;
}
static bool surface_contract(lv_obj_t *obj,unsigned *panels,bool startup) {
    if(hidden(obj))return true;
    if(lv_obj_get_class(obj)==&lv_obj_class&&lv_obj_get_width(obj)>=250&&lv_obj_get_height(obj)>=90&&
       lv_obj_get_style_bg_opa(obj,0)==LV_OPA_COVER&&obj!=lv_screen_active()&&obj!=lv_layer_top()){
        int w=lv_obj_get_width(obj),h=lv_obj_get_height(obj);
        bool medium=(w==338&&h==406)||(w==442&&h==394)||(w==312&&h==312);
        bool status=(w==332&&(h==149||h==200))||(w==776&&h==456&&lv_obj_get_parent(obj)==lv_screen_active())||
            (w==736&&h==92);
        bool warning=w==520&&h==340&&image_for_source(obj,&portal_core_warning);
        unsigned expected=startup?(w==376?0xf7c768:0xeeae2e):w==800&&h==480?0xd9d9d9:w==306&&h==210?0x081418:
            w==296&&h==134?0x202429:warning||(w==263&&h==117)?0xe9eef1:medium?0x7f7f7f:status?0x344550:0xeeeeee;
        unsigned actual=lv_color_to_u32(lv_obj_get_style_bg_color(obj,0))&0xffffff;
        if(actual!=expected||lv_obj_get_style_bg_grad_dir(obj,0)!=LV_GRAD_DIR_NONE||lv_obj_get_style_shadow_width(obj,0)){
            printf("SURFACE_FAIL %dx%d color=%06x expected=%06x\n",w,h,actual,expected);return false;
        }
        (*panels)++;
    }
    if(lv_obj_check_type(obj,&lv_label_class)&&(!strcmp(lv_label_get_text(obj),"APERTURE")||!strcmp(lv_label_get_text(obj),"KLIPPER")))return false;
    for(unsigned i=0;i<lv_obj_get_child_count(obj);i++)if(!surface_contract(lv_obj_get_child(obj,i),panels,startup))return false;
    return true;
}
static bool action(const char *text) {
    unsigned index,value;
    if(!strncmp(text,"@utf8:",6)){
        char decoded[1024];unsigned n=0;
        for(const char *hex=text+6;hex[0]&&hex[1]&&n+1<sizeof(decoded);hex+=2){
            if(sscanf(hex,"%2x",&value)!=1)return false;
            decoded[n++]=(char)value;
        }
        decoded[n]='\0';return action(decoded);
    }
    if(!strcmp(text,"@expectdirectframe")){
        uint16_t *before=malloc(sizeof(framebuffer));if(!before)return false;
        lv_refr_now(lv_display_get_default());memcpy(before,framebuffer,sizeof(framebuffer));
        lv_obj_invalidate(lv_screen_active());lv_obj_invalidate(lv_layer_top());lv_refr_now(lv_display_get_default());
        bool pass=!memcmp(before,framebuffer,sizeof(framebuffer));free(before);
        printf("EXPECT_DIRECT_FRAME matches_full_redraw=1 %s\n",pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expectsaver:",13)){
        bool pass=any_label(lv_layer_sys(),text+13);
        printf("EXPECT_SAVER %s %s\n",text+13,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expectnotsaver:",16))
        return !any_label(lv_layer_sys(),text+16);
    if(!strcmp(text,"@expectnosaver"))return !any_label(lv_layer_sys(),"Toca para volver");
    if(sscanf(text,"@expectbrightness:%u",&value)==1){
        bool pass=panel_brightness==value;
        printf("EXPECT_BRIGHTNESS %u %s\n",panel_brightness,pass?"PASS":"FAIL");return pass;
    }
    if(sscanf(text,"@networkbrightness:%u",&value)==1&&value<=100){network.brightness=value;return true;}
    if(sscanf(text,"@networksaverbrightness:%u",&value)==1&&value<=100){network.saver_brightness=value;return true;}
    if(!strcmp(text,"@expecteyemove")||!strcmp(text,"@expectnormaleyemove")||!strcmp(text,"@expectwarningeyemove")){
        bool saver=!strcmp(text,"@expecteyemove"),warning=!strcmp(text,"@expectwarningeyemove");
        unsigned x=saver?113:warning?190:85,y=saver?170:warning?120:75;
        unsigned w=warning?150:170,h=warning?155:165;
        uint16_t *before=malloc(sizeof(framebuffer));
        if(!before)return false;
        lv_refr_now(lv_display_get_default());
        memcpy(before,framebuffer,sizeof(framebuffer));
        lv_tick_inc(3500);portal_ui_tick();lv_timer_handler();
        lv_tick_inc(200);portal_ui_tick();lv_timer_handler();lv_refr_now(lv_display_get_default());
        bool changed=false,outside=false;
        for(unsigned row=0;row<480;++row)for(unsigned col=0;col<800;++col){
            if(before[row*800+col]==framebuffer[row*800+col])continue;
            if(col>=x&&col<x+w&&row>=y&&row<y+h)changed=true;
            else outside=true;
        }
        free(before);
        bool pass=changed&&!outside&&last_damage_pixels>0&&last_damage_pixels<=w*h;
        printf("EXPECT_EYE changed=%d outside=%d damage=%"PRIu32" %s\n",changed,outside,last_damage_pixels,pass?"PASS":"FAIL");
        return pass;
    }
    if(sscanf(text,"@expectdamage:%u",&value)==1){
        bool pass=last_damage_pixels>0&&(uint64_t)last_damage_pixels*100<=(uint64_t)800*480*value;
        printf("EXPECT_DAMAGE pixels=%"PRIu32" limit_percent=%u %s\n",last_damage_pixels,value,pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@expectidleframes")){
        lv_tick_inc(500);lv_timer_handler();lv_refr_now(lv_display_get_default());uint32_t before=rendered_frames;
        for(unsigned i=0;i<12;i++){lv_tick_inc(250);portal_ui_tick();lv_timer_handler();lv_refr_now(lv_display_get_default());}
        bool pass=before==rendered_frames;
        printf("EXPECT_IDLE_FRAMES extra=%"PRIu32" %s\n",rendered_frames-before,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@displaycomplete:",17)){
        display_settings_request_t request;if(!display_settings_begin(&display_mailbox,&request))return true;
        esp_err_t result=!strcmp(text+17,"ok")?ESP_OK:ESP_FAIL;display_writes++;
        display_settings_complete(&display_mailbox,&request,result,12000);
        printf("CALL settings_persist brightness=%u sleep=%u result=%d saver=%u\n",request.brightness,request.sleep_minutes,result,request.saver_brightness);return true;
    }
    if(sscanf(text,"@expectdisplaypending:%u",&value)==1){
        bool pass=display_mailbox.status.pending==(value!=0);
        printf("EXPECT_DISPLAY_PENDING %u %s\n",display_mailbox.status.pending,pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@manyfiles")){
        snapshot.files_count=KLIPPER_FILES_MAX<12?KLIPPER_FILES_MAX:12;
        for(unsigned i=3;i<snapshot.files_count;i++)snprintf(snapshot.files[i],sizeof(snapshot.files[i]),"fixture_%02u.gcode",i);
        snapshot.revision++;return true;
    }
    if(!strcmp(text,"@expectfilescrolled")){
        lv_obj_t *row=find_label(lv_screen_active(),snapshot.files[0]);
        while(row&&!lv_obj_check_type(row,&lv_button_class))row=lv_obj_get_parent(row);
        int offset=row?lv_obj_get_scroll_y(lv_obj_get_parent(row)):0;
        printf("EXPECT_FILES_SCROLLED offset=%d %s\n",offset,offset>0?"PASS":"FAIL");return offset>0;
    }
    if(!strncmp(text,"@pointer:",9)){
        int pressed,x,y,ms;if(sscanf(text+9,"%d:%d:%d:%d",&pressed,&x,&y,&ms)!=4||
            pressed<0||pressed>1||x<0||x>=800||y<0||y>=480||ms<1||ms>10000)return false;
        host_pointer.state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
        host_pointer.point.x=x;host_pointer.point.y=y;lv_tick_inc(ms);lv_indev_read(host_input);
        events_sent++;printf("POINTER pressed=%d x=%d y=%d ms=%d\n",pressed,x,y,ms);return true;
    }
    if(!strncmp(text,"@drag:",6)){
        int x,y,dx,dy,steps,ms;
        if(sscanf(text+6,"%d:%d:%d:%d:%d:%d",&x,&y,&dx,&dy,&steps,&ms)!=6||
           steps<1||steps>1000||ms<1||ms>1000||x<0||x>=800||x+dx<0||x+dx>=800||
           y<0||y>=480||y+dy<0||y+dy>=480)return false;
        for(int step=0;step<=steps;step++){
            host_pointer.state=LV_INDEV_STATE_PRESSED;
            host_pointer.point.x=x+dx*step/steps;host_pointer.point.y=y+dy*step/steps;
            lv_tick_inc(ms);lv_indev_read(host_input);
        }
        host_pointer.state=LV_INDEV_STATE_RELEASED;lv_tick_inc(ms);lv_indev_read(host_input);
        events_sent+=steps+2;printf("DRAG dx=%d dy=%d samples=%d period_ms=%d\n",dx,dy,steps+2,ms);return true;
    }
    if(!strncmp(text,"@expectwrites:",14)){
        unsigned display,volume;if(sscanf(text+14,"%u:%u",&display,&volume)!=2)return false;
        bool pass=display_writes==display&&volume_writes==volume;
        printf("EXPECT_WRITES display=%u volume=%u %s\n",display_writes,volume_writes,pass?"PASS":"FAIL");return pass;
    }
    if(sscanf(text,"@expectselectedfile:%u",&index)==1){
        if(index>=snapshot.files_count)return false;
        /* The preview title shows the selection independently of list styling. */
        bool pass=false;lv_obj_t *screen=lv_screen_active();
        for(unsigned i=0;i<lv_obj_get_child_count(screen);i++){
            lv_obj_t *child=lv_obj_get_child(screen,i);
            if(lv_obj_check_type(child,&lv_label_class)&&!strcmp(lv_label_get_text(child),snapshot.files[index]))pass=true;
        }
        printf("EXPECT_SELECTED_FILE %u %s\n",index,pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@expectlaboratory")){unsigned count=0;portal_inspection_status_t status;portal_ui_inspection_status(&status);
        bool pass=laboratory_buttons(lv_screen_active(),&count,status.view==PORTAL_STARTUP)&&laboratory_buttons(lv_layer_top(),&count,false);
        printf("EXPECT_LABORATORY icon_buttons=%u palette_exact=1 %s\n",count,pass?"PASS":"FAIL");return pass;}
    if(sscanf(text,"@keyboardmode:%u",&value)==1){lv_obj_t *keyboard=visible_widget(&lv_keyboard_class,0);if(!keyboard||value>LV_KEYBOARD_MODE_NUMBER)return false;lv_keyboard_set_mode(keyboard,(lv_keyboard_mode_t)value);return true;}
    if(!strcmp(text,"@geometry")){lv_obj_update_layout(lv_screen_active());lv_obj_update_layout(lv_layer_top());geometry_record(lv_screen_active(),"screen");geometry_record(lv_layer_top(),"top");return true;}
    if(!strcmp(text,"@protectedstyles")){protected_styles_only=true;unsigned control=0,scrollbar=0;control_styles(lv_screen_active(),&control,&scrollbar);control_styles(lv_layer_top(),&control,&scrollbar);protected_styles_only=false;printf("STYLES controls=%u scrollbars=%u\n",control,scrollbar);return true;}
    if(!strcmp(text,"@expectlayout")){
        unsigned panels=0;lv_obj_t *screen=lv_screen_active();
        portal_inspection_status_t status;portal_ui_inspection_status(&status);
        bool startup=status.view==PORTAL_STARTUP;
        unsigned background=startup?0xeeae2e:0xd9d9d9;
        bool pass=(lv_color_to_u32(lv_obj_get_style_bg_color(screen,0))&0xffffff)==background&&
            lv_obj_get_style_bg_image_src(screen,0)==(startup?NULL:&portal_background_image)&&
            surface_contract(screen,&panels,startup)&&surface_contract(lv_layer_top(),&panels,false)&&panels>0;
        for(unsigned i=0;i<lv_obj_get_child_count(screen);i++){
            lv_obj_t *child=lv_obj_get_child(screen,i);
            if(!hidden(child)&&((!startup&&lv_obj_get_y(child)<10)||
               (lv_obj_get_width(child)==776&&lv_obj_get_height(child)==48)))pass=false;
        }
        printf("EXPECT_LAYOUT headerless=1 background=%06x panels=%u %s\n",background,panels,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@swipe:",7)){
        int x,y;if(sscanf(text+7,"%d:%d",&x,&y)!=2)return false;
        for(int step=0;step<=5;step++){
            host_pointer.state=LV_INDEV_STATE_PRESSED;host_pointer.point.x=x+step*30;host_pointer.point.y=y;
            lv_tick_inc(12);lv_indev_read(host_input);
        }
        host_pointer.state=LV_INDEV_STATE_RELEASED;lv_tick_inc(12);lv_indev_read(host_input);
        events_sent++;printf("SWIPE_RIGHT pointer_samples=7\n");return true;
    }
    if(sscanf(text,"@opendropdown:%u",&index)==1){lv_obj_t *dropdown=visible_widget(&lv_dropdown_class,index);if(!dropdown)return false;lv_dropdown_open(dropdown);return true;}
    if(!strcmp(text,"@keyboardcancel")){lv_obj_t *keyboard=visible_widget(&lv_keyboard_class,0);if(!keyboard)return false;events_sent++;lv_obj_send_event(keyboard,LV_EVENT_CANCEL,NULL);return true;}
    if(!strcmp(text,"@styles")){unsigned control=0,scrollbar=0;control_styles(lv_screen_active(),&control,&scrollbar);control_styles(lv_layer_top(),&control,&scrollbar);printf("STYLES controls=%u scrollbars=%u\n",control,scrollbar);return true;}
    if(!strncmp(text,"@savebutton:",12)){
        saved_button=find_label(lv_layer_top(),text+12);if(!saved_button)saved_button=find_label(lv_screen_active(),text+12);
        while(saved_button&&!lv_obj_check_type(saved_button,&lv_button_class))saved_button=lv_obj_get_parent(saved_button);
        return saved_button!=NULL;
    }
    if(!strcmp(text,"@clicksaved")){
        if(!saved_button||!lv_obj_is_valid(saved_button))return false;events_sent++;
        printf("STALE_BUTTON_CLICK\n");lv_obj_send_event(saved_button,LV_EVENT_CLICKED,NULL);return true;
    }
    if(!strncmp(text,"@tuningclick:",13)||!strncmp(text,"@tuningforce:",13)){
        char operation[16];if(sscanf(text+13,"%u:%15s",&index,operation)!=2)return false;
        lv_obj_t *button=tuning_button(index,operation);bool force=!strncmp(text,"@tuningforce:",13);
        if(!button||(!force&&lv_obj_has_state(button,LV_STATE_DISABLED)))return false;
        events_sent++;printf("TUNING_CLICK row=%u op=%s force=%u\n",index,operation,force);lv_obj_send_event(button,LV_EVENT_CLICKED,NULL);return true;
    }
    if(!strncmp(text,"@expecttuningdisabled:",22)){
        char operation[16];if(sscanf(text+22,"%u:%15s",&index,operation)!=2)return false;
        lv_obj_t *button=tuning_button(index,operation);bool pass=button&&lv_obj_has_state(button,LV_STATE_DISABLED);
        printf("EXPECT_TUNING_DISABLED row=%u %s %s\n",index,operation,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expecttuningvalue:",19)){
        int consumed=0;if(sscanf(text+19,"%u:%n",&index,&consumed)!=1||!consumed)return false;
        lv_obj_t *panel=tuning_row(index);bool pass=panel&&any_label(panel,text+19+consumed);
        printf("EXPECT_TUNING_VALUE row=%u value=%s %s\n",index,text+19+consumed,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expecttuningcalls:",19)){
        unsigned expected[3];if(sscanf(text+19,"%u:%u:%u",expected,expected+1,expected+2)!=3)return false;
        bool pass=!memcmp(expected,tuning_calls,sizeof(expected));
        printf("EXPECT_TUNING_CALLS %u:%u:%u %s\n",tuning_calls[0],tuning_calls[1],tuning_calls[2],pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@tuningvalue:",13)){
        unsigned valid;char input[48];if(sscanf(text+13,"%u:%u:%47s",&index,&valid,input)!=3||index>=3)return false;
        float percent=!strcmp(input,"nan")?NAN:!strcmp(input,"inf")?INFINITY:(float)atof(input);
        if(index==0){snapshot.speed_factor_valid=valid!=0;snapshot.speed_percent=percent;}
        else if(index==1){snapshot.flow_factor_valid=valid!=0;snapshot.flow_percent=percent;}
        else {snapshot.fan_speed_valid=valid!=0;snapshot.fan_percent=percent;}
        snapshot.revision++;return true;
    }
    if(sscanf(text,"@printreject:%u",&value)==1){print_reject=value!=0;return true;}
    if(sscanf(text,"@printresult:%u",&value)==1&&value<=KLIPPER_UNKNOWN){print_result_pending=(int)value;return true;}
    if(sscanf(text,"@jobid:%u",&value)==1){snapshot.job_id=value;snapshot.revision++;return true;}
    if(sscanf(text,"@expectview:%u",&value)==1){
        portal_inspection_status_t status;portal_ui_inspection_status(&status);bool pass=(unsigned)status.view==value;
        printf("EXPECT_VIEW %u expected=%u %s\n",(unsigned)status.view,value,pass?"PASS":"FAIL");return pass;
    }
    if(sscanf(text,"@expectstate:%u",&value)==1){bool pass=(unsigned)snapshot.state==value;printf("EXPECT_STATE %u %s\n",(unsigned)snapshot.state,pass?"PASS":"FAIL");return pass;}
    if(!strcmp(text,"@expectgridicons"))return grid_artwork();
    if(!strncmp(text,"@expectsingleline:",18)){
        lv_obj_t *caption=plain_label(lv_layer_top(),text+18);if(!caption)caption=plain_label(lv_screen_active(),text+18);
        if(!caption)return false;const lv_font_t *font=lv_obj_get_style_text_font(caption,0);
        bool pass=lv_obj_get_height(caption)==lv_font_get_line_height(font)&&
            lv_text_get_width(lv_label_get_text(caption),strlen(lv_label_get_text(caption)),font,0)<=lv_obj_get_width(caption);
        printf("EXPECT_SINGLE_LINE %s %s\n",text+18,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expectnoicon:",14)){
        const lv_image_dsc_t *source=portal_icon_for_symbol(text+14);
        bool pass=source&&!image_for_source(lv_screen_active(),source);printf("EXPECT_NO_ICON %s %s\n",text+14,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expecticon:",12)){
        char semantic[48];unsigned size;if(sscanf(text+12,"%47[^:]:%u",semantic,&size)!=2)return false;
        const lv_image_dsc_t *source=portal_icon_for_symbol(semantic);if(!source)return false;
        lv_obj_t *image=image_for_source(lv_screen_active(),source);return artwork_visible(image,size,semantic);
    }
    if(!strncmp(text,"@expectbuttonicon:",18)){
        char semantic[48];unsigned size;int length=0;if(sscanf(text+18,"%47[^:]:%u:%n",semantic,&size,&length)!=2||!length)return false;
        return button_artwork(text+18+length,semantic,size);
    }
    if(!strcmp(text,"@expectlogos")){
        const lv_image_dsc_t *sources[]={&portal_start_aperture_image,&portal_klipper_image,&portal_start_wifi_image};
        const char *names[]={"Aperture","Klipper","Wi-Fi"};
        bool pass=true;
        for(unsigned i=0;i<3;i++){
            lv_obj_t *image=image_for_source(lv_screen_active(),sources[i]);
            pass=artwork_visible(image,0,names[i])&&lv_obj_get_style_image_recolor_opa(image,0)==LV_OPA_TRANSP&&pass;
        }
        printf("EXPECT_APPROVED_LOGOS visible_without_recolor %s\n",pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@expectgrid"))return grid_geometry();
    if(!strcmp(text,"@expectprintfooter")){
        lv_obj_t *button=find_label(lv_screen_active(),"IMPRIMIR");while(button&&!lv_obj_check_type(button,&lv_button_class))button=lv_obj_get_parent(button);
        unsigned first=0;bool pass=button&&lv_obj_get_height(button)==48&&!find_type(button,&lv_image_class,&first);
        printf("EXPECT_PRINT_FOOTER text_only_48px %s\n",pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@expectfileslist")){
        bool pass=true;unsigned count=0;
        for(unsigned i=0;i<snapshot.files_count;i++){
            const char *name=strrchr(snapshot.files[i],'/');name=name?name+1:snapshot.files[i];lv_obj_t *button=find_label(lv_screen_active(),name);
            while(button&&!lv_obj_check_type(button,&lv_button_class))button=lv_obj_get_parent(button);
            if(!button){pass=false;continue;}lv_area_t a;lv_obj_get_coords(button,&a);unsigned first=0;int height=lv_obj_get_height(button);
            if(height<72||height>88||a.x2>=382||find_type(button,&lv_image_class,&first))pass=false;count++;
        }
        printf("EXPECT_FILES_LIST names_only_80px rows=%u %s\n",count,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expectcore:",12))return expect_core(text+12);
    if(sscanf(text,"@expectthumbcount:%u",&value)==1){unsigned count=thumbnail_count(lv_screen_active());printf("EXPECT_THUMB_COUNT %u %s\n",count,count==value?"PASS":"FAIL");return count==value;}
    if(sscanf(text,"@expectswatches:%u",&value)==1){unsigned count=swatch_count(lv_screen_active(),0,false);printf("EXPECT_SWATCHES %u expected=%u %s\n",count,value,count==value?"PASS":"FAIL");return count==value;}
    if(sscanf(text,"@scrollswatch:%u",&index)==1){
        lv_obj_t *object=swatch_at(lv_screen_active(),&index);if(!object)return false;events_sent++;lv_obj_scroll_to_view_recursive(object,LV_ANIM_OFF);lv_obj_update_layout(lv_screen_active());
        lv_area_t a;lv_obj_get_coords(object,&a);bool pass=a.x1>=0&&a.y1>=0&&a.x2<800&&a.y2<480;
        for(lv_obj_t *parent=lv_obj_get_parent(object);parent;parent=lv_obj_get_parent(parent))if(lv_obj_has_flag(parent,LV_OBJ_FLAG_SCROLLABLE)){lv_area_t viewport;lv_obj_get_coords(parent,&viewport);pass=pass&&a.x1>=viewport.x1&&a.y1>=viewport.y1&&a.x2<=viewport.x2&&a.y2<=viewport.y2;}
        printf("EXPECT_SCROLLED_SWATCH %s\n",pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expectcolor:",13)){
        unsigned color,expected=0;int fields=sscanf(text,"@expectcolor:#%x:%u",&color,&expected);if(fields<1)return false;
        unsigned count=swatch_count(lv_screen_active(),color,true);bool pass=fields==2?count==expected:count>0;printf("EXPECT_COLOR #%06X count=%u %s\n",color,count,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@scrollto:",10)){lv_obj_t *object=plain_label(lv_screen_active(),text+10);if(!object)return false;events_sent++;lv_obj_scroll_to_view_recursive(object,LV_ANIM_OFF);return true;}
    if(!strncmp(text,"@expectinviewport:",18)){
        lv_obj_t *object=plain_label(lv_screen_active(),text+18);if(!object)return false;lv_area_t area;lv_obj_get_coords(object,&area);
        bool pass=area.x1>=0&&area.y1>=0&&area.x2<800&&area.y2<480;
        for(lv_obj_t *parent=lv_obj_get_parent(object);parent;parent=lv_obj_get_parent(parent))if(lv_obj_has_flag(parent,LV_OBJ_FLAG_SCROLLABLE)){
            lv_area_t viewport;lv_obj_get_coords(parent,&viewport);pass=pass&&area.x1>=viewport.x1&&area.y1>=viewport.y1&&area.x2<=viewport.x2&&area.y2<=viewport.y2;}
        printf("EXPECT_IN_VIEWPORT %s %s\n",text+18,pass?"PASS":"FAIL");return pass;
    }
    if(sscanf(text,"@expectimage:%u:%u",&index,&value)==2){unsigned count=matching_images(lv_screen_active(),index,value);printf("EXPECT_IMAGE source=%u visual=%u count=%u %s\n",index,value,count,count?"PASS":"FAIL");return count>0;}
    if(sscanf(text,"@thumbnail:%u",&value)==1){if(value!=0&&value!=100&&value!=300)return false;set_thumbnail(value);return true;}
    if(!strcmp(text,"@tools:same")){
        metadata.tools_count=metadata.colors_count=2;memset(metadata.tools,0,sizeof(metadata.tools));
        for(unsigned i=0;i<2;i++){klipper_tool_metadata_t *tool=&metadata.tools[i];tool->tool_index=i;tool->referenced=true;tool->has_color=tool->has_weight=true;tool->weight_grams=21.3f;strcpy(tool->color,"#FFAA22");strcpy(metadata.colors[i],tool->color);}
        metadata.revision++;snapshot.metadata_revision=metadata.revision;return true;
    }
    if(!strcmp(text,"@tools:orca")){
        static const float weights[]={1.49f,2.06f,1.6f,0,0,0,0,.48f};
        metadata.tools_count=metadata.colors_count=8;metadata.weight_grams=5.64f;metadata.has_weight=true;
        memset(metadata.tools,0,sizeof(metadata.tools));
        for(unsigned i=0;i<8;i++){klipper_tool_metadata_t *tool=&metadata.tools[i];tool->tool_index=i;tool->has_color=tool->has_weight=true;tool->weight_grams=weights[i];snprintf(tool->color,sizeof(tool->color),"#%06X",i%2?0xeeeeee:0xffaa22);strcpy(metadata.colors[i],tool->color);}
        metadata.revision++;snapshot.metadata_revision=metadata.revision;return true;
    }
    if(sscanf(text,"@tools:%u",&value)==1){
        if(value>KLIPPER_TOOLS_MAX)return false;metadata.tools_count=value;metadata.colors_count=value;
        memset(metadata.tools,0,sizeof(metadata.tools));
        for(unsigned i=0;i<value;i++){klipper_tool_metadata_t *tool=&metadata.tools[i];tool->tool_index=i;tool->referenced=true;tool->has_color=tool->has_weight=true;tool->weight_grams=42.6f/(value?value:1);snprintf(tool->color,sizeof(tool->color),"#%06X",i%2?0xeeeeee:0xffaa22);strcpy(metadata.colors[i],tool->color);}
        metadata.revision++;snapshot.metadata_revision=metadata.revision;return true;
    }
    if(!strcmp(text,"@longname")){const char *name="aperture_science_companion_cube_extended_multicolor_very_long_filename_for_scroll_test_0123456789.gcode";strcpy(snapshot.filename,name);strcpy(snapshot.files[0],name);strcpy(metadata.filename,name);metadata.revision++;snapshot.metadata_revision=metadata.revision;snapshot.revision++;return true;}
    if(!strcmp(text,"@lastjob")){
        last_job.valid=true;last_job.revision++;last_job.endpoint_generation=network.endpoint_generation;last_job.job_id=snapshot.job_id;
        last_job.result=KLIPPER_COMPLETE;last_job.progress=100;last_job.print_duration=9780;strcpy(last_job.filename,snapshot.filename);
        snapshot.state=snapshot.raw_state=KLIPPER_COMPLETE;snapshot.progress=100;snapshot.revision++;last_metadata=metadata;return true;
    }
    if(!strcmp(text,"@clearcurrent")){snapshot.filename[0]=0;snapshot.state=snapshot.raw_state=KLIPPER_STANDBY;snapshot.revision++;return true;}
    if(!strcmp(text,"@expectlastjob")){bool pass=last_job.valid&&last_metadata.valid&&last_job.job_id==42&&*last_job.filename;printf("EXPECT_LAST_JOB preserved=%u reset_calls=%u %s\n",last_job.valid,reset_calls,pass?"PASS":"FAIL");return pass;}
    if(sscanf(text,"@expectprints:%u",&value)==1){printf("EXPECT_PRINTS %u %s\n",print_calls,print_calls==value?"PASS":"FAIL");return print_calls==value;}
    if(!strncmp(text,"@forceclick:",12)){
        lv_obj_t *object=find_label(lv_layer_top(),text+12);if(!object)object=find_label(lv_screen_active(),text+12);
        while(object&&!lv_obj_check_type(object,&lv_button_class))object=lv_obj_get_parent(object);
        if(!object)return false;events_sent++;printf("FORCE_CLICK %s\n",text+12);lv_obj_send_event(object,LV_EVENT_CLICKED,NULL);return true;
    }
    if(!strncmp(text,"@expectnotbutton:",17)){
        bool found=find_label(lv_screen_active(),text+17)||find_label(lv_layer_top(),text+17);
        printf("EXPECT_ABSENT_BUTTON %s %s\n",text+17,found?"FAIL":"PASS");return !found;
    }
    if(!strcmp(text,"@hiddenmacros")){strcpy(snapshot.macros[0],"_INTERNAL_HELPER");strcpy(snapshot.macros[1],"__PRIVATE_START");strcpy(snapshot.macros[2],"CLEAN_NOZZLE");snapshot.revision++;return true;}
    if(!strncmp(text,"@network:",9)){
        if(!strcmp(text+9,"offline")){network.has_ip=false;snapshot.available=false;snapshot.socket_connected=false;snapshot.state=snapshot.raw_state=KLIPPER_OFFLINE;}
        else if(!strcmp(text+9,"error")){snapshot.available=false;snapshot.state=snapshot.raw_state=KLIPPER_ERROR;strcpy(snapshot.error,"Mock MCU error");}
        else if(!strcmp(text+9,"reconnect")){network.has_ip=true;snapshot.available=snapshot.socket_connected=snapshot.initial_snapshot=true;snapshot.state=snapshot.raw_state=KLIPPER_STANDBY;}
        else return false;snapshot.revision++;return true;
    }
    if(!strncmp(text,"@netcomplete:",13)){
        if(!network_settings.busy)return false;bool ok=!strcmp(text+13,"ok");network_settings.busy=false;network_settings.result=ok?ESP_OK:ESP_FAIL;network_settings.revision++;
        snprintf(network_settings.message,sizeof(network_settings.message),"%s",ok?"Operacion completada":"Error de conexion; vuelve a intentar");
        if(ok&&network_settings.operation==DEVICE_NETWORK_OP_SCAN){network_settings.count=2;strcpy(network_settings.networks[0].ssid,"Aperture Lab");network_settings.networks[0].rssi=-42;network_settings.networks[0].secured=true;strcpy(network_settings.networks[1].ssid,"Guest Open");network_settings.networks[1].rssi=-67;network_settings.networks[1].secured=false;}
        if(ok&&network_settings.operation==DEVICE_NETWORK_OP_ENDPOINT_SAVE){strcpy(network.moonraker_host,pending_host);network.moonraker_port=(uint16_t)atoi(pending_port);network.endpoint_generation++;snapshot.endpoint_generation=network.endpoint_generation;}
        return true;
    }
    if(sscanf(text,"@expectnetwork:%u",&value)==1){bool open=portal_network_settings_is_open();printf("EXPECT_NETWORK open=%u %s\n",open,open==value?"PASS":"FAIL");return open==value;}
    if(!strncmp(text,"@textarea:",10)){
        char *end;index=(unsigned)strtoul(text+10,&end,10);if(*end!=':')return false;
        lv_obj_t *field=visible_widget(&lv_textarea_class,index);if(!field)return false;
        lv_textarea_set_text(field,end+1);events_sent++;lv_obj_send_event(field,LV_EVENT_VALUE_CHANGED,NULL);printf("TEXTAREA index=%u redacted=1\n",index);return true;
    }
    if(sscanf(text,"@focus:%u",&index)==1){lv_obj_t *field=visible_widget(&lv_textarea_class,index);if(!field)return false;events_sent++;lv_obj_send_event(field,LV_EVENT_FOCUSED,NULL);return true;}
    if(sscanf(text,"@expectkeyboard:%u",&value)==1){
        lv_obj_t *keyboard=visible_widget(&lv_keyboard_class,0);bool pass=keyboard!=NULL;
        if(keyboard){lv_area_t a;lv_obj_get_coords(keyboard,&a);printf("KEYBOARD (%d,%d)-(%d,%d)\n",a.x1,a.y1,a.x2,a.y2);pass=a.x1>=0&&a.x2<=800&&a.y1>=0&&a.y2<=480&&lv_obj_get_width(keyboard)==800&&lv_obj_get_height(keyboard)==226;}
        if(!value)pass=!keyboard;printf("EXPECT_KEYBOARD visible=%u %s\n",value,pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@expectnumericdots")||!strcmp(text,"@expectnumericport")){
        lv_obj_t *keyboard=visible_widget(&lv_keyboard_class,0);
        bool pass=keyboard&&lv_keyboard_get_mode(keyboard)==LV_KEYBOARD_MODE_NUMBER,has_dot=false,has_sign=false;
        if(keyboard){
            const char *const *map=lv_keyboard_get_map_array(keyboard);
            for(unsigned i=0;map&&map[i]&&*map[i];i++){
                has_dot|=!strcmp(map[i],".");
                has_sign|=!strcmp(map[i],"+/-");
            }
        }
        pass=pass&&!has_sign&&(has_dot==!strcmp(text,"@expectnumericdots"));
        printf("EXPECT_NUMERIC_DOTS %s\n",pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@keyboarddone")){lv_obj_t *keyboard=visible_widget(&lv_keyboard_class,0);if(!keyboard)return false;events_sent++;lv_obj_send_event(keyboard,LV_EVENT_READY,NULL);return true;}
    if(!strcmp(text,"@expectpasswordempty")){lv_obj_t *field=visible_widget(&lv_textarea_class,1);bool pass=field&&!*lv_textarea_get_text(field);printf("EXPECT_PASSWORD_EMPTY %s\n",pass?"PASS":"FAIL");return pass;}
    if(!strncmp(text,"@screen:",8))return portal_ui_console_command(text+8);
    if(!strncmp(text,"@expectinspection:",18)) {
        unsigned active,view,pending;
        if(sscanf(text+18,"%u:%u:%u",&active,&view,&pending)!=3)return false;
        portal_inspection_status_t status;portal_ui_inspection_status(&status);
        bool pass=status.active==active&&status.view==view&&status.pending==pending;
        printf("EXPECT_INSPECTION active=%u view=%u pending=%u %s\n",status.active,(unsigned)status.view,status.pending,pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@savevoices")) {
        memcpy(inspection_voices_before,voice_accepted,sizeof(voice_accepted));
        audio_events_get_status(&inspection_audio_before);return true;
    }
    if(!strcmp(text,"@expectvoicesunchanged")) {
        audio_events_status_t status;audio_events_get_status(&status);
        bool pass=!memcmp(inspection_voices_before,voice_accepted,sizeof(voice_accepted))&&
            !memcmp(inspection_audio_before.clip_counts,status.clip_counts,sizeof(status.clip_counts));
        printf("EXPECT_INSPECTION_VOICES_UNCHANGED %s\n",pass?"PASS":"FAIL");return pass;
    }
    if(sscanf(text,"@advance:%u",&value)==1) {lv_tick_inc(value);portal_ui_tick();return true;}
    if(sscanf(text,"@expectdropdown:%u:%u",&index,&value)==2) {
        lv_obj_t *dropdown=visible_widget(&lv_dropdown_class,index);
        bool pass=dropdown&&lv_dropdown_get_selected(dropdown)==value;
        printf("EXPECT_DROPDOWN selected=%u %s\n",value,pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@eventsstart")){audio_service_stop();memset(voice_accepted,0,sizeof(voice_accepted));printf("VOICE_TEST explicitly mocked Moonraker snapshots; real audio_events; no POST/PCM\n");return true;}
    if(sscanf(text,"@voice_ready:%u",&value)==1){snapshot.socket_connected=true;snapshot.available=true;snapshot.initial_snapshot=true;snapshot.state=(klipper_state_t)value;audio_events_update(&snapshot);return true;}
    if(!strcmp(text,"@voice_offline")){snapshot.socket_connected=false;snapshot.available=false;snapshot.initial_snapshot=false;snapshot.state=KLIPPER_OFFLINE;audio_events_update(&snapshot);return true;}
    if(!strcmp(text,"@voice_partial")){snapshot.socket_connected=true;snapshot.available=false;snapshot.initial_snapshot=false;snapshot.state=KLIPPER_PRINTING;audio_events_update(&snapshot);return true;}
    if(!strcmp(text,"@voice_error")){snapshot.socket_connected=true;snapshot.available=false;snapshot.initial_snapshot=false;snapshot.state=KLIPPER_ERROR;audio_events_update(&snapshot);return true;}
    if(sscanf(text,"@voice_endpoint:%u",&value)==1){network.endpoint_generation=snapshot.endpoint_generation=value;audio_events_update(&snapshot);return true;}
    if(sscanf(text,"@voice_enabled:%u",&value)==1){audio_service_set_enabled(value!=0);return true;}
    if(sscanf(text,"@voice_volume:%u",&value)==1){audio_service_set_volume(value);return true;}
    if(!strncmp(text,"@expectvoices:",14)){
        unsigned expected[7];if(sscanf(text+14,"%u:%u:%u:%u:%u:%u:%u",&expected[0],&expected[1],&expected[2],&expected[3],&expected[4],&expected[5],&expected[6])!=7)return false;
        audio_events_status_t status;audio_events_get_status(&status);
        bool pass=true;for(unsigned i=0;i<sizeof(expected)/sizeof(expected[0]);i++)if(voice_accepted[i]!=expected[i]||status.clip_counts[i]!=expected[i])pass=false;
        printf("EXPECT_VOICES %u:%u:%u:%u:%u:%u:%u %s\n",voice_accepted[0],voice_accepted[1],voice_accepted[2],voice_accepted[3],voice_accepted[4],voice_accepted[5],voice_accepted[6],pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@voice_status"))return audio_events_console_command("audio events status");
    if(sscanf(text,"@commandresult:%u",&value)==1){
        if(!feedback_result.id||value>KLIPPER_UNKNOWN)return false;
        feedback_result.status=(klipper_command_status_t)value;feedback_result_ready=true;return true;
    }
    char clip_name[32];
    if(sscanf(text,"@expectclip:%31[^:]:%u",clip_name,&value)==2){
        audio_clip_t clip;
        if(!audio_clip_parse(clip_name,&clip))return false;
        bool pass=voice_accepted[clip]==value;
        printf("EXPECT_CLIP %s count=%u %s\n",clip_name,voice_accepted[clip],pass?"PASS":"FAIL");
        return pass;
    }
    if(sscanf(text,"@audioready:%u",&value)==1){audio_mock.ready=value!=0;return true;}
    if(!strcmp(text,"@audiotest_direct")){audio_service_test_tone();return true;}
    if(sscanf(text,"@show:%u",&value)==1){portal_ui_test_show((portal_view_t)value);return true;}
    if(!strncmp(text,"@expectaudio:",13)) {
        unsigned enabled,volume,queued;if(sscanf(text+13,"%u:%u:%u",&enabled,&volume,&queued)!=3)return false;
        bool pass=audio_mock.enabled==enabled&&audio_mock.volume==volume&&audio_mock.queued==queued;
        printf("EXPECT_AUDIO enabled=%u volume=%u queued=%u %s\n",audio_mock.enabled,audio_mock.volume,audio_mock.queued,pass?"PASS":"FAIL");return pass;
    }
    if(!strncmp(text,"@expectbuttondisabled:",22)) {
        lv_obj_t *object=find_label(lv_screen_active(),text+22);
        while(object&&!lv_obj_check_type(object,&lv_button_class))object=lv_obj_get_parent(object);
        bool pass=object&&lv_obj_has_state(object,LV_STATE_DISABLED);printf("EXPECT_BUTTON_DISABLED %s %s\n",text+22,pass?"PASS":"FAIL");return pass;
    }
    if(!strcmp(text,"@resultunknown")) { result_unknown_pending=true;printf("RESULT unknown queued\n");return true; }
    if(!strcmp(text,"@booterror")) { snapshot.available=false;snapshot.initial_snapshot=false;snapshot.state=KLIPPER_ERROR;strcpy(snapshot.error,"MCU unavailable: cannot connect to printer MCU");snapshot.error_revision++;return true; }
    if(!strcmp(text,"@recover")) { snapshot.available=false;snapshot.state=KLIPPER_STARTUP;snapshot.error[0]=0;snapshot.error_revision++;return true; }
    if(!strncmp(text,"@fullerror:",11)){
        snprintf(mock_error_detail,sizeof(mock_error_detail),"%s",text+11);
        snapshot.error_revision++;
        return true;
    }
    if(!strcmp(text,"@tick")) { lv_tick_inc(250);portal_ui_tick();return true; }
    if(sscanf(text,"@homed:%u",&value)==1){
        snapshot.homed_x=(value&1)!=0;snapshot.homed_y=(value&2)!=0;snapshot.homed_z=(value&4)!=0;return true;
    }
    if(!strncmp(text,"@expecttop:",11))return any_label(lv_layer_top(),text+11);
    if(!strncmp(text,"@expectnottop:",14))return !any_label(lv_layer_top(),text+14);
    if(!strncmp(text,"@expect:",8)) { bool found=any_label(lv_screen_active(),text+8)||any_label(lv_layer_top(),text+8);printf("EXPECT_LABEL %s %s\n",text+8,found?"PASS":"FAIL");return found; }
    if(!strncmp(text,"@expectnot:",11)) { bool found=any_label(lv_screen_active(),text+11)||any_label(lv_layer_top(),text+11);printf("EXPECT_ABSENT %s %s\n",text+11,!found?"PASS":"FAIL");return !found; }
    if(!strncmp(text,"@capture:",9)) { char path[2048];snprintf(path,sizeof(path),"%s/%s.rgb565",output_dir,text+9);lv_refr_now(lv_display_get_default());FILE *f=fopen(path,"wb");if(!f)return false;fwrite(framebuffer,sizeof(framebuffer),1,f);fclose(f);return true; }
    if(sscanf(text,"@generation:%u",&value)==1) { network.endpoint_generation=snapshot.endpoint_generation=value;printf("MOCK generation %u\n",value);return true; }
    if(sscanf(text,"@networkgeneration:%u",&value)==1) { network.endpoint_generation=value;return true; }
    if(sscanf(text,"@state:%u",&value)==1) { snapshot.raw_state=snapshot.state=(klipper_state_t)value;printf("MOCK state %u\n",value);return true; }
    if(strncmp(text,"@filename:",10)==0) { snprintf(snapshot.filename,sizeof(snapshot.filename),"%s",text+10);printf("MOCK filename %s\n",snapshot.filename);return true; }
    if(sscanf(text,"@dropdown:%u:%u",&index,&value)==2) {
        lv_obj_t *dropdown=visible_widget(&lv_dropdown_class,index); if(!dropdown) return false;
        lv_dropdown_set_selected(dropdown,value); events_sent++; printf("SELECT dropdown %u\n",value); lv_obj_send_event(dropdown,LV_EVENT_VALUE_CHANGED,NULL); return true;
    }
    if(sscanf(text,"@switch:%u:%u",&index,&value)==2) {
        lv_obj_t *toggle=find_type(lv_screen_active(),&lv_switch_class,&index); if(!toggle) return false;
        if(value)lv_obj_add_state(toggle,LV_STATE_CHECKED);else lv_obj_remove_state(toggle,LV_STATE_CHECKED);
        events_sent++;printf("SWITCH checked=%u\n",value);lv_obj_send_event(toggle,LV_EVENT_VALUE_CHANGED,NULL);return true;
    }
    bool slider_action=sscanf(text,"@slider:%u:%u",&index,&value)==2;
    if(!slider_action&&sscanf(text,"@slider:%u",&value)==1) { index=0;slider_action=true; }
    if(slider_action) {
        lv_obj_t *slider=find_type(lv_screen_active(),&lv_slider_class,&index); if(!slider) return false;
        events_sent+=3; lv_obj_send_event(slider,LV_EVENT_PRESSED,NULL);lv_slider_set_value(slider,value,LV_ANIM_OFF);lv_obj_send_event(slider,LV_EVENT_VALUE_CHANGED,NULL);lv_obj_send_event(slider,LV_EVENT_RELEASED,NULL);return true;
    }
    return click_text(text);
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0); setvbuf(stderr,NULL,_IONBF,0);
    if(argc<2) return 2;
    output_dir=argv[1];
    printf("HOST lv_init\n"); lv_init(); lv_display_t *display=lv_display_create(800,480);
    static uint16_t draw_buffer[800*480] __attribute__((aligned(16)));
    host_draw_buffer=draw_buffer;
    lv_display_set_color_format(display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display,draw_buffer,NULL,sizeof(draw_buffer),LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display,flush);
    lv_indev_t *input=lv_indev_create();host_input=input; lv_indev_set_type(input,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(input,idle_read); lv_indev_set_display(input,display);
    mock_view(0); printf("HOST portal_ui_init\n"); portal_ui_init(display,input);
    if(!(argc>3&&!strcmp(argv[3],"@eventsstart")))portal_ui_tick();printf("HOST init_done\n");
    for(unsigned view=0;view<PORTAL_VIEW_COUNT;view++) {
        mock_view(view); portal_ui_test_show((portal_view_t)view); refresh(display); inspect(lv_screen_active(),view);
        char path[2048]; snprintf(path,sizeof(path),"%s/%02u_%s.rgb565",argv[1],view,views[view]);
        FILE *file=fopen(path,"wb"); if(!file) return 3; fwrite(framebuffer,sizeof(framebuffer),1,file); fclose(file);
        printf("FRAME %u %s\n",view,views[view]);
    }
    /* Optional single action sequence: VIEW label label ... (real LVGL events). */
    if(argc>3) { unsigned view=(unsigned)atoi(argv[2]); mock_view(view);
        if(strcmp(argv[3],"@eventsstart")){lv_tick_inc(250);portal_ui_tick();}
        portal_ui_test_show((portal_view_t)view); refresh(display);
        if(strcmp(argv[3],"@eventsstart")){audio_events_update(&snapshot);audio_service_stop();memset(voice_accepted,0,sizeof(voice_accepted));printf("UI_AUDIO_ISOLATION baseline established; mock queue cleared\n");}
        printf("ACTION_BEGIN\n"); for(int i=3;i<argc;i++) {
            if(!action(argv[i])) return 4;refresh(display);
            /* Raw mock deltas intentionally precede portal_ui_tick. Check
             * geometry immediately; color/state assertions use synced UI. */
            inspect_colors=!strcmp(argv[i],"@tick")||!strncmp(argv[i],"@show:",6);
            portal_inspection_status_t current;portal_ui_inspection_status(&current);
            inspect(lv_screen_active(),current.view);inspect(lv_layer_top(),current.view);
        } }
    printf("SUMMARY frames=%u clicks=%u backend_calls=%u screen_clipped_labels=%u\n",PORTAL_VIEW_COUNT,events_sent,backend_calls,clipped_labels);
    return clipped_labels?5:0;
}

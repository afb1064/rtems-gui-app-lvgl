/*
 * Copyright (c) 2020 Vijay Kumar Banerjee <vijay@rtems.org>  All rights reserved. 
 * Copyright (c) 2020 Ali Furkan Bodur <furkanbodur52@gmail.com>  All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <assert.h>
#include <stdlib.h>
#include <sysexits.h>
#include <rtems.h>
#include <rtems/bsd/bsd.h>
#include <bsp/i2c.h>
#include <rtems/console.h>
#include <rtems/shell.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define PRIO_SHELL		150
#define STACK_SIZE_SHELL	(64 * 1024)
#define PRIO_MOUSE          (RTEMS_MAXIMUM_PRIORITY - 10)

#include <fbdev.h>
#include "lvgl/lvgl.h"
#include <lv_drivers/indev/evdev.h>
#include "mouse_cursor_icon.c"

typedef struct evdev_message {
    int fd;
    char device[256];
} evdev_message;

static rtems_id eid, emid;
static volatile bool kill_evtask, evtask_active;

void libbsdhelper_start_shell(rtems_task_priority prio){

	rtems_status_code sc = rtems_shell_init(
		"SHLL",
		STACK_SIZE_SHELL,
		prio,
		CONSOLE_DEVICE_NAME,
		false,
		true,
		NULL
	);
	assert(sc == RTEMS_SUCCESSFUL);

}

static void* tick_thread (void *);
static void evdev_input_task(rtems_task_argument);
static void* mem_monitor(lv_task_t *);
static void hal_init(void);
static void draw(lv_theme_t *);
static void topBar(void);
static void slider_event(lv_obj_t *, lv_event_t);

static void botBar(void);
static void loader(lv_obj_t *);
static void sideBar(void);
static void page(void);

static lv_obj_t * header;
static lv_obj_t * bottomBar;
static lv_obj_t * slider_label;
static lv_obj_t * sb;
static lv_obj_t * content;
static lv_obj_t * slider;


static void Init(rtems_task_argument arg)
{

	rtems_status_code sc;
	int exit_code;
	(void)arg;

	puts("\n**RTEMS LVGL**\n");

	sc = rtems_task_create(
    rtems_build_name('E', 'V', 'D', 'M'),
    PRIO_MOUSE,
    RTEMS_MINIMUM_STACK_SIZE,
    RTEMS_DEFAULT_MODES,
    RTEMS_FLOATING_POINT,
    &eid
    );
    assert(sc == RTEMS_SUCCESSFUL);

    sc = rtems_task_start(eid, evdev_input_task, 0);
    assert(sc == RTEMS_SUCCESSFUL);
    sc = rtems_bsd_initialize();
    assert(sc == RTEMS_SUCCESSFUL);

    lv_init();
    hal_init();
    draw(lv_theme_material_init(0 , NULL));
	
	lv_tick_inc(5);
	lv_task_handler();
    usleep(5000);
	
	libbsdhelper_start_shell(PRIO_SHELL);
    
    kill_evtask = true;
    while (evtask_active) {
        rtems_task_wake_after(RTEMS_MILLISECONDS_TO_TICKS(10));
    }

	exit(0);
}

static void* tick_thread (void *arg)
{

    (void)arg;

    while(1) {
        lv_tick_inc(1);
        lv_task_handler();
        usleep(1000);
    }
}

static void evdev_input_task(rtems_task_argument arg)
{

    rtems_status_code sc;
    size_t size;
    int fd = -1;
    pthread_t thread_id;
    
    evdev_message msg;

    kill_evtask = false;
    evtask_active = true;

    while(!kill_evtask){
        if ( access(EVDEV_NAME, F_OK) != -1){
            evdev_init();
            break;
        }
    }

    pthread_create(&thread_id, NULL, tick_thread, NULL);
    evtask_active = false;
    rtems_task_delete(RTEMS_SELF);
}

static void* mem_monitor(lv_task_t * param)
{

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    printf("used: %6d (%3d %%), frag: %3d %%, biggest free: %6d\n", (int)mon.total_size - mon.free_size,
            mon.used_pct,
            mon.frag_pct,
            (int)mon.free_biggest_size);
}

static void hal_init(void)
{

    fbdev_init();
    
    static lv_color_t buf[LV_HOR_RES_MAX*10];
    static lv_disp_buf_t disp_buf;
    lv_disp_buf_init(&disp_buf, buf, NULL, LV_HOR_RES_MAX*10);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.buffer = &disp_buf;
    disp_drv.flush_cb = fbdev_flush;
    lv_disp_drv_register(&disp_drv);
	
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = evdev_read;
    lv_indev_drv_register(&indev_drv);
    lv_indev_t * mouse_indev = lv_indev_drv_register(&indev_drv);

    LV_IMG_DECLARE(mouse_cursor_icon); /*Declare the image file.*/
    lv_obj_t * cursor_obj = lv_img_create(lv_scr_act(), NULL); /*Create an image object for the cursor */
    lv_img_set_src(cursor_obj, &mouse_cursor_icon);           /*Set the image source*/
    lv_indev_set_cursor(mouse_indev, cursor_obj); 
	
    lv_task_create(mem_monitor, 3000, LV_TASK_PRIO_MID, NULL);

}

static void topBar(void)
{

    header = lv_cont_create(lv_disp_get_scr_act(NULL), NULL);
    lv_obj_set_width(header, lv_disp_get_hor_res(NULL));
    static lv_style_t style_bg; 
    lv_style_copy(&style_bg, &lv_style_pretty);
    style_bg.body.main_color =  LV_COLOR_GRAY;
    style_bg.body.grad_color =  LV_COLOR_WHITE;
    style_bg.body.border.color = LV_COLOR_BLACK;

    lv_cont_set_style(header,LV_CONT_STYLE_MAIN, &style_bg);
    lv_obj_t * sym = lv_label_create(header, NULL);
    lv_label_set_text(sym, LV_SYMBOL_GPS LV_SYMBOL_WIFI LV_SYMBOL_BLUETOOTH LV_SYMBOL_POWER);
    lv_obj_align(sym, NULL, LV_ALIGN_IN_RIGHT_MID, -LV_DPI/10, 0);

    lv_obj_t * clock = lv_label_create(header, NULL);

    lv_label_set_text(clock, "01:02");
    lv_obj_align(clock, NULL, LV_ALIGN_CENTER, LV_DPI/10, 0);

    lv_cont_set_fit2(header, LV_FIT_NONE, LV_FIT_TIGHT);   
    lv_obj_set_pos(header, 0, 0);

}
static void loader(lv_obj_t *scr)
{

    static lv_style_t style;
    lv_style_copy(&style, &lv_style_plain);
    style.line.width = 10;                         
    style.line.color = lv_color_hex3(0x000);
    style.body.border.color = lv_color_hex3(0xBBB); 
    style.body.border.width = 10;
    style.body.padding.left = 0;

    lv_obj_t * preload = lv_preload_create(scr, NULL);
    lv_obj_set_size(preload, 50, 50);
    lv_obj_align(preload, NULL, LV_ALIGN_OUT_BOTTOM_MID, LV_DPI/2, LV_DPI);
    lv_preload_set_style(preload, LV_PRELOAD_STYLE_MAIN, &style);

}

static void sideBar(void)
{

    lv_coord_t hres = lv_disp_get_hor_res(NULL);
    lv_coord_t vres = lv_disp_get_ver_res(NULL);

    sb = lv_page_create(lv_disp_get_scr_act(NULL), NULL);
    lv_page_set_scrl_layout(sb, LV_LAYOUT_COL_M);
    lv_page_set_style(sb, LV_PAGE_STYLE_BG, &lv_style_transp_tight);
    lv_page_set_style(sb, LV_PAGE_STYLE_SCRL, &lv_style_transp);

    lv_obj_t  * calendar = lv_calendar_create(sb, NULL);
    lv_obj_set_size(calendar, 230, 230);
    lv_obj_align(calendar, NULL, LV_ALIGN_CENTER, 0, LV_DPI*2);
    
    lv_calendar_date_t today;
    today.year = 2020;
    today.month = 7;
    today.day = 25;

    lv_calendar_set_today_date(calendar, &today);
    lv_calendar_set_showed_date(calendar, &today);

    loader(sb);    

    if(hres > vres) {
        lv_obj_set_height(sb, vres - lv_obj_get_height(header));
        lv_cont_set_fit2(sb, LV_FIT_TIGHT, LV_FIT_NONE);
        lv_obj_align(sb, header, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
        lv_page_set_sb_mode(sb, LV_SB_MODE_DRAG);
    } else {
        lv_obj_set_size(sb, hres, vres / 2 - lv_obj_get_height(header));
        lv_obj_align(sb, header, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 0);
        lv_page_set_sb_mode(sb, LV_SB_MODE_AUTO);
    }

}

static void botBar(void)
{

    bottomBar = lv_cont_create(lv_disp_get_scr_act(NULL), NULL);
    lv_obj_set_width(bottomBar, lv_disp_get_hor_res(NULL));

    lv_obj_t * sym = lv_label_create(bottomBar, NULL);
    lv_label_set_text(sym, LV_SYMBOL_PREV "  " LV_SYMBOL_PLAY "  " LV_SYMBOL_NEXT);
    lv_obj_align(sym, NULL, LV_ALIGN_IN_TOP_MID, -LV_DPI/10, 0);

    static lv_style_t style_bg;
    static lv_style_t style_indic;
    static lv_style_t style_knob;

    lv_style_copy(&style_bg, &lv_style_pretty);
    style_bg.body.main_color =  LV_COLOR_GRAY;
    style_bg.body.grad_color =  LV_COLOR_WHITE;
    style_bg.body.radius = LV_RADIUS_CIRCLE;
    style_bg.body.border.color = LV_COLOR_BLACK;

    lv_style_copy(&style_indic, &lv_style_pretty_color);
    style_indic.body.radius = LV_RADIUS_CIRCLE;
    style_indic.body.shadow.width = 8;
    style_indic.body.shadow.color = LV_COLOR_RED;
    style_indic.body.padding.left = 3;
    style_indic.body.padding.right = 3;
    style_indic.body.padding.top = 3;
    style_indic.body.padding.bottom = 3;

    lv_style_copy(&style_knob, &lv_style_pretty);
    style_knob.body.radius = LV_RADIUS_CIRCLE;
    style_knob.body.opa = LV_OPA_80;
    style_knob.body.padding.top = 10 ;
    style_knob.body.padding.bottom = 10 ;

    lv_cont_set_style(bottomBar,LV_CONT_STYLE_MAIN, &style_bg);
    slider = lv_slider_create(bottomBar, NULL);
    lv_obj_set_width(slider, LV_DPI*1.5);
    lv_obj_set_height(slider, LV_DPI/3.5);
    lv_slider_set_style(slider, LV_SLIDER_STYLE_BG, &style_bg);
    lv_slider_set_style(slider, LV_SLIDER_STYLE_INDIC,&style_indic);
    lv_slider_set_style(slider, LV_SLIDER_STYLE_KNOB, &style_knob);
    lv_obj_align(slider, bottomBar, LV_ALIGN_IN_TOP_RIGHT, -LV_DPI/10, 0);
    lv_obj_set_event_cb(slider, slider_event);
    lv_slider_set_range(slider, 0, 100);

    lv_obj_t * sName = lv_label_create(bottomBar, NULL);
    lv_label_set_text(sName, "Steve Ballmer - Developers (Remix)");
    lv_obj_align(sName, bottomBar, LV_ALIGN_OUT_BOTTOM_MID, 5, -50);

    slider_label = lv_label_create(bottomBar, NULL);
    lv_label_set_text(slider_label, LV_SYMBOL_MUTE);
    lv_obj_set_auto_realign(slider_label, true);
    lv_obj_align(slider_label, slider, LV_ALIGN_IN_TOP_MID, 1,5);

    lv_cont_set_fit2(bottomBar, LV_FIT_NONE, LV_FIT_TIGHT);   
    lv_obj_set_pos(bottomBar, 0,430);
}

static void slider_event(lv_obj_t * obj, lv_event_t event)
{

    if(event == LV_EVENT_VALUE_CHANGED) {
        static char buf[4]; 
        snprintf(buf, 4, "%u", lv_slider_get_value(slider));
        if(lv_slider_get_value(slider) != 0){
            lv_label_set_text(slider_label, buf);
        }
        else{
            lv_label_set_text(slider_label, LV_SYMBOL_MUTE);
        }
    }
}

static void page(void)
{

    lv_coord_t hres = lv_disp_get_hor_res(NULL);
    lv_coord_t vres = lv_disp_get_ver_res(NULL);

    content = lv_page_create(lv_disp_get_scr_act(NULL), NULL);

    if(hres > vres) {
        lv_obj_set_size(content, hres - lv_obj_get_width(sb), vres - lv_obj_get_height(header) - lv_obj_get_height(bottomBar) - 5);
        lv_obj_set_pos(content,  lv_obj_get_width(sb), lv_obj_get_height(header));
    } else {
        lv_obj_set_size(content, hres , vres / 2 - 50);
        lv_obj_set_pos(content,  0, vres / 2);
    }

    lv_page_set_scrl_layout(content, LV_LAYOUT_PRETTY);
    lv_page_set_scrl_fit2(content, LV_FIT_FLOOD, LV_FIT_TIGHT);

    lv_coord_t max_w = lv_page_get_fit_width(content);

    lv_obj_t * btn = lv_btn_create(content, NULL);
    lv_btn_set_ink_in_time(btn, 200);
    lv_btn_set_ink_wait_time(btn, 100);
    lv_btn_set_ink_out_time(btn, 500);
    lv_obj_t * label = lv_label_create(btn, NULL);
    lv_label_set_text(label, "Buton");

    lv_obj_t * sw = lv_sw_create(content, NULL);
    lv_sw_set_anim_time(sw, 250);

    lv_cb_create(content, NULL);

    lv_obj_t * ta = lv_ta_create(content, NULL);
    lv_obj_set_size(ta, lv_obj_get_width(content) + 5 , lv_obj_get_height(content) / 3 - 5);
    lv_obj_align(ta, NULL, LV_ALIGN_IN_TOP_MID, 0, lv_obj_get_width(content));
    lv_ta_set_cursor_type(ta, LV_CURSOR_BLOCK);

    lv_obj_t * kb = lv_kb_create(content, NULL);
    lv_obj_set_size(kb, lv_obj_get_width(content) + 5 , lv_obj_get_height(content) / 3 - 5);
    lv_obj_align(kb, ta, LV_ALIGN_OUT_BOTTOM_MID, 0, LV_DPI);
    lv_kb_set_ta(kb, ta);
}

static void draw(lv_theme_t * th)
{

    lv_theme_set_current(th);
    lv_obj_t * scr = lv_obj_create(NULL, NULL);
    lv_disp_load_scr(scr);

    topBar();
    sideBar();
    botBar();
    page();               
}


/*
 * Configure LibBSD.
 */
#define RTEMS_BSD_CONFIG_BSP_CONFIG
#define RTEMS_BSD_CONFIG_TERMIOS_KQUEUE_AND_POLL
#define RTEMS_BSD_CONFIG_INIT

#include <machine/rtems-bsd-config.h>

/*
 * Configure RTEMS.
 */
#define CONFIGURE_MICROSECONDS_PER_TICK 1000

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_STUB_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_ZERO_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_LIBBLOCK

#define CONFIGURE_FILESYSTEM_DOSFS
#define CONFIGURE_LIBIO_MAXIMUM_FILE_DESCRIPTORS 32

#define CONFIGURE_UNLIMITED_OBJECTS
#define CONFIGURE_UNIFIED_WORK_AREAS
#define CONFIGURE_MAXIMUM_USER_EXTENSIONS 1

#define CONFIGURE_INIT_TASK_STACK_SIZE (64*1024)
#define CONFIGURE_INIT_TASK_INITIAL_MODES RTEMS_DEFAULT_MODES
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT

#define CONFIGURE_BDBUF_BUFFER_MAX_SIZE (32 * 1024)
#define CONFIGURE_BDBUF_MAX_READ_AHEAD_BLOCKS 4
#define CONFIGURE_BDBUF_CACHE_MEMORY_SIZE (1 * 1024 * 1024)
#define CONFIGURE_BDBUF_READ_AHEAD_TASK_PRIORITY 97
#define CONFIGURE_SWAPOUT_TASK_PRIORITY 97

//#define CONFIGURE_STACK_CHECKER_ENABLED

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_INIT

#include <rtems/confdefs.h>
#include <bsp/nexus-devices.h>

SYSINIT_DRIVER_REFERENCE(ukbd, uhub);
SYSINIT_DRIVER_REFERENCE(ums, uhub);

/*
 * Configure Shell.
 */
#include <rtems/netcmds-config.h>
#include <bsp/irq-info.h>
#define CONFIGURE_SHELL_COMMANDS_INIT

#define CONFIGURE_SHELL_USER_COMMANDS \
  &bsp_interrupt_shell_command, \
  &rtems_shell_ARP_Command, \
  &rtems_shell_I2C_Command

#define CONFIGURE_SHELL_COMMANDS_ALL

#include <rtems/shellconfig.h>

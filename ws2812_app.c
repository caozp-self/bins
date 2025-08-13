/****************************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdbool.h>
#include <poll.h>
#include <sched.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <mqueue.h>
#include <semaphore.h>
#include <syslog.h>
#include <media_api.h>
#include <math.h>

#include <nuttx/leds/ws2812.h>
#include "ws2812_app.h"
#include "audio_manager_c.h"
#include "system.h"
#include "rhy_record_client.h"
#include "led_event.h"
#include <kvdb.h>
#include "player_common.h"

#define       NM_POWER                        (0.4)
#define       RHYTHM_QUEUE_NAME               "rhythmQueue"
#define       PV_RHYTHM_QUEUE_NAME             "pVrhythmQueue"
#define       RHYTHM_MSG_LEN                  (sizeof(RHYTHM_MSG))
#define       GRAD_CHANGE_QUEUE_NAME          "gradChangeQueue"
#define       GRAD_CHANGE_MSG_LEN              (sizeof(GRAD_CHANGE_MSG))
#define       EXEC_MSG_LEN                    (sizeof(EXEC_MSG))
#define       EXEC_MSG_QUEUE                  "exec_msgQueue"
#define       RHYTHM_RECV_ENERGY_QUEUE        "recv_energyQueue"
#define       RHYTHM_RECV_ENERGY_MSG_LEN      (sizeof(RECV_ENERGY_MSG))
#define       TYPE_DISTRIBUTE                 (0)
#define       TYPE_SHUT                       (1)
#define       REBOOT_PARAM_OH2_MASK           (0xA55A)
#define       REBOOT_PARAM_CRASH              (1 << 18)
#define       REBOOT_PARAM_OTA                (1 << 17)
#define       REBOOT_PARAM_OH2_SLIENT         (1 << 16)

//渐变过程调整参数
#define MUSIC_PLAY_INIT_TIME            (7)
#define MUSIC_PLAY_STEP_NUM             (3.0)  //0-->3  4*8 = 32 MS

#define TTS_PLAY_INIT_TIME              (18)
#define TTS_PLAY_STEP_NUM               (9.0)  //0-->10  10*8 = 80 MS

#define VOICE_PICK_INIT_TIME            (15)
#define VOICE_PICK_STEP_NUM             (6.0)  //0-->6  7*8 = 56 MS

// #define       RAW_DATA_32BIT               (1)
static  bool  _G_nm = false;
static  int   _G_volume;
static  int  _G_btVolume;
static  int  btPlay;
static  int  pv_exist;
extern  RRS_CLIENT_INFO  _G_client_info;
extern  void reboot_param_oh2_clear(void);
extern  uint32_t reboot_param_oh2_get(void);
extern player_session_t* bt_player_get(void);
extern player_session_t* music_player_get(void);
extern int check_is_background(void);
bool  is_in_stack(Stack* sta, int data);

typedef struct {
    pid_t tid;
    int con_met;
    pthread_mutex_t cmd_mutex;
    pthread_cond_t exec_cond;
} exec_ctx_t;
static exec_ctx_t  exec_ctx;
static pthread_mutex_t stack_mutex;

RGB_MODE_INFO  _G_info = {
    .Normal_table =
     {
        USER_RGB_MODE_WHILT_BREATH,
        USER_RGB_MODE_DYN_01,
        USER_RGB_MODE_DYN_02,
        USER_RGB_MODE_WALK_LE_AND_RI,
        USER_RGB_MODE_SIDE_TO_CENTER,
        USER_RGB_MODE_BOOT_SUCCESS,
        USER_RGB_MODE_INIT_CONFIG,
        USER_RGB_MODE_OTA_UPGRADE,
        USER_RGB_MODE_VOIP_DIAL,
        USER_RGB_MODE_NET_OFF,
        USER_RGB_MODE_COMB_PLAY,
        USER_RGB_MODE_PLAING_LOAD_SOURCE,
        USER_RGB_MODE_VOIP_BUSY,
        USER_RGB_MODE_SYSTEM_FAULT,
        USER_RGB_MODE_ALARM_ACTIVE,
        USER_RGB_MODE_MIC_MUTE,
        USER_RGB_MODE_OTA_UPGRADE_1,
        USER_RGB_MODE_DYN_TTS,
        USER_RGB_MODE_OPEN_BLE_DISCOVER,
        USER_RGB_MODE_DYN_03,
        USER_RGB_MODE_MIAOBO_DYN,
        USER_RGB_SET_PRE_CONFIG_MODE,
        USER_RGB_MODE_WALK_LE_AND_RI_NO_REST,
        USER_RGB_MODE_MIPLAY,

        USER_RGB_MODE_OFF,
     },
};
#if 0
int  send_energy2rhythm(RECV_ENERGY_MSG  msg)
{
    mqd_t mqfd;

    mqfd = mq_open(RHYTHM_RECV_ENERGY_QUEUE, O_WRONLY | O_NONBLOCK);
    if (mqfd == (mqd_t)-1)
    {
        return (-1);
    }

    mq_send(mqfd, (const char*)&msg, RHYTHM_RECV_ENERGY_MSG_LEN, 42);

    mq_close(mqfd);

    return 0;
}
#endif
int  send_energy2rhythm(RECV_ENERGY_MSG  msg)
{
    int ret = -1;

    ret = pthread_mutex_trylock(&_G_info.cur_percent_mutex);
    if(ret == 0)
    {
        _G_info.cur_percent = msg.energy;
        pthread_mutex_unlock(&_G_info.cur_percent_mutex);
    }

    return 0;
}
unsigned long long  time_get_ms(void);
void rgb_scan_time_update(RGB_MODE_INFO *info);
int hsv2rgb(int hh, int ss, int vv){

    int rr, gg, bb;
    if((!(hh) && !(ss) && !(vv))){
        rr =0;
        gg =0;
        bb =0;
        return 0;
    }

    float h = (ofb_h(hh)*MAX_H_F)/MAX_H*1.0f;
    float s = ofb_s(ss)  / MAX_S_F;
    float v = ofb_v(vv)  / MAX_S_F;
    float r = 0;
    float g = 0;
    float b = 0; // 0.0-1.0
    int   hi = (int)(h / (MAX_H_F/6)) % 6;
    float f  = (h / (MAX_H_F/6)) - hi;
    float p  = v * (1.0f - s);
    float q  = v * (1.0f - s * f);
    float t  = v * (1.0f - s * (1.0f - f));

    switch(hi) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        case 5: r = v, g = p, b = q; break;
    }

    rr = (unsigned char)(r * MAX_RGB_F); // dst_r : 0-255
    gg = (unsigned char)(g * MAX_RGB_F); // dst_r : 0-255
    bb = (unsigned char)(b * MAX_RGB_F); // dst_r : 0-255

    return  (  ((rr << 16) & 0xff0000)
             | ((gg << 8)  & 0x00ff00)
             | ((bb << 0)  & 0x0000ff)  );
}

typedef int (*Function)(RGB_MODE_INFO *);

int  fill_info_buff(RGB_MODE_INFO * info)
{
    int        rgb;
    uint32_t  *each_led_rgb;
    int        i;

    if(_G_nm && info->v != 0 &&
       info->Normal_mode != USER_RGB_MODE_WHILT_BREATH  && info->Normal_mode != USER_RGB_MODE_BOOT_SUCCESS)
    {
        info->v = info->v * NM_POWER;
    }

    rgb = hsv2rgb(info->h, info->s, info->v);  //转化为rgb值
    each_led_rgb = info->rgbBuff;

    memset(info->rgbBuff, 0, 4 * LED_NUM);
    for(i = 0; i < LED_NUM; i++)
    {
        *each_led_rgb = rgb;
        each_led_rgb++;
    }

    return 0;
}

int  one_by_one_fill_info_buff(RGB_MODE_INFO * info, int display_num)
{
    int        rgb;
    uint32_t  *each_led_rgb = info->rgbBuff;
    int        i;

    memset(info->rgbBuff, 0, sizeof(info->rgbBuff));
    for(i = 0; i < display_num; i++)
    {
        if(_G_nm && info->Normal_mode != USER_RGB_MODE_WHILT_BREATH  && info->Normal_mode != USER_RGB_MODE_BOOT_SUCCESS)
        {
            int tmp = info->hsv[i].v;
            info->hsv[i].v = info->hsv[i].v * NM_POWER;
            if(info->hsv[i].v == 0  && tmp != 0) {
                info->hsv[i].v = 1;
            }
        }
        rgb = hsv2rgb(info->hsv[i].h, info->hsv[i].s, info->hsv[i].v);  //转化为rgb值
        *each_led_rgb = rgb; //填充每个rgb
        each_led_rgb++;
    }

    return 0;
}

int send_rgbbuf_to_spi(RGB_MODE_INFO * info)
{
    int fd;
    fd = open("/dev/leds0", O_WRONLY);
    write(fd, info->rgbBuff, sizeof(info->rgbBuff));
    close(fd);
    return  0;
}
/****************************************************************************
 * 灯光函数
 ****************************************************************************/
int green_stay_init(RGB_MODE_INFO * info)
{
    info->time_m = 25;

    unsigned long long cur_time_ms = time_get_ms();
    info->Special_mode_time = 2000 + cur_time_ms; // 延长闪烁时间

    return 0;
}
/****************************************************************************
 * 绿色常亮
 ****************************************************************************/
int green_stay_run(RGB_MODE_INFO * info)
{
    pthread_mutex_lock(&info->rgb_buf_mutex);
    int  left_index = 3;
    int  right_index = 4;
    int i = 0;
    int display_num = 1;
    int display_v = 0;

    int volume = _G_volume;

    if(volume < 25)
    {
        display_num = 1;
        switch(volume)
        {
            case 4:   display_v = 80; break;
            case 8:   display_v = 90; break;
            case 12:  display_v = 105; break;
            case 16:  display_v = 160; break;
            case 20:  display_v = 210; break;
            case 24:  display_v = 255; break;
            default:  display_v = 80; break;
        }
    } else if(volume < 50)
    {
        display_num = 2;
        switch(volume)
        {
            case 28:   display_v = 30; break;
            case 32:   display_v = 55; break;
            case 36:   display_v = 105; break;
            case 40:   display_v = 160; break;
            case 44:   display_v = 210; break;
            case 48:   display_v = 255; break;
            default:   display_v = 80; break;
        }
    }  else if(volume < 75)
    {
        display_num = 3;
        switch(volume)
        {
            case 52:   display_v = 20; break;
            case 56:   display_v = 55; break;
            case 60:   display_v = 105; break;
            case 64:   display_v = 160; break;
            case 68:   display_v = 210; break;
            case 72:   display_v = 255; break;
            default:   display_v = 80; break;
        }
    }  else if(volume <= 100)
    {
        display_num = 4;
        switch(volume)
        {
            case 76:   display_v = 20; break;
            case 80:   display_v = 60; break;
            case 84:   display_v = 100; break;
            case 88:   display_v = 140; break;
            case 92:   display_v = 180; break;
            case 96:   display_v = 220; break;
            case 100:  display_v = 255; break;
            default:   display_v = 80; break;
        }
    }

    //填充 hsv buff
    memset(_G_info.hsv, 0, sizeof(_G_info.hsv));
    for(i = 0; i < display_num; i++)
    {
        _G_info.hsv[right_index + i].h = 350;
        _G_info.hsv[right_index + i].s = 70;
        _G_info.hsv[left_index - i].h = 350;
        _G_info.hsv[left_index - i].s = 70;
        //填充每个灯的hsv
        if(i == display_num - 1) {    //最顶上的灯亮度变化
            //left
            _G_info.hsv[left_index - i].v = display_v;
            //right
            _G_info.hsv[right_index + i].v = display_v;
        } else if (i == display_num - 2)// 2.3.4  倒2
        {
            switch (display_num)
            {
                case 2:
                    //left
                    _G_info.hsv[left_index - i].v = 250;
                    //right
                    _G_info.hsv[right_index + i].v = 250;
                    break;

                case 3:
                    //left
                    _G_info.hsv[left_index - i].v = 125;
                    //right
                    _G_info.hsv[right_index + i].v = 125;
                    break;

                case 4:
                    //left
                    _G_info.hsv[left_index - i].v = 150;
                    //right
                    _G_info.hsv[right_index + i].v = 150;
                    break;

                default:
                    break;
            }
        }else if(i == display_num - 3) // 3.4  倒3
        {
            switch (display_num)
            {
                case 3:
                    //left
                    _G_info.hsv[left_index - i].v = 250;
                    //right
                    _G_info.hsv[right_index + i].v = 250;
                    break;

                case 4:
                    //left
                    _G_info.hsv[left_index - i].v = 180;
                    //right
                    _G_info.hsv[right_index + i].v = 180;
                    break;

                default:
                    break;
            }
        } else if(i == display_num - 4) // 4 倒4
        {
            switch (display_num)
            {
                case 4:
                    //left
                    _G_info.hsv[left_index - i].v = 250;
                    //right
                    _G_info.hsv[right_index + i].v = 250;
                    break;

                default:
                    break;
            }
        }
    }

    one_by_one_fill_info_buff(&_G_info, LED_NUM); //hsv-->rgb buf
    send_rgbbuf_to_spi(&_G_info); //发送一帧
    pthread_mutex_unlock(&info->rgb_buf_mutex);

    return 0;
}
/****************************************************************************
 * off
 ****************************************************************************/
int off_stay_init(RGB_MODE_INFO * info)
{
    info->time_m = 200;
    info->h = 0;
    info->s = 0;
    info->v = 0;

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);
    return 0;
}
/****************************************************************************
 * off
 ****************************************************************************/
int off_stay_run(RGB_MODE_INFO * info)
{   
    info->time_m = 200;
    info->h = 0;
    info->s = 0;
    info->v = 0;

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);
    return 0;
}
/****************************************************************************
 * mic mute
 ****************************************************************************/
int mic_mute_init(RGB_MODE_INFO * info)
{
    info->time_m = 10;
    info->h = 344;
    info->s = 245;
    info->v = 255;

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);
    return 0;
}
/****************************************************************************
 * mic mute
 ****************************************************************************/
int mic_mute_run(RGB_MODE_INFO * info)
{
    info->time_m = 10;
    info->h = 344;
    info->s = 245;
    info->v = 255;

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);
    return 0;
}

/****************************************************************************
 * 系统故障
 ****************************************************************************/
int system_fault_init(RGB_MODE_INFO * info)
{
    info->time_m = 10;
    info->h = 0;
    info->s = 255;
    info->v = 255;

    return 0;
}
/****************************************************************************
 * 系统故障
 ****************************************************************************/
int system_fault_run(RGB_MODE_INFO * info)
{
    fill_info_buff(info);
    send_rgbbuf_to_spi(info);
    return 0;
}
/****************************************************************************
 * 左右游走
 ****************************************************************************/
 /* 从中间到最右端，最终变为光点*/
static  int  loop_index;
#define   WHILT__CENTER     {.h = 330, .s = 70, .v = 170}
#define   WHILT__BACKGR     {.h = 330, .s = 70, .v = 36 }

COLOUR_HSV   right2left_hsv[17][LED_NUM] = {
    //4                   3              2              1              -1             -2             -3             -4
    [0] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [1] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [2] = {WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [3] = {WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [4] = {WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [5] = {WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [6] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [7] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [8] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
    [9] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR},
    [10] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR},
    [11] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER},
    [12] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER},
    [13] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR},
    [14] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR},
    [15] = {WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR, WHILT__CENTER, WHILT__BACKGR, WHILT__BACKGR, WHILT__BACKGR},
};
int walk_left_and_right_init_no_rest(RGB_MODE_INFO * info)
{
    info->time_m = 90;
    return 0;
}
int walk_left_and_right_init(RGB_MODE_INFO * info)
{
    loop_index = 0;
    info->time_m = 90;
    return 0;
}
/****************************************************************************
 * 左右游走
 ****************************************************************************/
int walk_left_and_right_run(RGB_MODE_INFO * info)
{
    memset(info->hsv, 0, sizeof(info->hsv));
    memcpy(info->hsv, &right2left_hsv[loop_index], sizeof(info->hsv));
    loop_index++;
    if(loop_index >= 16)
    {
        loop_index = 0;
    }
    one_by_one_fill_info_buff(info, LED_NUM);
    send_rgbbuf_to_spi(info);

    return 0;
}
/****************************************************************************
 * OTA upgrade
 ****************************************************************************/
 /* 从中间到最右端，最终变为光点*/
static  int ota_start_pos = 0;
int ota_upgrade_init(RGB_MODE_INFO * info)
{
    info->time_m = 125;
    ota_start_pos = 0;  //从最左边开始
    return 0;
}
/****************************************************************************
 * 从左向右游走
 ****************************************************************************/
int ota_upgrade_run(RGB_MODE_INFO * info)
{
    int i = 0;
    memset(info->hsv, 0, sizeof(info->hsv));
    //填充一帧
    for(i = 0; i < LED_NUM; i++)
    {
        if(i == ota_start_pos || i == ota_start_pos + 1)
        {
            info->hsv[i].h = 225;
            info->hsv[i].s = 230;
            info->hsv[i].v = 255;
        } 
    }

    ota_start_pos++;
    if(ota_start_pos == LED_NUM)
    {
        ota_start_pos = 0;
    }

    one_by_one_fill_info_buff(info, LED_NUM);
    send_rgbbuf_to_spi(info);
    return 0;
}
static  int  upgrade1_index;
#define   WHILT______P25     {.h = 225, .s = 230, .v = 60}
#define   WHILT______OFF     {.h = 0,   .s = 0,   .v = 0 }
#define   WHILT_ON__P125     {.h = 225, .s = 230, .v = 125}
#define   WHILT_ON__P255     {.h = 225, .s = 230, .v = 255}

COLOUR_HSV   boot_upgrade_hsv[11][LED_NUM] = {
    //4                    3                2              1                -1             -2              -3              -4
    [0] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_ON__P125},
    [1] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_ON__P255, WHILT_ON__P125},
    [2] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_ON__P255, WHILT_ON__P125, WHILT______P25},
    [3] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_ON__P255, WHILT_ON__P125, WHILT______P25, WHILT______OFF},
    [4] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_ON__P255, WHILT_ON__P125, WHILT______P25, WHILT______OFF, WHILT______OFF},
    [5] = {WHILT______OFF, WHILT______OFF, WHILT_ON__P255, WHILT_ON__P125, WHILT______P25, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [6] = {WHILT______OFF, WHILT_ON__P255, WHILT_ON__P125, WHILT______P25, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [7] = {WHILT_ON__P255, WHILT_ON__P125, WHILT______P25, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [8] = {WHILT_ON__P125, WHILT______P25, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [9] = {WHILT______P25, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF},
};
int upgrade1_init(RGB_MODE_INFO * info)
{
    upgrade1_index = 0;
    info->time_m = 120;
    return 0;
}
int upgrade1_run(RGB_MODE_INFO * info)
{
    if(upgrade1_index < 10) {
        memset(info->hsv, 0, sizeof(info->hsv));
        memcpy(info->hsv, &boot_upgrade_hsv[upgrade1_index], sizeof(info->hsv));
        upgrade1_index++;
        if(upgrade1_index >= 10)
        {
            upgrade1_index = 0;
        }
        one_by_one_fill_info_buff(info, LED_NUM);
        send_rgbbuf_to_spi(info);
    }
    return 0;
}
#if  0
/****************************************************************************
 * 从左向右游走
 ****************************************************************************/
static  int  ble_discv_index;
#define   BLUE__P25     {.h = 240, .s = 255, .v = 60}
#define   BLUE__OFF     {.h = 0,   .s = 0,   .v = 0 }
#define   BLUE_P125     {.h = 240, .s = 255, .v = 125}
#define   BLUE_P255     {.h = 210, .s = 255, .v = 150}

COLOUR_HSV   ble_discv_hsv[13][LED_NUM] = {
    //4                3         2          1          -1         -2         -3         -4
    [0] = {BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE_P255},
    [1] = {BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE_P255, BLUE__OFF},

    [2] = {BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE_P255, BLUE__OFF, BLUE__OFF},
    [3] = {BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE_P255, BLUE__OFF, BLUE__OFF, BLUE__OFF},
    [4] = {BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE_P255, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF},
    [5] = {BLUE__OFF, BLUE__OFF, BLUE_P255, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF},
    [6] = {BLUE__OFF, BLUE_P255, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF},
    [7] = {BLUE_P255, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF, BLUE__OFF},
};
int open_ble_discv_init(RGB_MODE_INFO * info)
{
    ble_discv_index = 0;
    info->time_m = 200;
    return 0;
}
int open_ble_discv_run(RGB_MODE_INFO * info)
{
    if(ble_discv_index < 8) {
        memset(info->hsv, 0, sizeof(info->hsv));
        memcpy(info->hsv, &ble_discv_hsv[ble_discv_index], sizeof(info->hsv));
        ble_discv_index++;

        if(ble_discv_index >= 8)
        {
            ble_discv_index = 0;
        }

        one_by_one_fill_info_buff(info, LED_NUM);
        send_rgbbuf_to_spi(info);
    }
    return 0;
}
#endif
/****************************************************************************
 * 蓝牙可发现打开，单向从左向右游走，蓝色
 ****************************************************************************/
static  bool  bt_turn_fade = false;
static  int   bt_turnon_pos = 1;
static  int   bt_fade_pos;
int open_ble_discv_init(RGB_MODE_INFO * info)
{
    bt_turn_fade = false; //turn on
    bt_turnon_pos = 7;
    bt_fade_pos = 7;
    info->time_m = 100;
    return 0;
}
/****************************************************************************
 * 蓝牙可发现打开，单向从左向右游走，蓝色
 ****************************************************************************/
int open_ble_discv_run(RGB_MODE_INFO * info)
{   
    int  i = 0;
    memset(info->hsv, 0, sizeof(info->hsv));

    if(!bt_turn_fade)
    {
        //填充一帧
        for(i = 7; i >= 0; i--)
        {
            if(i >= bt_turnon_pos)
            {
                info->hsv[i].h = 225;
                info->hsv[i].s = 230;
                info->hsv[i].v = 255;
            }
        }
        bt_turnon_pos--;
    } else  //fade
    {
        //填充一帧
        for(i = 7; i >= 0; i--)
        {
            if(i < bt_fade_pos)
            {
                info->hsv[i].h = 225;
                info->hsv[i].s = 230;
                info->hsv[i].v = 255;
            }
        }
        bt_fade_pos--;
    }
    if(bt_turnon_pos < 0)
    {
        bt_turnon_pos = 7;
        bt_fade_pos = 7;
        bt_turn_fade = true;
    }
    if(bt_fade_pos < 0)
    {
        bt_turnon_pos = 7;
        bt_fade_pos = 7;
        bt_turn_fade = false;
    }

    one_by_one_fill_info_buff(info, LED_NUM);
    send_rgbbuf_to_spi(info);
    return 0;
}
/****************************************************************************
 * 开机成功
 ****************************************************************************/
static  int  bts_index;
#define   WHILT_______ON    {.h = 330, .s = 70, .v = 100 }
#define   WHILT__ON__V50    {.h = 330, .s = 70, .v = 50  }
#define   WHILT__ON__255    {.h = 330, .s = 70, .v = 255 }
#define   WHILT__ON_WEAK    {.h = 330, .s = 70, .v = 20  }
#define   WHILT__ON_V80     {.h = 330, .s = 70, .v = 80  }
#define   WHILT__ON_V160    {.h = 330, .s = 70, .v = 160 }
#define   WHILT__ON_V120    {.h = 330, .s = 70, .v = 160 }
#define   WHILT__ON_V10     {.h = 330, .s = 70, .v = 10  }

COLOUR_HSV   boot_success_hsv[50][LED_NUM] = {
    //4                      3                  2                1                 -1                -2                -3                -4
//第 1轮聚笼
    [1] = {WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON},
    [2] = {WHILT______OFF, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT______OFF},
    [3] = {WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT______OFF, WHILT______OFF},
    [4] = {WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT__ON_V80, WHILT__ON_V80, WHILT______OFF, WHILT______OFF, WHILT_______ON},
//第 2轮聚笼
    [5] = {WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT__ON_V80, WHILT__ON_V80, WHILT______OFF, WHILT______OFF, WHILT_______ON},
    [6] = {WHILT______OFF, WHILT_______ON, WHILT______OFF, WHILT__ON_V80, WHILT__ON_V80, WHILT______OFF, WHILT_______ON, WHILT______OFF},
    [7] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT__ON_V80, WHILT__ON_V80, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [8] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT______OFF},

//三轮点亮汇聚
    [9] =  {WHILT__ON_WEAK, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT__ON_WEAK},
    [10] =  {WHILT__ON_V80, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT__ON_V80},
    [11] = {WHILT_______ON, WHILT______OFF, WHILT__ON_WEAK, WHILT_______ON, WHILT_______ON, WHILT__ON_WEAK, WHILT______OFF, WHILT_______ON},
    [12] = {WHILT_______ON, WHILT______OFF, WHILT__ON_V80, WHILT_______ON, WHILT_______ON,  WHILT__ON_V80,  WHILT______OFF, WHILT_______ON},
    [13] = {WHILT__ON__255, WHILT______OFF, WHILT_______ON, WHILT__ON__255, WHILT__ON__255, WHILT_______ON, WHILT______OFF, WHILT__ON__255}, //like all on
//缩放
    [14] = {WHILT______OFF, WHILT_______ON, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT_______ON, WHILT______OFF},
    [15] = {WHILT______OFF, WHILT__ON_WEAK, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON_WEAK, WHILT______OFF},
    [16] = {WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT__ON__255, WHILT__ON__255, WHILT_______ON, WHILT______OFF, WHILT______OFF},
    [17] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [18] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT__ON_V80, WHILT__ON_V80, WHILT______OFF, WHILT______OFF, WHILT______OFF},
//左右两侧展开
    [19] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [20] = {WHILT______OFF, WHILT______OFF, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT______OFF, WHILT______OFF},
    [21] = {WHILT______OFF, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT______OFF},
    [22] = {WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255},
//消隐
    [23] = {WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON},
    [24] = {WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON},
    [25] = {WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK},
    [26] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF},
};
int boot_success_init(RGB_MODE_INFO * info)
{
    bts_index = 0;
    info->time_m = 73;
    return 0;
}
int boot_success_run(RGB_MODE_INFO * info)
{
    if(bts_index < 27) {
        memset(info->hsv, 0, sizeof(info->hsv));
        memcpy(info->hsv, &boot_success_hsv[bts_index], sizeof(info->hsv));
        bts_index++;
        #if 0
        if(bts_index >= 22)
        {
            info->time_m = 40;
            rgb_scan_time_update(info);
            bts_index = 0;
        }
        #endif
        if(bts_index == 8)
        {
            info->time_m = 110;
            rgb_scan_time_update(info);
        }
        one_by_one_fill_info_buff(info, LED_NUM);
        send_rgbbuf_to_spi(info);
    }
    return 0;
}

COLOUR_HSV   boot_success_hsv1[50][LED_NUM] = {
    //4                      3                  2                1                 -1                -2                -3                -4
//第 1轮聚笼
    [1] = {WHILT__ON__V50, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT__ON__V50},
    [2] = {WHILT__ON_V80,  WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT__ON_V80},
    [3] = {WHILT______OFF, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT______OFF},
    [4] = {WHILT______OFF, WHILT______OFF, WHILT__ON_V120, WHILT______OFF, WHILT______OFF, WHILT__ON_V120, WHILT______OFF, WHILT______OFF},
    [5] = {WHILT______OFF, WHILT______OFF, WHILT__ON_V120, WHILT______OFF, WHILT__ON_V120, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [6] = {WHILT__ON_V80, WHILT______OFF, WHILT______OFF, WHILT__ON_V160, WHILT______OFF, WHILT______OFF, WHILT______OFF,  WHILT__ON_V80},
//第 2轮聚笼
    [7] = {WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT__ON_V160, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON},
    [8] = {WHILT______OFF, WHILT__ON_V120, WHILT______OFF, WHILT__ON_V160, WHILT______OFF, WHILT______OFF, WHILT__ON_V120, WHILT______OFF},
    [9] = {WHILT______OFF, WHILT______OFF, WHILT__ON_V10, WHILT__ON__255, WHILT______OFF, WHILT__ON_V120, WHILT______OFF, WHILT______OFF},
    [10] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT__ON__255, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [11] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT__ON__255, WHILT__ON_V10, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [12] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT__ON__255, WHILT__ON_WEAK, WHILT______OFF, WHILT______OFF, WHILT______OFF},
//三轮点亮汇聚
    [13] = {WHILT__ON_V80,  WHILT______OFF, WHILT______OFF, WHILT__ON__255, WHILT__ON__V50, WHILT______OFF, WHILT______OFF, WHILT__ON_V80},
    [14] = {WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT__ON__255, WHILT__ON_V80,  WHILT______OFF, WHILT______OFF, WHILT_______ON},
//中间变亮
    [15] = {WHILT__ON_V160, WHILT______OFF, WHILT______OFF, WHILT__ON__255, WHILT__ON_V160, WHILT______OFF, WHILT______OFF, WHILT__ON_V160},
    [16] = {WHILT__ON_V160, WHILT__ON_WEAK, WHILT______OFF, WHILT__ON__255, WHILT__ON__255, WHILT______OFF, WHILT__ON_V10,  WHILT__ON_V160},
    [17] = {WHILT_______ON, WHILT______OFF, WHILT__ON_V10, WHILT__ON__255, WHILT__ON__255, WHILT______OFF, WHILT__ON_WEAK, WHILT__ON_V160},

    [18] = {WHILT__ON_V160, WHILT______OFF, WHILT__ON_WEAK, WHILT__ON__255, WHILT__ON__255, WHILT______OFF, WHILT__ON__V50, WHILT__ON_V160},
    [19] = {WHILT______OFF, WHILT_______ON, WHILT__ON_WEAK, WHILT__ON__255, WHILT__ON__255, WHILT__ON_WEAK, WHILT__ON_V160, WHILT______OFF},
//link
    [20] = {WHILT______OFF, WHILT__ON_V160, WHILT__ON__V50, WHILT__ON__255, WHILT__ON__255, WHILT__ON__V50, WHILT__ON_V160, WHILT______OFF},
    [21] = {WHILT______OFF, WHILT______OFF, WHILT__ON_V160, WHILT__ON__255, WHILT__ON__255, WHILT__ON_V160, WHILT______OFF, WHILT______OFF},
    [22] = {WHILT______OFF, WHILT__ON_WEAK, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON_WEAK, WHILT______OFF},
//缩放
    [23] = {WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT__ON__255, WHILT__ON__255, WHILT_______ON, WHILT______OFF, WHILT______OFF},
    [24] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [25] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT__ON_V80, WHILT__ON_V80, WHILT______OFF, WHILT______OFF, WHILT______OFF},
//左右两侧展开
    [26] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT_______ON, WHILT_______ON, WHILT______OFF, WHILT______OFF, WHILT______OFF},
    [27] = {WHILT______OFF, WHILT______OFF, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT______OFF, WHILT______OFF},
    [28] = {WHILT______OFF, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT______OFF},
    [29] = {WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255, WHILT__ON__255},
//消隐
    [30] = {WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON},
    [31] = {WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON, WHILT_______ON},
    [32] = {WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK, WHILT__ON_WEAK},
    [33] = {WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF, WHILT______OFF},
};
int boot_success_init1(RGB_MODE_INFO * info)
{
    bts_index = 0;
    info->time_m = 80;
    return 0;
}
int boot_success_run1(RGB_MODE_INFO * info)
{
    if(bts_index < 34) {
        memset(info->hsv, 0, sizeof(info->hsv));
        memcpy(info->hsv, &boot_success_hsv1[bts_index], sizeof(info->hsv));
        bts_index++;
        #if 0
        if(bts_index >= 22)
        {
            info->time_m = 40;
            rgb_scan_time_update(info);
            bts_index = 0;
        }
        #endif
        if(bts_index == 26)
        {
            info->time_m = 110;
            rgb_scan_time_update(info);
        }
        one_by_one_fill_info_buff(info, LED_NUM);
        send_rgbbuf_to_spi(info);
    }
    return 0;
}

/****************************************************************************
 * 闹钟激活
 ****************************************************************************/
static  int  alarmactive_index;
#define   XGREEN0    {.h = 180, .s = 255, .v = 255}
#define   XGREEN1    {.h = 160, .s = 255, .v = 255}
#define   XGREEN2    {.h = 130, .s = 255, .v = 255}
#define   XGREEN3    {.h = 120, .s = 255, .v = 255}
#define   XGREEN4    {.h = 100, .s = 255, .v = 255}
#define   XGREEN5    {.h = 90, .s = 255, .v = 255}
#define   XGREEN6    {.h = 80, .s = 255, .v = 255}
#define   XGREEN7    {.h = 70, .s = 255, .v = 255}

#define   YELLOW0    {.h = 60, .s = 255, .v = 255}
#define   YELLOW1    {.h = 55, .s = 255, .v = 255}
#define   YELLOW2    {.h = 50, .s = 255, .v = 255}
#define   YELLOW3    {.h = 45, .s = 255, .v = 255}
#define   YELLOW4    {.h = 40, .s = 255, .v = 255}
#define   YELLOW5    {.h = 30, .s = 255, .v = 255}
#define   YELLOW6    {.h = 20, .s = 255, .v = 255}
#define   YELLOW7    {.h = 14, .s = 255, .v = 255}

#define   PURPLE0    {.h = 270, .s = 255, .v = 255}
#define   PURPLE1    {.h = 265, .s = 255, .v = 255}
#define   PURPLE2    {.h = 260, .s = 255, .v = 255}
#define   PURPLE3    {.h = 258, .s = 255, .v = 255}
#define   PURPLE4    {.h = 255, .s = 255, .v = 255}
#define   PURPLE5    {.h = 252, .s = 255, .v = 255}
#define   PURPLE6    {.h = 247, .s = 255, .v = 255}
#define   PURPLE7    {.h = 242, .s = 255, .v = 255}

#define   X_BLUE0    {.h = 240, .s = 255, .v = 255}
#define   X_BLUE1    {.h = 235, .s = 255, .v = 255}
#define   X_BLUE2    {.h = 230, .s = 255, .v = 255}
#define   X_BLUE3    {.h = 220, .s = 255, .v = 255}
#define   X_BLUE4    {.h = 210, .s = 255, .v = 255}
#define   X_BLUE5    {.h = 200, .s = 255, .v = 255}
#define   X_BLUE6    {.h = 190, .s = 255, .v = 255}
#define   X_BLUE7    {.h = 180, .s = 255, .v = 255}

#define   X_BLUE8    {.h = 160, .s = 255, .v = 255}
#define   X_BLUE9    {.h = 140, .s = 255, .v = 255}
#define   X_BLUEa    {.h = 120, .s = 255, .v = 255}
#define   X_BLUEb    {.h = 100, .s = 255, .v = 255}
#define   X_BLUEc    {.h = 80, .s = 255, .v = 255}


COLOUR_HSV   alarm_active_hsv[46][LED_NUM] = {
    //4            3       2       1       -1      -2      -3      -4
    [ 0] = {XGREEN0, XGREEN1, XGREEN2, XGREEN3, XGREEN4, XGREEN5, XGREEN6, XGREEN7},
    [ 1] = {XGREEN1, XGREEN2, XGREEN3, XGREEN4, XGREEN5, XGREEN6, XGREEN7, YELLOW0},
    [ 2] = {XGREEN2, XGREEN3, XGREEN4, XGREEN5, XGREEN6, XGREEN7, YELLOW0, YELLOW1},
    [ 3] = {XGREEN3, XGREEN4, XGREEN5, XGREEN6, XGREEN7, YELLOW0, YELLOW1, YELLOW2},
    [ 4] = {XGREEN4, XGREEN5, XGREEN6, XGREEN7, YELLOW0, YELLOW1, YELLOW2, YELLOW3},
    [ 5] = {XGREEN5, XGREEN6, XGREEN7, YELLOW0, YELLOW1, YELLOW2, YELLOW3, YELLOW4},
    [ 6] = {XGREEN6, XGREEN7, YELLOW0, YELLOW1, YELLOW2, YELLOW3, YELLOW4, YELLOW5},
    [ 7] = {XGREEN7, YELLOW0, YELLOW1, YELLOW2, YELLOW3, YELLOW4, YELLOW5, YELLOW6},

    [ 8] = {YELLOW0, YELLOW1, YELLOW2, YELLOW3, YELLOW4, YELLOW5, YELLOW6, YELLOW7},
    [ 9] = {YELLOW1, YELLOW2, YELLOW3, YELLOW4, YELLOW5, YELLOW6, YELLOW7, PURPLE0},
    [10] = {YELLOW2, YELLOW3, YELLOW4, YELLOW5, YELLOW6, YELLOW7, PURPLE0, PURPLE1},
    [11] = {YELLOW3, YELLOW4, YELLOW5, YELLOW6, YELLOW7, PURPLE0, PURPLE1, PURPLE2},
    [12] = {YELLOW4, YELLOW5, YELLOW6, YELLOW7, PURPLE0, PURPLE1, PURPLE2, PURPLE3},
    [13] = {YELLOW5, YELLOW6, YELLOW7, PURPLE0, PURPLE1, PURPLE2, PURPLE3, PURPLE4},
    [14] = {YELLOW6, YELLOW7, PURPLE0, PURPLE1, PURPLE2, PURPLE3, PURPLE4, PURPLE5},
    [15] = {YELLOW7, PURPLE0, PURPLE1, PURPLE2, PURPLE3, PURPLE4, PURPLE5, PURPLE6},

    [16] = {PURPLE0, PURPLE1, PURPLE2, PURPLE3, PURPLE4, PURPLE5, PURPLE6, PURPLE7},
    [17] = {PURPLE1, PURPLE2, PURPLE3, PURPLE4, PURPLE5, PURPLE6, PURPLE7, X_BLUE0},
    [18] = {PURPLE2, PURPLE3, PURPLE4, PURPLE5, PURPLE6, PURPLE7, X_BLUE0, X_BLUE1},
    [19] = {PURPLE3, PURPLE4, PURPLE5, PURPLE6, PURPLE7, X_BLUE0, X_BLUE1, X_BLUE2},
    [20] = {PURPLE4, PURPLE5, PURPLE6, PURPLE7, X_BLUE0, X_BLUE1, X_BLUE2, X_BLUE3},
    [21] = {PURPLE5, PURPLE6, PURPLE7, X_BLUE0, X_BLUE1, X_BLUE2, X_BLUE3, X_BLUE4},
    [22] = {PURPLE6, PURPLE7, X_BLUE0, X_BLUE1, X_BLUE2, X_BLUE3, X_BLUE4, X_BLUE5},
    [23] = {PURPLE7, X_BLUE0, X_BLUE1, X_BLUE2, X_BLUE3, X_BLUE4, X_BLUE5, X_BLUE6},
    [24] = {X_BLUE0, X_BLUE1, X_BLUE2, X_BLUE3, X_BLUE4, X_BLUE5, X_BLUE6, X_BLUE7},

    [25] = {X_BLUE1, X_BLUE2, X_BLUE3, X_BLUE4, X_BLUE5, X_BLUE6, X_BLUE7, X_BLUE8},
    [26] = {X_BLUE2, X_BLUE3, X_BLUE4, X_BLUE5, X_BLUE6, X_BLUE7, X_BLUE8, X_BLUE9},
    [27] = {X_BLUE3, X_BLUE4, X_BLUE5, X_BLUE6, X_BLUE7, X_BLUE8, X_BLUE9, X_BLUEa},
    [28] = {X_BLUE4, X_BLUE5, X_BLUE6, X_BLUE7, X_BLUE8, X_BLUE9, X_BLUEa, X_BLUEb},
    [29] = {X_BLUE5, X_BLUE6, X_BLUE7, X_BLUE8, X_BLUE9, X_BLUEa, X_BLUEb, X_BLUEc},
    [30] = {X_BLUE6, X_BLUE7, X_BLUE8, X_BLUE9, X_BLUEa, X_BLUEb, X_BLUEc, YELLOW0},
    [31] = {X_BLUE7, X_BLUE8, X_BLUE9, X_BLUEa, X_BLUEb, X_BLUEc, YELLOW0, YELLOW2},
    [32] = {X_BLUE8, X_BLUE9, X_BLUEa, X_BLUEb, X_BLUEc, YELLOW0, YELLOW2, YELLOW4},
    [33] = {X_BLUE9, X_BLUEa, X_BLUEb, X_BLUEc, YELLOW0, YELLOW2, YELLOW4, XGREEN7},
    [34] = {X_BLUEa, X_BLUEb, X_BLUEc, YELLOW0, YELLOW2, YELLOW4, XGREEN7, XGREEN5},
    [35] = {X_BLUEb, YELLOW0, YELLOW0, YELLOW2, YELLOW4, XGREEN7, XGREEN5, XGREEN3},

    [36] = {YELLOW0, YELLOW1, YELLOW2, YELLOW3, YELLOW4, XGREEN7, XGREEN5, XGREEN3},
    [37] = {YELLOW1, YELLOW2, YELLOW3, YELLOW4, XGREEN7, XGREEN5, XGREEN3, XGREEN2},
    [38] = {YELLOW2, YELLOW3, YELLOW4, XGREEN7, XGREEN5, XGREEN3, XGREEN2, XGREEN1},
    [39] = {YELLOW3, YELLOW4, XGREEN7, XGREEN5, XGREEN3, XGREEN2, XGREEN1, XGREEN0},
    [40] = {YELLOW4, XGREEN7, XGREEN5, XGREEN3, XGREEN2, XGREEN1, XGREEN0, XGREEN1},
    [41] = {XGREEN7, XGREEN5, XGREEN3, XGREEN2, XGREEN1, XGREEN0, XGREEN1, XGREEN2},
    [42] = {XGREEN5, XGREEN3, XGREEN2, XGREEN1, XGREEN0, XGREEN1, XGREEN2, XGREEN3},
    [43] = {XGREEN3, XGREEN2, XGREEN1, XGREEN0, XGREEN1, XGREEN2, XGREEN3, XGREEN4},
    [44] = {XGREEN2, XGREEN1, XGREEN0, XGREEN1, XGREEN2, XGREEN3, XGREEN4, XGREEN5},
    [45] = {XGREEN1, XGREEN0, XGREEN1, XGREEN2, XGREEN3, XGREEN4, XGREEN5, XGREEN6},

};
int alarm_active_init(RGB_MODE_INFO * info)
{
    alarmactive_index = 0;
    info->time_m = 190;
    return 0;
}
int alarm_active_run(RGB_MODE_INFO * info)
{
    memset(info->hsv, 0, sizeof(info->hsv));
    memcpy(info->hsv, &alarm_active_hsv[alarmactive_index], sizeof(info->hsv));
    alarmactive_index++;

    if(alarmactive_index == 25)
    {
        info->time_m = 140;
        rgb_scan_time_update(info);
    }
    if(alarmactive_index == 36)
    {
        info->time_m = 190;
        rgb_scan_time_update(info);
    }
    
    if(alarmactive_index >= 46)
    {
        alarmactive_index = 0;
    }

    one_by_one_fill_info_buff(info, LED_NUM);
    send_rgbbuf_to_spi(info);

    return 0;
}
#if 0
/****************************************************************************
 * 蓝牙连接成功，持续1s 后熄灭
 ****************************************************************************/
int ble_conn_ok_init(RGB_MODE_INFO * info)
{
    info->time_m = 25;

    info->h = 210;
    info->s = 255;
    info->v = 255;

    unsigned long long cur_time_ms = time_get_ms();
    info->Special_mode_time = 1000 + cur_time_ms; // 延长闪烁时间

    return 0;
}
/****************************************************************************
 * 蓝牙连接成功，持续1s 后熄灭
 ****************************************************************************/
int ble_conn_ok_run(RGB_MODE_INFO * info)
{
    info->h = 210;
    info->s = 255;
    info->v = 255;
    fill_info_buff(info);
    send_rgbbuf_to_spi(info);
    return 0;
}
#endif
#define  SET_PRE_CR0           {.h = 330, .s = 70, .v = 0}
#define  SET_PRE_CR20           {.h = 330, .s = 70, .v = 20}
#define  SET_PRE_CR60           {.h = 330, .s = 70, .v = 60}
#define  SET_PRE_CR100          {.h = 330, .s = 70, .v = 100}
#define  SET_PRE_CR140          {.h = 330, .s = 70, .v = 140}
#define  SET_PRE_CR200          {.h = 330, .s = 70, .v = 200}
static  int  set_pre_index;
COLOUR_HSV   pre_config_hsv[24][LED_NUM] = 
{
    [1] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR60, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR60, SET_PRE_CR0, SET_PRE_CR0},
    [2] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR60, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR60, SET_PRE_CR0, SET_PRE_CR0},
    [3] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR60, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR60, SET_PRE_CR0, SET_PRE_CR0},
    [4] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR0, SET_PRE_CR0},
    [5] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR100, SET_PRE_CR140, SET_PRE_CR140, SET_PRE_CR100, SET_PRE_CR0, SET_PRE_CR0},
    [7] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR60, SET_PRE_CR140, SET_PRE_CR140, SET_PRE_CR60, SET_PRE_CR0, SET_PRE_CR0},
    [8] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR140, SET_PRE_CR140, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0},
    [9] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR140, SET_PRE_CR140, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0},
    [10] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR20, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR20, SET_PRE_CR0, SET_PRE_CR0},
    [11] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR60, SET_PRE_CR140, SET_PRE_CR140, SET_PRE_CR60, SET_PRE_CR0, SET_PRE_CR0},
    [12] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR60, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR60, SET_PRE_CR0, SET_PRE_CR0},
    [13] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR140, SET_PRE_CR140, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0},
    [14] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR140, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR140, SET_PRE_CR0, SET_PRE_CR0},
    [15] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0},
    [16] = {SET_PRE_CR0, SET_PRE_CR20, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR20, SET_PRE_CR0},
    [17] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR60, SET_PRE_CR140, SET_PRE_CR140, SET_PRE_CR60, SET_PRE_CR0, SET_PRE_CR0},
    [18] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR60, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR60, SET_PRE_CR0, SET_PRE_CR0},
    [19] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0},
    [20] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0},
    [21] = {SET_PRE_CR0, SET_PRE_CR20, SET_PRE_CR60, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR60, SET_PRE_CR20, SET_PRE_CR0},
    [22] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR100, SET_PRE_CR100, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0},
    [23] = {SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0, SET_PRE_CR0},
};
int  set_pre_config_mode_init(RGB_MODE_INFO * info)
{
    set_pre_index = 0;
    info->time_m = 80;
    return 0;
}

int  set_pre_config_mode_run(RGB_MODE_INFO * info)
{
    if(set_pre_index < 24)
    {
        memset(info->hsv, 0, sizeof(info->hsv));
        memcpy(info->hsv, &pre_config_hsv[set_pre_index], sizeof(info->hsv));
        set_pre_index++;
        one_by_one_fill_info_buff(info, LED_NUM);
        send_rgbbuf_to_spi(info);
    } 

    return 0;
}
/****************************************************************************
 * 蓝牙连接成功，持续1s 后熄灭
 ****************************************************************************/
static  int  bt_con_on_index;
#define  CON__OK_CR          {.h = 225, .s = 230, .v = 255}
#define  CON_OFF_CR          {.h = 0,   .s = 0,   .v = 0  }
#define  CON__OK_CR_V120     {.h = 225, .s = 230, .v = 120}
#define  CON__OK_CR_VV60     {.h = 225, .s = 230, .v = 60}
#define  CON__OK_CR_VV20     {.h = 225, .s = 230, .v = 20}
COLOUR_HSV   bt_conn_ok_hsv[9][LED_NUM] = 
{
    [1] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR},
    [2] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR},
    [3] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [4] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [5] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [6] = {CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [7] = {CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [8] = {CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},

};
COLOUR_HSV   bt_conn_ok_fade_hsv[4][LED_NUM] = 
{
    [0] = {CON__OK_CR_V120, CON__OK_CR_V120, CON__OK_CR_V120, CON__OK_CR_V120, CON__OK_CR_V120, CON__OK_CR_V120, CON__OK_CR_V120, CON__OK_CR_V120},
    [1] = {CON__OK_CR_VV60, CON__OK_CR_VV60, CON__OK_CR_VV60, CON__OK_CR_VV60, CON__OK_CR_VV60, CON__OK_CR_VV60, CON__OK_CR_VV60, CON__OK_CR_VV60},
    [2] = {CON__OK_CR_VV20, CON__OK_CR_VV20, CON__OK_CR_VV20, CON__OK_CR_VV20, CON__OK_CR_VV20, CON__OK_CR_VV20, CON__OK_CR_VV20, CON__OK_CR_VV20},
    [3] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR},
};
int ble_conn_ok_init(RGB_MODE_INFO * info)
{
    info->time_m = 80;
    bt_con_on_index = 0;
    unsigned long long cur_time_ms = time_get_ms();
    info->Special_mode_time = 1800 + cur_time_ms; // 延长闪烁时间

    return 0;
}
int ble_conn_ok_run(RGB_MODE_INFO * info)
{
    if(bt_con_on_index < 9)
    {
        memset(info->hsv, 0, sizeof(info->hsv));
        memcpy(info->hsv, &bt_conn_ok_hsv[bt_con_on_index], sizeof(info->hsv));

        one_by_one_fill_info_buff(info, LED_NUM);
        send_rgbbuf_to_spi(info);
    } else 
    {
        if(bt_con_on_index == 19 || bt_con_on_index == 20 || bt_con_on_index == 21 || bt_con_on_index == 22)
        {
            memset(info->hsv, 0, sizeof(info->hsv));
            switch(bt_con_on_index)
            {
                case 19: memcpy(info->hsv, &bt_conn_ok_fade_hsv[0], sizeof(info->hsv)); break;
                case 20: memcpy(info->hsv, &bt_conn_ok_fade_hsv[1], sizeof(info->hsv)); break;
                case 21: memcpy(info->hsv, &bt_conn_ok_fade_hsv[2], sizeof(info->hsv)); break;
                case 22: memcpy(info->hsv, &bt_conn_ok_fade_hsv[3], sizeof(info->hsv)); break;
                default: break;
            }
            one_by_one_fill_info_buff(info, LED_NUM);
            send_rgbbuf_to_spi(info);
        }
    }
    bt_con_on_index++;

    return 0;
}

/****************************************************************************
 * 蓝牙 disconnected，持续1s 后熄灭
 ****************************************************************************/
static int bt_disconn_index;
COLOUR_HSV   bt_disconn_hsv[10][LED_NUM] = 
{
    [1] = {CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [2] = {CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [3] = {CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [4] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [5] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [6] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR, CON__OK_CR},
    [7] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR, CON__OK_CR},
    [8] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON__OK_CR},
    [9] = {CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR, CON_OFF_CR},
};
int bt_disconnected_init(RGB_MODE_INFO * info)
{
    info->time_m = 90;
    bt_disconn_index = 0;
    unsigned long long cur_time_ms = time_get_ms();
    info->Special_mode_time = 1100 + cur_time_ms; // 延长闪烁时间

    return 0;
}
int bt_disconnected_run(RGB_MODE_INFO * info)
{
    if(bt_disconn_index < 10)
    {
        memset(info->hsv, 0, sizeof(info->hsv));
        memcpy(info->hsv, &bt_disconn_hsv[bt_disconn_index], sizeof(info->hsv));
        bt_disconn_index++;
        one_by_one_fill_info_buff(info, LED_NUM);
        send_rgbbuf_to_spi(info);
    }

    return 0;
}
/****************************************************************************
 * 初始化/配置，wifi连接过程，单向从左向右游走，橙色
 ****************************************************************************/
static  bool  turn_fade = false;
static  int   turnon_pos = 1;
static  int   fade_pos;
int init_config_init(RGB_MODE_INFO * info)
{
    turn_fade = false; //turn on
    turnon_pos = 7;
    fade_pos = 7;
    info->time_m = 100;
    return 0;
}
/****************************************************************************
 * 初始化/配置，wifi连接过程，单向从左向右游走，橙色
 ****************************************************************************/
int init_config_run(RGB_MODE_INFO * info)
{
    int  i = 0;
    memset(info->hsv, 0, sizeof(info->hsv));

    if(!turn_fade)
    {
        //填充一帧
        for(i = 7; i >= 0; i--)
        {
            if(i >= turnon_pos)
            {
                info->hsv[i].h = 12;
                info->hsv[i].s = 255;
                info->hsv[i].v = 255;
            }
        }
        turnon_pos--;
    } else  //fade
    {
        //填充一帧
        for(i = 7; i >= 0; i--)
        {
            if(i < fade_pos)
            {
                info->hsv[i].h = 12;
                info->hsv[i].s = 255;
                info->hsv[i].v = 255;
            }
        }
        fade_pos--;
    }
    if(turnon_pos < 0)
    {
        turnon_pos = 7;
        fade_pos = 7;
        turn_fade = true;
    }
    if(fade_pos < 0)
    {
        turnon_pos = 7;
        fade_pos = 7;
        turn_fade = false;
    }

    one_by_one_fill_info_buff(info, LED_NUM);
    send_rgbbuf_to_spi(info);
    return 0;
}
/****************************************************************************
 * 两边向中间聚拢
 ****************************************************************************/
static  volatile  int mv_pos = 0;
static  volatile  int fade_step  = 0;
int side_to_center_init(RGB_MODE_INFO * info)
{
    info->time_m = 50;
    mv_pos = 0;
    fade_step = 0;
    return 0;
}
/****************************************************************************
 * 两边向中间聚拢
 ****************************************************************************/
int side_to_center_run(RGB_MODE_INFO * info)
{
    int i = 0;
    memset(info->hsv, 0, sizeof(info->hsv));
    //填充一帧
    for(i = 0; i < LED_NUM; i++)
    {
        //left and right
        if(mv_pos == i || (7 - mv_pos) == i)
        {
            info->hsv[i].h = 350;
            info->hsv[i].s = 70;
            info->hsv[i].v = 255;
        }
    }

    if(mv_pos < 3)
    {
        mv_pos++;
    } else {  //聚在中间，亮度渐灭
        if(fade_step < 130)
        {
            fade_step += 40;
        }
        if(fade_step > 130)
        {
            fade_step = 130;
        }
        info->hsv[3].v = 255 - fade_step;  //变为 120 和拾音保持一致
        info->hsv[4].v = 255 - fade_step;
    }

    one_by_one_fill_info_buff(info, LED_NUM);
    send_rgbbuf_to_spi(info);
    return 0;
}

/****************************************************************************
 * 蓝色呼吸
 ****************************************************************************/
static bool isWbMaxV = false;
int blue_breath_init(RGB_MODE_INFO * info)
{
    struct timeval current_time;
    unsigned long long   cur_time_ms;

    gettimeofday(&current_time, NULL);
    cur_time_ms = (current_time.tv_sec) * 1000 + (current_time.tv_usec) / 1000;  //转化为ms

    info->time_m = 25;
    info->Special_mode_time = 5000 + cur_time_ms; // 延长闪烁时间

    info->v = 200;
    info->h = 240;
    info->s = 255;

    info->mult_u32[0]=0;
    isWbMaxV = false;

    return 0;
}
/****************************************************************************
 * 蓝色呼吸
 ****************************************************************************/
int blue_breath_run(RGB_MODE_INFO * info)
{
    info->mult_u32[0] += 10;
    unsigned int *v_cn = &info->mult_u32[0];

    if(*v_cn >= 200)                                           /* 到达最大亮度 / 或到达最低亮度                    */
    {
        *v_cn = 0;                                             /* 清 0 计数                                        */
        isWbMaxV = isWbMaxV == false? true : false;
    }
    if(isWbMaxV) {
        info->v = 200 - (*v_cn);
    } else
    {
        info->v=(*v_cn);
    }

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);

    return 0;
}
/****************************************************************************
 * 未识别到指令声音，点亮 1 和 -1 灯，超时返回
 ****************************************************************************/
int no_sound_detect_init(RGB_MODE_INFO * info)
{
    struct timeval current_time;
    unsigned long long   cur_time_ms;

    gettimeofday(&current_time, NULL);
    cur_time_ms = (current_time.tv_sec) * 1000 + (current_time.tv_usec) / 1000;  //转化为ms

    info->time_m = 25;
    info->Special_mode_time = 5000 + cur_time_ms; // 延长闪烁时间

    return 0;
}
int no_sound_detect_run(RGB_MODE_INFO * info)
{
    int i = 0;
    memset(info->hsv, 0, sizeof(info->hsv));
    //填充一帧
    for(i = 0; i < LED_NUM; i++)
    {
        //left and right
        if(3 == i || 4 == i)
        {
            info->hsv[i].h = 0;
            info->hsv[i].s = 0;
            info->hsv[i].v = 120;
        }
    }

    one_by_one_fill_info_buff(info, LED_NUM);
    send_rgbbuf_to_spi(info);

    return 0;
}
/****************************************************************************
 * 白色呼吸
 ****************************************************************************/
static bool isRbMaxV = false;
int whilt_breath_init(RGB_MODE_INFO * info)
{
    info->h = 330;
    info->s = 70;
    info->v = 255;

    info->mult_u32[0]=0;
    isRbMaxV = false;

    info->time_m = 28;

    return 0;
}
/****************************************************************************
 * 白色呼吸
 ****************************************************************************/
int whilt_breath_run(RGB_MODE_INFO * info)
{
    info->mult_u32[0] += 10;
    unsigned int *v_cn = &info->mult_u32[0];

    if(*v_cn >= 255)                                           /* 到达最大亮度 / 或到达最低亮度                    */
    {
        *v_cn = 0;                                             /* 清 0 计数                                        */
        isRbMaxV = isRbMaxV == false? true : false;
    }
    if(isRbMaxV) {
        info->v = 255 - (*v_cn);
    } else
    {
        info->v=(*v_cn);
    }

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);

    return 0;
}
/****************************************************************************
 * 歌曲切换时加载资源
 ****************************************************************************/
static bool isplsiMaxV = false;
int playing_load_source_init(RGB_MODE_INFO * info)
{
    info->h = 330;
    info->s = 70;
    info->v = 255;

    info->mult_u32[0]=0;
    isplsiMaxV = false;

    info->time_m = 50;

    return 0;
}
/****************************************************************************
 * 歌曲切换时加载资源
 ****************************************************************************/
int playing_load_source_run(RGB_MODE_INFO * info)
{
    info->mult_u32[0] += 10;
    unsigned int *v_cn = &info->mult_u32[0];

    if(*v_cn >= 255)                                           /* 到达最大亮度 / 或到达最低亮度                    */
    {
        *v_cn = 0;                                             /* 清 0 计数                                        */
        isplsiMaxV = isplsiMaxV == false? true : false;
    }
    if(isplsiMaxV) {
        info->v = 255 - (*v_cn);
    } else
    {
        info->v=(*v_cn);
    }

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);

    return 0;
}
/****************************************************************************
 * voip 通话中
 ****************************************************************************/
static bool iopibusyMaxV = false;
int voip_busy_init(RGB_MODE_INFO * info)
{
    info->h = 0;
    info->s = 0;
    info->v = 255;

    info->mult_u32[0]=0;
    iopibusyMaxV = false;

    info->time_m = 50;

    return 0;
}
/****************************************************************************
 * voip通话中
 ****************************************************************************/
int voip_busy_run(RGB_MODE_INFO * info)
{
    info->mult_u32[0] += 10;
    unsigned int *v_cn = &info->mult_u32[0];

    if(*v_cn >= 255)                                           /* 到达最大亮度 / 或到达最低亮度                    */
    {
        *v_cn = 0;                                             /* 清 0 计数                                        */
        iopibusyMaxV = iopibusyMaxV == false? true : false;
    }
    if(iopibusyMaxV) {
        info->v = 255 - (*v_cn);
    } else
    {
        info->v=(*v_cn);
    }

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);

    return 0;
}
/****************************************************************************
 * 音箱断网橙色呼吸
 ****************************************************************************/
static bool isOrMaxV = false;
int orange_breath_init(RGB_MODE_INFO * info)
{
    info->h = 13;
    info->s = 255;
    info->v = 255;

    info->mult_u32[0]=0;
    isOrMaxV = false;

    info->time_m = 65;

    return 0;
}
/****************************************************************************
 * 音箱断网橙色呼吸
 ****************************************************************************/
int orange_breath_run(RGB_MODE_INFO * info)
{
    info->mult_u32[0] += 10;
    unsigned int *v_cn = &info->mult_u32[0];

    if(*v_cn >= 255)                                           /* 到达最大亮度 / 或到达最低亮度                    */
    {
        *v_cn = 0;                                             /* 清 0 计数                                        */
        isOrMaxV = isOrMaxV == false? true : false;
    }
    if(isOrMaxV) {
        info->v = 255 - (*v_cn);
    } else
    {
        info->v=(*v_cn);
    }

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);

    return 0;
}
/****************************************************************************
 * 组合播放，呼吸单次
 ****************************************************************************/
static bool isCPMaxV = false;
static int  cp_breath_cnt = 0;
int comb_play_init(RGB_MODE_INFO * info)
{
    info->h = 160;
    info->s = 255;
    info->v = 255;

    info->mult_u32[0]=0;   //OFF
    isCPMaxV = false;
    cp_breath_cnt = 0;

    info->time_m = 50;

    return 0;
}
/****************************************************************************
 * 组合播放，呼吸单次
 ****************************************************************************/
int comb_play_run(RGB_MODE_INFO * info)
{
    info->mult_u32[0] += 10;
    unsigned int *v_cn = &info->mult_u32[0];

    if(cp_breath_cnt <= 1)     //one time breath
    {
        if(*v_cn >= 255)                                           /* 到达最大亮度 / 或到达最低亮度                    */
        {
            cp_breath_cnt++;
            *v_cn = 0;                                             /* 清 0 计数                                        */
            isCPMaxV = isCPMaxV == false? true : false;
        }
        if(isCPMaxV) {
            info->v = 255 - (*v_cn);
        } else
        {
            info->v=(*v_cn);
        }
    }else
    {
        info->v = 0;
    }

    fill_info_buff(info);
    send_rgbbuf_to_spi(info);

    return 0;
}
/****************************************************************************
律动灯效
*****************************************************************************/
#define TIMER_PIECE_MS      (43) //每个样本 100 ms
#define SAVE_BUF_SIZE       (100) //样本最大个数
#if 0
/**
 * @brief
 *
 * @param in_energy 输入值
 * @param out_max 最大值
 * @param out_mix 最小值
 * @param cycle_sec 时间段  最长时间(TIMER_PIECE_MS*SAVE_BUF_SIZE)/1000  s
 */
static void max_energy(int in_energy,int *out_max,int *out_mix,int cycle_sec){

    int dac_energy_max = 0, dac_energy_mix = 4294967295;
    // int dac_energy = audio_dac_energy_get();
    static int energy_max_table[SAVE_BUF_SIZE]={0};
    static int energy_mix_table[SAVE_BUF_SIZE]={0};
    static int cycle=0;

    if(!cycle_sec){
        *out_max=*out_mix=in_energy;
        return;
    }
    cycle_sec*=10;
    cycle_sec=((cycle_sec>SAVE_BUF_SIZE)?SAVE_BUF_SIZE:cycle_sec);

    struct timeval current_time;

    gettimeofday(&current_time, NULL);

    //curms /100 ms,表示第几个 100ms ，再取余 cycle_sec，表示在数组中的index，这样一个样本时间内的多次采样，都会放到同一个 index 位置
    unsigned int cur_ms = (((current_time.tv_sec) * 1000 + (current_time.tv_usec) / 1000) / TIMER_PIECE_MS) % cycle_sec;  //ms
    static unsigned int  old_ms = 0;


    if(old_ms!=cur_ms){
        energy_max_table[cur_ms] = 0;
        energy_mix_table[cur_ms] = 4294967295;  //int 的最大范围
        old_ms = cur_ms;
    }

    if(energy_max_table[cur_ms]<in_energy){
        energy_max_table[cur_ms] = in_energy;
    }

    if(energy_mix_table[cur_ms]>in_energy){
        energy_mix_table[cur_ms] = in_energy;
    }

    //最大能量值 最新能量值
    for(int i = 0;i<cycle_sec;i++){
        if(dac_energy_max<energy_max_table[i]){
            dac_energy_max = energy_max_table[i];
        }
        if(dac_energy_mix>energy_mix_table[i]){
            dac_energy_mix = energy_mix_table[i];
        }
    }
    *out_max = dac_energy_max;
    *out_mix = dac_energy_mix;
}
#endif
/****************************************************************************
 * 律动灯效 1
 ****************************************************************************/
int dyn_01_init(RGB_MODE_INFO * info)
{
    int i = 0;
    for(i = 0; i < LED_NUM; i++)
    {
        //填充每个灯的hsv
        info->hsv[i].h = 0;
        info->hsv[i].s = 0;
        info->hsv[i].v = 0;
    }
    info->time_m = MUSIC_PLAY_INIT_TIME;//模式刷新时间
    info->time_d = MUSIC_PLAY_INIT_TIME;//能量检测刷新时间

    return 0;
}
int dyn_tts_init(RGB_MODE_INFO * info)
{
    int i = 0;
    for(i = 0; i < LED_NUM; i++)
    {
        //填充每个灯的hsv
        info->hsv[i].h = 330;
        info->hsv[i].s = 70;
        info->hsv[i].v = 255;
    }
    info->time_m = TTS_PLAY_INIT_TIME;//模式刷新时间
    info->time_d = TTS_PLAY_INIT_TIME;//能量检测刷新时间

    return 0;
}
int dyn_pick_init(RGB_MODE_INFO * info)
{
    int i = 0;
    for(i = 0; i < LED_NUM; i++)
    {
        //填充每个灯的hsv
        info->hsv[i].h = 330;
        info->hsv[i].s = 70;
        info->hsv[i].v = 255;
    }
    info->time_m = VOICE_PICK_INIT_TIME;//模式刷新时间
    info->time_d = VOICE_PICK_INIT_TIME;//能量检测刷新时间

    return 0;
}
/****************************************************************************
 * 获取ms
 ****************************************************************************/
unsigned long long  time_get_ms(void)
{
    struct timeval current_time;
    unsigned long long  cur_time_ms;

    gettimeofday(&current_time, NULL);

    cur_time_ms = (current_time.tv_sec) * 1000 + (current_time.tv_usec) / 1000;  //转化为ms
    return  cur_time_ms;
}
/****************************************************************************
 * 发送律动灯效
 ****************************************************************************/
int  send_rhythm(RHYTHM_MSG  msg)
{
	int status = 0;
    mqd_t mqfd;

    mqfd = mq_open(RHYTHM_QUEUE_NAME, O_WRONLY | O_NONBLOCK);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"send_rhythm: ERROR mq_open failed errno = %d\r\n", errno);
        return (-1);
    }

    status = mq_send(mqfd, (const char*)&msg, RHYTHM_MSG_LEN, 42);
    if (status < 0)
    {
        syslog(LOG_ERR,"send_rhythm: ERROR mq_send failure=%d errno = %d\r\n", status, errno);
    }

    mq_close(mqfd);

    return (status);
}

int  send_pvrhythm(RHYTHM_MSG  msg)
{
	int status = 0;
    mqd_t mqfd;

    mqfd = mq_open(PV_RHYTHM_QUEUE_NAME, O_WRONLY | O_NONBLOCK);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"send_pvrhythm: ERROR mq_open failed errno = %d\r\n", errno);
        return (-1);
    }

    status = mq_send(mqfd, (const char*)&msg, RHYTHM_MSG_LEN, 42);
    if (status < 0)
    {
        syslog(LOG_ERR,"send_pvrhythm: ERROR mq_send failure=%d errno = %d\r\n", status, errno);
    }

    mq_close(mqfd);

    return (status);
}

/****************************************************************************
 * 发送 num & v
 ****************************************************************************/
int  send_grad_change(GRAD_CHANGE_MSG  msg)
{
	int status = 0;
    mqd_t mqfd;

    mqfd = mq_open(GRAD_CHANGE_QUEUE_NAME, O_WRONLY | O_NONBLOCK);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"send_grad_change: ERROR mq_open failed errno = %d\r\n", errno);
        return (-1);
    }

    status = mq_send(mqfd, (const char*)&msg, GRAD_CHANGE_MSG_LEN, 42);
    if (status < 0)
    {
        syslog(LOG_ERR,"send_grad_change: ERROR mq_send failure=%d errno = %d\r\n", status, errno);
    }

    mq_close(mqfd);

    return 0;
}

/****************************************************************************
 * 从能量队列中获取能量值
 ****************************************************************************/
double rhy_old_energy = 0;
#if 0
int  get_audio_energy(double *energy)
{
    int                 nbytes;
    RECV_ENERGY_MSG     msg_buffer;

    mqd_t mqfd = mq_open(RHYTHM_RECV_ENERGY_QUEUE, O_RDONLY | O_NONBLOCK);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"get_audio_energy: ERROR mq_open failed errno = %d\r\n", errno);
        return -1;
    }
    //read one time energy, non block
    nbytes = mq_receive(mqfd, (char*)&msg_buffer, RHYTHM_RECV_ENERGY_MSG_LEN, NULL);
    if (nbytes < 0)
    {
        *energy = rhy_old_energy;
    } else if (nbytes != RHYTHM_RECV_ENERGY_MSG_LEN)
    {
        syslog(LOG_ERR," get_audio_energy:mq_receive return bad size %d \r\n", nbytes);
        *energy = rhy_old_energy;
    } else
    {
        *energy = msg_buffer.energy;
    }

    rhy_old_energy = *energy;
    mq_close(mqfd);

    return 0;
}
#endif

int  get_audio_energy(double *energy)
{
    int ret = -1;

    ret = pthread_mutex_trylock(&_G_info.cur_percent_mutex);
    if(ret == 0)
    {
        *energy = _G_info.cur_percent;
        pthread_mutex_unlock(&_G_info.cur_percent_mutex);
    } else 
    {
        *energy = rhy_old_energy;
    }
    rhy_old_energy = *energy;

    return 0;
}

/* 播放音乐色值轮换表*/
#define  RT_LEN     (27)
#define  STEP       (22)
COLOUR_HSV  music_colour_rt[RT_LEN] =
{
    {30, 255, 255},  //0
    {36, 230, 255},  //1
    {42, 212, 255},  //2
    {50, 180, 255},
    {95, 130, 255},
    {130, 150, 255},
    {160, 200, 255}, //6
    {160, 200, 255}, //7
    {190, 220, 255},
    {200, 255, 255},  //9
    {210, 255, 255},  //10
    {200, 240, 255},
    {200, 220, 255},

    {210, 190, 255},  //13
    {210, 190, 255},
    {226, 150, 255},
    {240, 135, 255},
    {270, 150, 255},
    {300, 150, 255},
    {330, 150, 255},
    {353, 135, 255},

    {353, 170, 255}, //21
    {353, 170, 255}, //22
    {14, 188, 255},
    {14, 205, 255}, //24
    {14, 205, 255}, //25
    {25, 220, 255},
};
static int ci;
static int stay_cnt = 0;
void  output_next_colour(int  *ph, int *ps)
{
    int h, s, cnt_limit;

    h = music_colour_rt[ci].h;
    s = music_colour_rt[ci].s;

    if(ci == 0 || ci == 1 || ci == 2 || ci == 6 || ci == 7 || ci == 9|| ci == 10 ||
       ci == 13 || ci == 21 || ci == 22 || ci == 24 || ci == 25 || ci == 18
      ){
        cnt_limit = 62;  // 60 * 14 * 12
    } else if(ci == 13 || ci == 14 || ci == 15 || ci == 16 || ci == 17 || ci == 19 || ci == 20) {
        cnt_limit = 12;  // 10 * 14 * 8
    } else {
        cnt_limit = 24; // 20 * 14 * 7
    }

    stay_cnt++;
    if(stay_cnt >= cnt_limit) { 
        stay_cnt = 0;
        ci++;
    }
    
    if(ci == RT_LEN) { ci = 0;}

    *ph = h;
    *ps = s;
}

/****************************************************************************
 * 律动灯效 1
 ****************************************************************************/
int draw_rms(double rms, RGB_MODE_INFO * info, GRAD_CHANGE_MSG *numv_msg);
int dyn_01_run(RGB_MODE_INFO * info)
{
    double energy = 0;
    GRAD_CHANGE_MSG numv_msg;
    memset(&numv_msg, 0, sizeof(GRAD_CHANGE_MSG));

    get_audio_energy(&energy);
    draw_rms(energy, info, &numv_msg);
    send_grad_change(numv_msg);

    return 0;
}

/****************************************************************************
 * 根据分贝计算要亮的灯的个数，抄xm的算法
 ****************************************************************************/
double rms_before = 0;

DB_MAP  db_map_arr[] =
{
    {2, 1, 150},
    {3, 1, 150},
    {4, 1, 150},
    {5, 2, 80},
    {6, 2, 90},
    {7, 2, 120},

    {8, 2, 150},
    {9, 2, 180},
    {10, 2, 210},
    {13, 2, 240},
    {14, 3, 80},
    {15, 3, 75},
    {16, 3, 90},
    {17, 3, 120},
    {18, 3, 150},
    {19, 3, 180},
    {20, 3, 210},
    {21, 3, 240},
    {22, 3, 255},
    {23, 4, 80},
    {24, 4, 90},

    {25, 4, 110},
    {26, 4, 130},
    {27, 4, 150},
    {28, 4, 180},
    {29, 4, 200},
    {30, 4, 220},
    {31, 4, 240},
    {32, 4, 255},
    {33, 4, 255},
    {34, 4, 255},
};

DB_MAP  db_map_arr_tts[] =
{
    {2, 1, 150},
    {3, 1, 150},
    {4, 1, 150},
    {5, 2, 80},
    {6, 2, 90},
    {7, 2, 120},

    {8, 2, 150},
    {9, 2, 180},
    {10, 2, 210},
    {13, 2, 240},
    {14, 3, 80},
    {15, 3, 75},
    {16, 3, 90},
    {17, 3, 120},
    {18, 3, 150},
    {19, 3, 180},
    {20, 3, 210},
    {21, 3, 240},
    {22, 3, 255},
    {23, 4, 80},
    {24, 4, 90},

    {25, 4, 110},
    {26, 4, 130},
    {27, 4, 150},
    {28, 4, 180},
    {29, 4, 200},
    {30, 4, 220},
    {31, 4, 240},
    {32, 4, 255},
    {33, 4, 255},
    {34, 4, 255},
};

/* 32 bit 音频能量映射*/
DB_MAP  db_map_arr_32bit[] =
{
    {100, 1, 5},   {101, 1, 15},  {102, 1, 25},  {103, 1, 35},  {104, 1, 45},  {105, 1, 55},  {106, 1, 65},  {107, 1, 75}, {108, 1, 85}, {109, 1, 95},

    {110, 1, 105}, {111, 1, 115}, {112, 1, 125}, {113, 1, 135}, {114, 1, 145}, {115, 1, 155}, {116, 1, 165}, {117, 1, 175},   {118, 1, 185},  {119, 1, 195},

    {120, 1, 205},  {121, 1, 215},  {122, 1, 225},  {123, 1, 235}, {124, 1, 245}, {125, 2, 5}, {126, 2, 15}, {127, 2, 25}, {128, 2, 35}, {129, 2, 45},

    {130, 2, 55}, {131, 2, 65},   {132, 2, 75},  {133, 2, 85},  {134, 2, 95},  {135, 2, 105},  {136, 2, 115},  {137, 2, 125},  {138, 2, 135},  {139, 2, 145},

    {140, 2, 155}, {141, 2, 165},   {142, 2, 175},  {143, 2, 185},  {144, 2, 195},  {145, 2, 205},  {146, 2, 215},  {147, 2, 225}, {148, 2, 235}, {149, 2, 245},


    {150, 3, 3},   {151, 3, 20},  {152, 3, 37},  {153, 3, 54},  {154, 3, 71},  {155, 3, 88},  {156, 3, 105},  {157, 3, 122}, {158, 3, 139}, {159, 3, 156},

    {160, 3, 173},   {161, 3, 190},  {162, 3, 207},  {163, 3, 224},  {164, 3, 241},  {165, 3, 255},  {166, 4, 3},  {167, 4, 20}, {168, 4, 37}, {169, 4, 54},

    {170, 4, 71},   {171, 4, 88},  {172, 4, 105},  {173, 4, 122},  {174, 4, 139},  {175, 4, 156},  {176, 4, 173},  {177, 4, 190}, {178, 4, 207}, {179, 4, 255},

};

DB_MAP  db_record_map_arr[] =
{
    {0.3, 1, 120},
    {0.4, 1, 130},
    {0.5, 1, 150},
    {0.6, 1, 200},

    {0.7, 2, 50},
    {0.8, 2, 100},
    {0.9, 2, 150},
    {1.0, 2, 200},

    {1.1, 3, 50},
    {1.2, 3, 100},
    {1.3, 3, 150},
    {1.4, 3, 200},

    {1.5, 4, 50},
    {1.6, 4, 100},
    {1.7, 4, 150},
    {1.8, 4, 255},
};
typedef enum {
    M_MUSIC,
    M_TTS,
    M_VOICE,
    M_MAX,
} M_MODE;

typedef struct bt_mult
{
    int volume;
    double mult;
} BT_MULT;

#define  BT_MULT_POWER    (0.5)
//12-->10
BT_MULT bt_mult[26] = 
{
    {0, 1},
    {1, 40},
    {2, 38.3},
    {3, 10.83},
    {4, 7.91},
    {5, 5.58},
    {6, 4.25},
    {7, 3.91},
    {8, 2.74},
    {9, 2.68},
    {10, 1.95},
    {11, 1.94},
    {12, 1.94},
    {13, 1.93},
    {14, 1.94},
    {15, 1.93},
    {16, 1.91},
    {17, 1.91},
    {18, 1.91},
    {19, 1.91},
    {20, 1.91},
    {21, 1.91},
    {22, 1.95},
    {23, 1.95},
    {24, 1.95},
    {25, 1.95},
};

#define WINDOW_SIZE       (80)          // 500ms数据窗口
#define BASELINE          (8.0)         // 基准中心值
#define RESPONSE_FACTOR   (4.5)
#define REGRESS_RATE      (0.8)
#define MIN_ENERGY        (2.0)
#define MAX_ENERGY        (29.0)        // 修改后的最大值
#define STABLE_THRESHOLD  (5)

// 滑动窗口结构体
typedef struct {
    double buffer[WINDOW_SIZE];
    int index;
    double sum;
    int count;
} EnergyWindow;

static EnergyWindow energy_window = {0};

// 辅助函数
static double get_window_avg(void) {
    return (energy_window.count > 0) ? 
           energy_window.sum / energy_window.count : 
           BASELINE;
}

static void update_window(double new_val) {
    if(energy_window.count < WINDOW_SIZE) {
        energy_window.sum += new_val;
        energy_window.buffer[energy_window.index++] = new_val;
        energy_window.count++;
    } else {
        energy_window.sum += new_val - energy_window.buffer[energy_window.index];
        energy_window.buffer[energy_window.index] = new_val;
        energy_window.index = (energy_window.index + 1) % WINDOW_SIZE;
    }
}

static double clamp(double value, double min, double max) {
    return fmax(fmin(value, max), min);
}

// 主处理函数
double map_rms(double input) {
    static double output = BASELINE;
    static int stable_counter = 0;    
    static int wave_phase = 0;       
    
    update_window(input);
    const double window_avg = get_window_avg();
    const double deviation = fabs(input - window_avg);
    const double relative_deviation = deviation / window_avg;

    /* 动态响应阶段:
       当检测到输入值与窗口平均值的偏差超过10%时，认为有显著变化。这里用RESPONSE_FACTOR来控制响应幅度。比如输入突然增大，输出会快速增加
     */
    if(relative_deviation >= 0.1) {
        const double delta = (input > window_avg) ? 
                           RESPONSE_FACTOR * relative_deviation :     //响应方向计算
                           -RESPONSE_FACTOR * relative_deviation;
        output = clamp(output + delta, MIN_ENERGY, MAX_ENERGY);
        stable_counter = 0;
        wave_phase = 0;
    }
    /* 
     基准回归阶段
     当偏差较小时，系统会逐渐回归到基准值。REGRESS_RATE控制回归速度。如果输出值高于基准，则每次减少REGRESS_RATE，直到接近基准时直接设为基准值
     */
    else {
        const double delta = (output - BASELINE) * REGRESS_RATE;  //回归
        output = clamp(output - delta, MIN_ENERGY, MAX_ENERGY);
        //波动
        if(fabs(output - BASELINE) < 0.4) {
            if(++stable_counter > STABLE_THRESHOLD) {
                const double radians = (++wave_phase % 3) * M_PI / 1.5;
                output = BASELINE + sin(radians);
                output = round(output);
                output = clamp(output, BASELINE-1, BASELINE+1);
            }
        } else {
            stable_counter = 0;
        }
    }

    return output;
}
#if 0
// 更新后的可视化工具
void print_analysis(double input, double output) {
    static int counter = 0;
    const int bar_length = 30;  // 缩短刻度条长度适应更大范围
    
    printf("[%03d] Input: %5.1f → Output: %5.1f ", counter++, input, output);
    
    // 动态刻度条（2-29范围）
    const int pos = (int)((output - MIN_ENERGY) / (MAX_ENERGY - MIN_ENERGY) * bar_length);
    for(int i=0; i<bar_length; i++) {
        putchar(i == pos ? '#' : 
               (i == (int)((BASELINE-MIN_ENERGY)/(MAX_ENERGY-MIN_ENERGY)*bar_length) ? '|' : ' '));
    }
    printf(" [%.0f-%.0f]\n", MIN_ENERGY, MAX_ENERGY);
}

int main() {
    srand(time(NULL));
    
    // 压力测试：验证能达到最大值29
    printf("======== 最大值验证测试 ========\n");
    for(int i=0; i<20; i++) {
        double out = draw_rms(100.0); // 持续输入极大值
        print_analysis(100.0, out);
    }
    
    // 回归测试：验证能恢复波动
    printf("\n======== 基准恢复测试 ========\n");
    for(int i=0; i<20; i++) {
        double out = draw_rms(BASELINE);
        print_analysis(BASELINE, out);
    }
    
    return 0;
}
#endif
int draw_rms(double rms, RGB_MODE_INFO * info, GRAD_CHANGE_MSG *numv_msg)     //按每帧平均RMS显示
{
    ArrayPtr pA = NULL;
    // int xun;

    if(_G_client_info.music_recv_runing || _G_client_info.miplay_recv_runing)
    {
        #ifdef RAW_DATA_32BIT
            pA = db_map_arr_32bit;
        #else
            if(!_G_info.tts_rhy) {
                if(btPlay)
                {
                    #if 0
                    for(xun = 0; xun < (sizeof(bt_mult) / sizeof(BT_MULT)); xun++)
                    {
                        if(_G_btVolume == bt_mult[xun].volume)
                        {
                            rms = (rms * bt_mult[xun].mult * BT_MULT_POWER);  //乘以系数
                            break;
                        }
                    }
                    #endif
                    rms = map_rms(rms);
                }

                pA = db_map_arr;
            } else {
                pA = db_map_arr_tts;
            }
        #endif
    } else if(_G_client_info.voice_recv_runing)
    {
        pA = db_record_map_arr;
    }

    if(pA != NULL) {
        numv_msg->pMap = pA;
    } else {
        numv_msg->pMap = NULL;
    }
    numv_msg->rms = rms;

    return 0;
}
/****************************************************************************
 * 律动灯效 1
 ****************************************************************************/
int dyn_01_dyn(RGB_MODE_INFO * info)
{
#if 0
   int energy;
   //决定要亮的灯的个数  和 颜色

   //计算display_num
   int all_number = LED_NUM;
   int max,min,cur,display,number,tmp;
   display = info->mult_int[0];
   number = all_number;

   get_audio_energy(&energy);
   cur = energy;
   max_energy(cur,&max, &min, 2);  // 20 * 43 = 860ms

   tmp = 0;

   if(max && cur){
       tmp = (number * cur) / max;
   }
   if(tmp > 8)
   { tmp = 8;}
   if(cur>((max*6)/10)){
       tmp = number;
   }else if(cur>(max/3)){
       tmp = ((number+2)/2);
   }else if(cur>(max/4)){
       tmp = 1;
   }else{
       tmp = 0;
   }

   if(display<tmp){
       if(tmp-display>3){
           display=tmp;
       }else if(tmp-display>2){
           display++;
       }else if(display){
           display--;
       }
   }else if(display){
       display--;
   }

   if(display < 0) {display = 0;}
   if(display > 8) {display = 8;}

   info->mult_int[0] = display;
/************************************JL************************************ */
   double energy;
   get_audio_energy(&energy);
   draw_rms(energy, info);
#endif
    return 0;
}
/****************************************************************************
 * 中间向两边消散
 ****************************************************************************/
static int start_index = -1;
int voip_dial_init(RGB_MODE_INFO * info)
{
    start_index = -1;
    info->time_m = 300;
    return 0;
}
int voip_dial_run(RGB_MODE_INFO * info)
{
    int  i = 0;
    int  left = 3;
    int  right = 4;
    memset(info->hsv, 0, sizeof(info->hsv));

    if(start_index < 0) //all oon
    {
        for(i = 0; i < LED_NUM; i++)
        {
            info->hsv[i].h = 0;
            info->hsv[i].s = 0;
            info->hsv[i].v = 50;
        }
    } else  //fade
    {
        for(i = 0; i < LED_NUM; i++)
        {
            if(i < (left - start_index) || i > (right + start_index))
            {
                info->hsv[i].h = 0;
                info->hsv[i].s = 0;
                info->hsv[i].v = 50;
            }
        }

    }
    start_index++;
    if(start_index == 4)
    {
        start_index = -1;  //loop
    }

    one_by_one_fill_info_buff(info, LED_NUM);
    send_rgbbuf_to_spi(info);

    return 0;
}

/****************************************************************************
 * 灯光运行函数表
 ****************************************************************************/
Function rgb_mode_run_table[USER_RGB_MODE_MAX][MODE_MAX]={
    /*
     * 特殊模式，指不在用户模式切换列表里面但是需要显示的模式，特殊模式执行完之后会返回用户模式
     */
    [USER_RGB_MODE_BLUE_BREATH]         = {blue_breath_init, blue_breath_run},
    [USER_RGB_MODE_GREEN_STAY]          = {green_stay_init, green_stay_run},            //音量
    [USER_RGB_MODE_BLE_CONN_OK]         = {ble_conn_ok_init, ble_conn_ok_run},          //蓝牙连接成功，持续1s 后熄灭
    [USER_RGB_MODE_NO_SOUND_DETECT]     = {no_sound_detect_init, no_sound_detect_run},  //未识别到指令声音，超时返回上一的灯效
    [USER_RGB_MODE_BT_DISCONNECTED]     = {bt_disconnected_init, bt_disconnected_run},  //bt disconnected
    /*
     * 用户模式
     */
    [USER_RGB_MODE_WHILT_BREATH]   = {whilt_breath_init, whilt_breath_run},              //白色呼吸，预加载阶段
    [USER_RGB_MODE_DYN_01]         = {dyn_01_init, dyn_01_run, dyn_01_dyn},              //播放律动
    [USER_RGB_MODE_DYN_02]         = {dyn_pick_init, dyn_01_run, dyn_01_dyn},            //拾音律动
    [USER_RGB_MODE_WALK_LE_AND_RI] = {walk_left_and_right_init, walk_left_and_right_run},//左右游走效果
    [USER_RGB_MODE_SIDE_TO_CENTER] = {side_to_center_init, side_to_center_run},          //两边向中间聚拢效果
    [USER_RGB_MODE_BOOT_SUCCESS]   = {boot_success_init1, boot_success_run1},              //开机成功效果
    [USER_RGB_MODE_INIT_CONFIG]    = {init_config_init, init_config_run},                //初始化/配置，wifi连接过程，单向从左向右游走，橙色
    [USER_RGB_MODE_OTA_UPGRADE]    = {ota_upgrade_init, ota_upgrade_run},                //监测到新版本，含安装包下载，自动升级,blue
    [USER_RGB_MODE_VOIP_DIAL]      = {voip_dial_init, voip_dial_run},                    //VOIP通话，拨号中/来电响铃时
    [USER_RGB_MODE_NET_OFF]        = {orange_breath_init, orange_breath_run},            //音箱断网，橙色呼吸
    [USER_RGB_MODE_COMB_PLAY]      = {comb_play_init, comb_play_run},                    //组合播放，呼吸单次
    [USER_RGB_MODE_PLAING_LOAD_SOURCE] = {playing_load_source_init, playing_load_source_run}, //歌曲切换时加载资源。2.5s 呼吸一次
    [USER_RGB_MODE_VOIP_BUSY]      = {voip_busy_init, voip_busy_run},
    [USER_RGB_MODE_SYSTEM_FAULT]   = {system_fault_init, system_fault_run},
    [USER_RGB_MODE_ALARM_ACTIVE]   = {alarm_active_init, alarm_active_run},
    [USER_RGB_MODE_MIC_MUTE]       = {mic_mute_init, mic_mute_run},
    [USER_RGB_MODE_OTA_UPGRADE_1]  = {upgrade1_init, upgrade1_run},
    [USER_RGB_MODE_DYN_TTS]        = {dyn_tts_init, dyn_01_run, dyn_01_dyn},
    [USER_RGB_MODE_OPEN_BLE_DISCOVER] = {open_ble_discv_init,open_ble_discv_run},
    [USER_RGB_MODE_DYN_03]         = {dyn_01_init, dyn_01_run, dyn_01_dyn},
    [USER_RGB_MODE_MIAOBO_DYN]     = {dyn_01_init, dyn_01_run, dyn_01_dyn},
    [USER_RGB_SET_PRE_CONFIG_MODE] = {set_pre_config_mode_init, set_pre_config_mode_run},
    [USER_RGB_MODE_WALK_LE_AND_RI_NO_REST] = {walk_left_and_right_init_no_rest, walk_left_and_right_run},
    [USER_RGB_MODE_MIPLAY]         = {dyn_01_init, dyn_01_run, dyn_01_dyn},

    [USER_RGB_MODE_OFF]            = {off_stay_init, off_stay_run},                      //关灯
};

/****************************************************************************
 * 更新scan 时间
 ****************************************************************************/
void rgb_scan_time_update(RGB_MODE_INFO *info)
{
    if(!info){
        return;
    }

    //连续 run
    if(info->id_m && info->time_m && info->timer_cm!=info->time_m){

        info->timer_cm = info->time_m;

        struct itimerspec      new_ts_m;
        new_ts_m.it_value.tv_sec        = 0;
        new_ts_m.it_value.tv_nsec       = info->timer_cm * 1000 * 1000;
        new_ts_m.it_interval.tv_sec     = 0;
        new_ts_m.it_interval.tv_nsec    = info->timer_cm * 1000 * 1000;

        timer_settime(info->id_m, 0, &new_ts_m, NULL);;     //重置定时器
    }
    //更新 hsv dyn
    if(info->id_d && info->time_d && info->timer_cd!=info->time_d){

        info->timer_cd = info->time_d;

        struct itimerspec      new_ts_d;
        new_ts_d.it_value.tv_sec        = 0;
        new_ts_d.it_value.tv_nsec       = info->timer_cd * 1000 * 1000;
        new_ts_d.it_interval.tv_sec     = 0;
        new_ts_d.it_interval.tv_nsec    = info->timer_cd * 1000 * 1000;

        timer_settime(info->id_d, 0, &new_ts_d, NULL);;     //重置定时器
    }
}
/****************************************************************************
 * run 过程执行
 ****************************************************************************/
int rgb_mode_run(RGB_MODE_INFO *info,unsigned char mode,MODE_FUN fun){
    // log_debug("----%d",info->mode_group);

    if(!info||
        !(*info->run_table)||
        mode<=USER_RGB_MODE_MIX ||
        mode>=USER_RGB_MODE_MAX ||
        fun>=MODE_MAX){
        return -1;
    }

    if((*info->run_table)[mode][fun]){
        if(MODE_INIT==fun){
            info->Special_mode_time=0;
            memset(info->mult_int,0,sizeof(info->mult_int));
            memset(info->mult_u32,0,sizeof(info->mult_u32));
            memset(info->mult_priv_data,0,sizeof(info->mult_priv_data));
            rgb_scan_time_update(info);
        }
        return (*info->run_table)[mode][fun](info);
    }

    return -1;
}
/****************************************************************************
 * normal run
 ****************************************************************************/
static void scan_normal(RGB_MODE_INFO *info){
    rgb_mode_run(info,info->Normal_mode,MODE_RUN);
}
/****************************************************************************
 * special run
 ****************************************************************************/
 int set_rgb_mode_auto(int want, RGB_MODE_INFO *info, int mode);
static bool scan_special(RGB_MODE_INFO *info){
    bool ret=false;
    struct timeval current_time;
    unsigned long long   cur_time_ms;

    gettimeofday(&current_time, NULL);
    cur_time_ms = (current_time.tv_sec) * 1000 + (current_time.tv_usec) / 1000;  //转化为ms

    pthread_mutex_lock(&info->info_para_mutex);
    if(info->Special_mode_time <= cur_time_ms)              //特殊模式时间已结束
    {
        if(USER_RGB_MODE_MIX != info->Special_mode)
        {
            info->Special_mode = info->Special_keep;
            pthread_mutex_unlock(&info->info_para_mutex);
            led_effect_shut_gateway((void*)USER_RGB_MODE_DUMMY_RESUME);
            return false;
        } else 
        {
            pthread_mutex_unlock(&info->info_para_mutex);
            return false;
        }

    }

    ret = !rgb_mode_run(info,info->Special_mode,MODE_RUN);
    pthread_mutex_unlock(&info->info_para_mutex);

    return ret;
}
/****************************************************************************
 * scan 函数不停地执行 run函数，run 函数会根据 dyn的结果，更新色值
 ****************************************************************************/
static void rgb_scan(union sigval v)
{
    if(!scan_special(&_G_info)){
        scan_normal(&_G_info);
    }
}
/****************************************************************************
 * dyn 函数不停地获取频点或者能量，以给 run 函数提供色值来源
 ****************************************************************************/
static void rgb_dyn(union sigval v){

    RGB_MODE_INFO  *info = &_G_info;
    if(USER_RGB_MODE_MIX==info->Special_mode){
        rgb_mode_run(info,info->Normal_table[info->index],MODE_DYN);
    }else{
        rgb_mode_run(info,info->Special_mode,MODE_DYN);
    }
}
/****************************************************************************
 * 设置 rgb 模式
 ****************************************************************************/
int set_rgb_mode(RGB_MODE_INFO *info,unsigned char mode,MODE_CLASS mode_class)
{
    pthread_mutex_lock(&info->info_para_mutex);
    if(!info || !mode || rgb_mode_run(info,mode,MODE_INIT)){
        return -1;
    }
    //即使是在特殊模式也能进行切换，终结特殊模式
    if(MODE_NOR==mode_class){
        for (int i = 0; i < sizeof(info->Normal_table); i++){
            if (mode == info->Normal_table[i]){
                info->index = i;
                break;
            }
        }
        info->Normal_mode=mode;
        info->Special_keep=USER_RGB_MODE_MIX;
        info->Special_mode_time=0;
    }else if(MODE_SPE==mode_class){
        info->Special_mode=mode;
    }else if(MODE_KEEP==mode_class){
        info->Special_keep=info->Special_mode=mode;
    }
    pthread_mutex_unlock(&info->info_para_mutex);

    rgb_scan_time_update(info);

//    save_rgb_mode(info);
    return 0;
}

#define KVDB_KEY_IS_BT_PLAYING          "persist.isbt.playing"
/****************************************************************************
 * 设置 rgb 模式
 ****************************************************************************/
int set_rgb_mode_auto(int want, RGB_MODE_INFO *info, int mode)
{
    if(mode == USER_RGB_MODE_DYN_03){
        property_set_int32(KVDB_KEY_IS_BT_PLAYING, 1);
        btPlay = 1;
    } else {
        property_set_int32(KVDB_KEY_IS_BT_PLAYING, 0);
        btPlay = 0;
    }

    if(want != USER_RGB_MODE_MIC_MUTE_OFF)
    {
        if( 
            #if 0
            (info->cur_ef == USER_RGB_MODE_MIC_MUTE && mode == USER_RGB_MODE_PLAING_LOAD_SOURCE) ||
            (info->cur_ef == USER_RGB_MODE_MIC_MUTE && mode == USER_RGB_MODE_DYN_01) ||
            (info->cur_ef == USER_RGB_MODE_MIC_MUTE && mode == USER_RGB_MODE_DYN_03) ||
            (info->cur_ef == USER_RGB_MODE_MIC_MUTE && mode == USER_RGB_MODE_MIAOBO_DYN)
            #endif
            (
                (mode == USER_RGB_MODE_PLAING_LOAD_SOURCE) ||
                (mode == USER_RGB_MODE_DYN_01)             ||
                (mode == USER_RGB_MODE_DYN_03)             ||
                (mode == USER_RGB_MODE_MIAOBO_DYN)         ||
                (mode == USER_RGB_MODE_MIPLAY)
            ) && (is_in_stack(_G_info.led_stack, USER_RGB_MODE_MIC_MUTE))
        )
        {
            syslog(LOG_INFO, "mode  = %d keep_mute_rgb_mode\r\n", mode);
            mode = USER_RGB_MODE_MIC_MUTE;
        }
    }

    if(mode != USER_RGB_MODE_GREEN_STAY)  //volume set not record
    {
        info->cur_ef = mode;
    }
    syslog(LOG_INFO, "rgb_mode = %d\r\n", mode);

    MODE_CLASS mode_class;
    if(mode>USER_RGB_MODE_MIX && mode<USER_RGB_MODE_SPE_MAX){
        mode_class=MODE_SPE;
    }else if(mode>USER_RGB_MODE_SPE_MAX && mode<USER_RGB_MODE_MAX){
        mode_class=MODE_NOR;
    }else{
        return -1;
    }
    return set_rgb_mode(info,mode,mode_class);
}
int  effect_convert(int effect)
{
    int convert = 0;

    if(effect < __LED_LIGHT_MAX) 
    {
        switch(effect)
        {
            case LED_LIGHT_SHUTDOWN:
                convert = USER_RGB_MODE_OFF;
                break;

            case LED_LIGHT_01_WAKEUP:
                convert = USER_RGB_MODE_SIDE_TO_CENTER;
                break;

            case LED_LIGHT_02_LOAD:
                convert = USER_RGB_MODE_PLAING_LOAD_SOURCE;
                break;

            case LED_LIGHT_03_TTS:
                convert = USER_RGB_MODE_DYN_TTS;
                break;

            case LED_LIGHT_04_STARTUP:
                break;

            case LED_LIGHT_05_ALARM:
                convert = USER_RGB_MODE_ALARM_ACTIVE;
                break;

            case LED_LIGHT_06_OFFLINE:
                convert = USER_RGB_MODE_NET_OFF;
                break;

            case LED_LIGHT_07_MIC:
                convert = USER_RGB_MODE_MIC_MUTE;
                break;

            case LED_LIGHT_08_ERROR:
                break;

            case LED_LIGHT_09_OTA:
                convert = USER_RGB_MODE_OTA_UPGRADE_1;
                break;

            case LED_LIGHT_10_CONFIG:
                convert = USER_RGB_MODE_INIT_CONFIG;
                break;

            case LED_LIGHT_11_WAKEUPEND:
                break;

            case LED_LIGHT_12_VOLUME:
                convert = USER_RGB_MODE_GREEN_STAY;
                break;

            case LED_LIGHT_13_VOIP:
                break;

            case LED_LIGHT_14_PLAYER:
                convert = USER_RGB_MODE_DYN_01;
                break;

            case LED_LIGHT_15_BT_DISC_ENABLE:
                convert = USER_RGB_MODE_OPEN_BLE_DISCOVER;
                break;

            case LED_LIGHT_16_BT_CONNECTED:
                convert = USER_RGB_MODE_BLE_CONN_OK;
                break;

            case LED_LIGHT_23_BT_DISCONNECTED:
                convert = USER_RGB_MODE_BT_DISCONNECTED;
                break;

            case LED_LIGHT_17_VOICE_PICKUP:
                convert = USER_RGB_MODE_DYN_02;
                break;

            case LED_LIGHT_18_SEMANTIC_UNDERSTANDING:        //语义理解中
                convert = USER_RGB_MODE_WALK_LE_AND_RI;
                break;

            case LED_LIGHT_19_VOIP_DIAL_CALL:               //VOIP 通话/拨号
                convert = USER_RGB_MODE_VOIP_DIAL;
                break;

            case LED_LIGHT_20_VOIP_ONTHE_LINE:              //Voip 通话中
                convert = USER_RGB_MODE_VOIP_BUSY;
                break;

            case LED_LIGHT_21_SYSTEM_FAULT:
                convert = USER_RGB_MODE_SYSTEM_FAULT;
                break;

            case LED_LIGHT_22_BT_MUSIC:
                convert = USER_RGB_MODE_DYN_03;
                break;

            case LED_LIGHT_24_NO_TTS_END:
                convert = USER_RGB_MODE_NO_TTS_END_OFF;
                break;

            case LED_LIGHT_25_MIPLAY_SHOW:
                convert = USER_RGB_MODE_MIPLAY;
                break;

            default:
                break;
        }
    } else {     //无需转换
        return  convert = effect;
    }

    return convert;
}
Stack* creat_stack(void)
{
    Stack* line = (Stack*)malloc(sizeof(Stack));
    if(line == NULL)
    {
        syslog(LOG_INFO, "creat_stack ERROR\r\n");
        return (NULL);
    }
    line->top = NULL;
    line->len = 0;
    return line;
}
Node* creat_node(int data)
{
    Node* node = (Node*)malloc(sizeof(Node));
    if(NULL == node)
    {
        syslog(LOG_INFO, "creat_node ERROR\r\n");
        return (NULL);
    }
    node->data = data;
    node->next = NULL;
    return node;
}

bool empty_stack(Stack* sta)
{
    pthread_mutex_lock(&stack_mutex);
    bool empty = !sta->len;
    pthread_mutex_unlock(&stack_mutex);
    return empty;
}

void push_stack(Stack* sta, int data)
{
    pthread_mutex_lock(&stack_mutex);
    Node* node = creat_node(data);
    if(NULL == node)
    {
        syslog(LOG_INFO, "push_stack ERROR\r\n");
        pthread_mutex_unlock(&stack_mutex);
        return;
    }
    if (!sta->len) {
        sta->top = node;
    } else {
        node->next = sta->top;
        sta->top = node;
    }
    sta->len++;
    pthread_mutex_unlock(&stack_mutex);
}

int top_stack(Stack* sta)
{
    pthread_mutex_lock(&stack_mutex);
    if (!sta->len) 
    {
        pthread_mutex_unlock(&stack_mutex);
        return USER_RGB_MODE_MAX;
    }
    int data = USER_RGB_MODE_MAX;
    if(sta->top != NULL) {
        data = sta->top->data;
    }
    pthread_mutex_unlock(&stack_mutex);
    return data;
}

bool pop_stack(Stack* sta)
{
    pthread_mutex_lock(&stack_mutex);
    if (!sta->len)
    {
        pthread_mutex_unlock(&stack_mutex);
        return false;
    }
    Node* node = sta->top;
    sta->top = node->next;
    syslog(LOG_INFO, "pop_stack:%d\r\n",node->data);
    free(node);
    sta->len--;
    pthread_mutex_unlock(&stack_mutex);
    return true;
}

void destory_stack(Stack* sta)
{
    while (pop_stack(sta))
    {
        ;
    }
    free(sta);
}

void print_stack(Stack* sta)
{
    pthread_mutex_lock(&stack_mutex);
    if (!sta->len)
    {
        syslog(LOG_INFO,"print:STACK EMPTY top = %p\r\n", sta->top);
        pthread_mutex_unlock(&stack_mutex);
        return;
    }

    Node* pTop = sta->top;
    while (pTop != NULL)
    {
        syslog(LOG_INFO,"RGB_STACK: %d\r\n", pTop->data);
        pTop = pTop->next;
    }
    pthread_mutex_unlock(&stack_mutex);
}

bool  is_in_stack(Stack* sta, int data)
{
    pthread_mutex_lock(&stack_mutex);
    if (!sta->len)
    {
        syslog(LOG_INFO,"print:STACK EMPTY top = %p\r\n", sta->top);
        pthread_mutex_unlock(&stack_mutex);
        return false;
    }

    Node* pTop = sta->top;
    while (pTop != NULL)
    {
        if(pTop->data == data) {
            pthread_mutex_unlock(&stack_mutex);
            return true;
        }
        pTop = pTop->next;
    }
    pthread_mutex_unlock(&stack_mutex);
    return  false;
}

void delete_specified_node(Stack* sta, int data)
{
    pthread_mutex_lock(&stack_mutex);
    Node* pTop = sta->top;
    Node* pre  = NULL;
    Node* current = pTop;
    pre = current;

    if (!sta->len)
    {
        syslog(LOG_INFO, "STACK EMPTY\r\n");
        pthread_mutex_unlock(&stack_mutex);
        return;
    }

    while(current != NULL)
    {
        if(current->data == data)
        {
            if(pre != current)
            {
                pre->next = current->next;
                free(current);
                sta->len--;
                current = pre->next;
            } else
            {
                sta->top = current->next;
                pre = pre->next;
                free(current);
                current = pre;
                sta->len--;
            }
        } else
        {
            pre = current;
            current = pre->next;
        }
    }
    pthread_mutex_unlock(&stack_mutex);
}
int  get_current_effect(void)
{
    return _G_info.cur_ef;
}
/****************************************************************************
 * 语义理解 超时定时器
 ****************************************************************************/
timer_t  semantic_understand_id = 0;
static void semantic_understanding_timeout_cb(union sigval v)
{
    delete_specified_node(_G_info.led_stack, USER_RGB_MODE_WALK_LE_AND_RI);
    if(get_current_effect() == USER_RGB_MODE_WALK_LE_AND_RI || get_current_effect() == USER_RGB_MODE_WALK_LE_AND_RI_NO_REST)
    {
        // set_rgb_mode_auto(USER_RGB_MODE_OFF, &_G_info, USER_RGB_MODE_OFF);
        led_effect_shut_gateway((void*)USER_RGB_MODE_DUMMY_RESUME);
    }
}
void  semantic_understanding_set(void)
{
    struct sigevent        evp_m;
    struct itimerspec      ts_m;

    memset(&evp_m, 0, sizeof(evp_m));
    evp_m.sigev_value.sival_ptr = &semantic_understand_id;
    evp_m.sigev_notify          = SIGEV_THREAD;
    evp_m.sigev_notify_function = semantic_understanding_timeout_cb;
    evp_m.sigev_value.sival_int = 1;

    if (semantic_understand_id == 0)
    {
        //创建定时器
        timer_create(CLOCK_REALTIME, &evp_m, &semantic_understand_id);
    }

    ts_m.it_value.tv_sec        = 10;
    ts_m.it_value.tv_nsec       = 0;
    ts_m.it_interval.tv_sec = 0;
    ts_m.it_interval.tv_nsec = 0;

    timer_settime(semantic_understand_id, 0, &ts_m, NULL);
}
/****************************************************************************
 * Outgoing distribute interface
 ****************************************************************************/
void led_effect_distribute_gateway(void* eff)
{
    pthread_mutex_lock(&_G_info.exec_mutex);
    mqd_t mqfd;
    EXEC_MSG msg;

    mqfd = mq_open(EXEC_MSG_QUEUE, O_WRONLY | O_NONBLOCK);
    if (mqfd == (mqd_t)-1)
    {
        pthread_mutex_unlock(&_G_info.exec_mutex);
        return;
    }
    msg.type = TYPE_DISTRIBUTE;
    msg.effect = (int)eff;
    mq_send(mqfd, (const char*)&msg, EXEC_MSG_LEN, 42);

    mq_close(mqfd);
    pthread_mutex_unlock(&_G_info.exec_mutex);
}
/****************************************************************************
 * Outgoing shut interface
 ****************************************************************************/
void led_effect_shut_gateway(void *eff)
{
    pthread_mutex_lock(&_G_info.exec_mutex);
    mqd_t mqfd;
    EXEC_MSG msg;

    mqfd = mq_open(EXEC_MSG_QUEUE, O_WRONLY | O_NONBLOCK);
    if (mqfd == (mqd_t)-1)
    {
        pthread_mutex_unlock(&_G_info.exec_mutex);
        return;
    }
    msg.type = TYPE_SHUT;
    msg.effect = (int)eff;
    mq_send(mqfd, (const char*)&msg, EXEC_MSG_LEN, 42);

    mq_close(mqfd);
    pthread_mutex_unlock(&_G_info.exec_mutex);
}
//persist.aivs.continuous_dialog: false
static bool get_continuous_diag_stat(void)
{
    //全双工上了再开启
     return (property_get_bool("persist.aivs.continuous_dialog", false));
    //return false;
}
static int get_is_play(void)
{
    pv_exist = 0;
    if(_G_info.pl_enable)   //播放灯光关闭时，拾音可以抢占播放，只要 TTS 没在播
    {
        player_session_t * player_mp = music_player_get();
        if(player_mp != NULL && player_mp->is_active && player_mp->is_active(player_mp->priv))
        {
            if(check_is_background()) {
                pv_exist = 0;  //后台可以抢占
            } else {
                pv_exist = 1;
            }
        }

        player_session_t * player_bt = bt_player_get();
        if(player_bt != NULL && player_bt->is_active && player_bt->is_active(player_bt->priv))
        {
            pv_exist = 1;
        }
    }

    if(is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_TTS))
    {
        pv_exist = 1;
    }

    syslog(LOG_INFO, "pv_exist  = %d\r\n", pv_exist);
    return  pv_exist;
}
static int get_condition(void)
{
    if(get_continuous_diag_stat()  == true && _G_info.pl_enable == false && _G_info.listruc_set  == true) {    //连续对话 && 关闭播放灯光 && 仍在拾音
        syslog(LOG_INFO, "_G_info.listruc_set  = %d \r\n", _G_info.listruc_set);
        return true;
    } else {
        return false;
    }
}

/****************************************************************************
 * exec interface
 ****************************************************************************/
void distribute_gateway_exec(int effect)
{
#if  0
    if(_G_info.cur_ef == USER_RGB_MODE_DYN_01 || _G_info.cur_ef == USER_RGB_MODE_DYN_TTS || _G_info.cur_ef == USER_RGB_MODE_DYN_03
       || _G_info.cur_ef == USER_RGB_MODE_MIAOBO_DYN)
    {
        //正在播放，提前销毁律动过程，避免在 dismiss 时，因为栈里面有该灯效，恢复时出现 stop--》start 短时间出现的情况
        destory_play_data_process();
    }
#endif
    //开启时，恢复播放等效
    if(effect == USER_RGB_MODE_DYN_01_BY_PASS)
    {
        if( is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_01) )  //playing
        {
            _G_client_info.music_recv_runing = true;
            set_rgb_mode_auto(USER_RGB_MODE_DYN_01, &_G_info, USER_RGB_MODE_DYN_01);
        }

        return;
    }
    //关闭时，恢复拾音等效
    if(effect == USER_RGB_MODE_DYN_02_BY_PASS)
    {
        if(_G_info.listruc_set)
        {
            trigger_record_data_process();      //启动拾音律动
            set_rgb_mode_auto(USER_RGB_MODE_DYN_02, &_G_info, USER_RGB_MODE_DYN_02);
        }
        return;
    }

    if(effect == USER_RGB_MODE_BT_DISCONNECTED)
    {
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_DYN_03);
        if( //other play
            !(
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_01)  ||
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_TTS) ||
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_MIAOBO_DYN)
            )
            )
        {
            destory_play_data_process();
        }
    }

    if(effect == USER_RGB_MODE_PLAING_LOAD_SOURCE)
    {
        /* 
         * 解决上下一首，loadsource--》onmusic， loadsource 时栈中存在本来有 music， onmusic 会 stop --》 start
         */
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_DYN_01);
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_DYN_03);
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_MIAOBO_DYN);
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_DYN_TTS);

        destory_play_data_process();
    }

    if(effect == USER_RGB_MODE_DYN_01 || effect == USER_RGB_MODE_DYN_TTS || effect == USER_RGB_MODE_DYN_03 || effect == USER_RGB_MODE_MIAOBO_DYN)
    {
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_WALK_LE_AND_RI);
        if(effect == USER_RGB_MODE_DYN_TTS) {
            _G_info.tts_rhy  = true;
        } else {
            _G_info.tts_rhy  = false;
        }

        trigger_play_data_process();        //启动播放律动
    }

    if(USER_RGB_MODE_MIPLAY == effect)
    {
        _G_client_info.music_recv_runing = false;
        _G_client_info.voice_recv_runing = false;
        _G_client_info.miplay_recv_runing = true;
        _G_info.tts_rhy = false;
    }

    syslog(LOG_INFO, "RGB_PUSH\r\n");
    if(effect == USER_RGB_MODE_WALK_LE_AND_RI ||        //语义理解
       effect == USER_RGB_MODE_NET_OFF  ||              //断网
       effect == USER_RGB_MODE_MIC_MUTE ||              //禁麦
    //    effect == USER_RGB_MODE_ALARM_ACTIVE ||          //闹钟激活
    //    effect == USER_RGB_MODE_PLAING_LOAD_SOURCE ||    //歌曲加载资源
       effect == USER_RGB_MODE_INIT_CONFIG ||           //配网
       effect == USER_RGB_MODE_OTA_UPGRADE_1 ||         //OTA
       effect == USER_RGB_MODE_DYN_01 ||                //小爱音箱播放音乐
       effect == USER_RGB_MODE_MIAOBO_DYN ||            //妙播
       effect == USER_RGB_MODE_DYN_03 ||                //蓝牙
       effect == USER_RGB_MODE_DYN_TTS ||
    //    effect == USER_RGB_MODE_DYN_02 ||                //拾音律动
       effect == USER_RGB_MODE_SYSTEM_FAULT ||          //系统故障
       effect == USER_RGB_MODE_OPEN_BLE_DISCOVER ||     //打开蓝牙可发现
       effect == USER_RGB_MODE_MIPLAY
       )
       {
            // push stack
            if(effect != top_stack(_G_info.led_stack)) {
                push_stack(_G_info.led_stack, effect);
            }
       }
    print_stack(_G_info.led_stack);

    //断网时重置拾音乐标记
    if(effect == USER_RGB_MODE_NET_OFF)
    {
        pthread_mutex_lock(&exec_ctx.cmd_mutex);
        syslog(LOG_INFO, "NETOFF_PICK_VOICE_DISMISS\r\n");
        _G_info.listruc_set = false;
        pthread_mutex_unlock(&exec_ctx.cmd_mutex);
    }

    if(effect == USER_RGB_MODE_GREEN_STAY && get_current_effect() == USER_RGB_MODE_BLE_CONN_OK )
    {
        syslog(LOG_INFO, "VOLUME_SYNC_DROP_WHEN_BT_CON\r\n");
        return;
    }

    if(effect == USER_RGB_MODE_DYN_02)
    {
        if(get_continuous_diag_stat() == true) 
        {
            if(get_is_play() != 1)
            {
                trigger_record_data_process();      //启动拾音律动
            }
        } else   //非全双工
        {
            trigger_record_data_process();      //启动拾音律动
        }
    }

    if(effect == USER_RGB_MODE_WALK_LE_AND_RI)  //语义理解，启动超时定时器
    {
        semantic_understanding_set();
    }

    if(effect == USER_RGB_MODE_SIDE_TO_CENTER)  //断网时语音唤醒
    {
        //重复唤醒时出现 聆听灯效被 唤醒灯打断，之后有数据，无灯效的情况，这里在唤醒时重置 聆听状态标记
        pthread_mutex_lock(&exec_ctx.cmd_mutex);
        syslog(LOG_INFO, "WAKEUP_PICK_VOICE_DISMISS\r\n");
        _G_info.listruc_set = false;
        pthread_mutex_unlock(&exec_ctx.cmd_mutex);

        if(is_in_stack(_G_info.led_stack, USER_RGB_MODE_NET_OFF))
        {
            return;
        }
    }

    if(effect == USER_RGB_MODE_BLE_CONN_OK)
    {
        //删除可发现节点，防止误恢复
        syslog(LOG_INFO, "BLE_CONNECTED_DEL_DISCOVER_NODE && BTMUSICSHOW_NODE\r\n");
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_DYN_03);
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_OPEN_BLE_DISCOVER);
    }

    if(get_continuous_diag_stat() == true && pv_exist == 1 && effect == USER_RGB_MODE_DYN_02)
    {
        syslog(LOG_INFO, "DROP_PICK_VOICE_OPERATE keep %d\r\n", get_current_effect());
    } else 
    {
        if((effect == USER_RGB_MODE_DYN_01 || effect == USER_RGB_MODE_DYN_03) && get_condition())
        {
            //抢到的灯给到拾音
            trigger_record_data_process();      //启动拾音律动
            set_rgb_mode_auto(effect, &_G_info, USER_RGB_MODE_DYN_02);
        } else {
            if(effect == USER_RGB_MODE_BLE_CONN_OK || effect == USER_RGB_MODE_BT_DISCONNECTED  ||
               effect == USER_RGB_MODE_GREEN_STAY || effect == USER_RGB_MODE_OPEN_BLE_DISCOVER )
            {
                if(get_current_effect() == USER_RGB_MODE_ALARM_ACTIVE) {
                    //keep alarm
                } else {
                    set_rgb_mode_auto(effect, &_G_info, effect);
                }
            } else {
                set_rgb_mode_auto(effect, &_G_info, effect);
            }
        }
    }
    pv_exist = 0;
}

static  void wait_wakeup_end(void) 
{
    // syslog(LOG_INFO, "LTDIS_WAIT_END\r\n");
    struct timeval start_time;
    gettimeofday(&start_time, NULL);  // 获取初始时间

    // 轮询等待 A_cnt=3 或超时
    while (fade_step < 130) 
    {
        // 获取当前时间
        struct timeval current_time;
        gettimeofday(&current_time, NULL);

        // 计算已等待的时间（单位：毫秒）
        int64_t elapsed_ms = (current_time.tv_sec - start_time.tv_sec) * 1000 +
                            (current_time.tv_usec - start_time.tv_usec) / 1000;

        // 超时判断
        if (elapsed_ms >= 500) {
            break;
        }

        // 短暂休眠（60ms）
        usleep(60000);
    }
}
/****************************************************************************
 * shut exec interface
 ****************************************************************************/
void shut_gateway_exec(int effect)
{
    if(effect == USER_RGB_MODE_DUMMY_RESUME)
    {
        syslog(LOG_INFO, "DUMMY_RESUME\r\n");
    }

    if(effect == USER_RGB_MODE_NO_TTS_END_OFF)
    {
        delete_specified_node(_G_info.led_stack, USER_RGB_MODE_WALK_LE_AND_RI);
    }

    if(effect == USER_RGB_MODE_DYN_02)      //该灯效未入栈，放前面，防止在空栈时处理不到
    {
        pthread_mutex_lock(&exec_ctx.cmd_mutex);
        syslog(LOG_INFO, "PICK_VOICE_DISMISS\r\n");
        _G_info.listruc_set = false;
        pthread_mutex_unlock(&exec_ctx.cmd_mutex);
        destory_record_data_process();
    }

    if(effect == USER_RGB_MODE_MIPLAY)
    {
        _G_client_info.miplay_recv_runing = false;
    }

    syslog(LOG_INFO, "RGB_POP\r\n");
    if(empty_stack(_G_info.led_stack))
    {
        if (_G_info.listruc_set)  //还在拾音
        {
            syslog(LOG_INFO, "empty_stack2_pickup\r\n");
            led_effect_distribute_gateway((void *)USER_RGB_MODE_DYN_02);
            return;
        }

        syslog(LOG_INFO, "empty_stack2_shutoff\r\n");
        /*
          1.当前不是唤醒灯效都能进（满足 con 1）
          2.当前是唤醒等效，但是要拾音结束，拾音结束都能进（满足 con 2）
          3.当前是唤醒等效，且不是拾音要结束，直接 return 了
        */
        if(get_current_effect() != USER_RGB_MODE_SIDE_TO_CENTER || effect == USER_RGB_MODE_DYN_02)
        {
            if(effect == USER_RGB_MODE_MIC_MUTE)
            {
                set_rgb_mode_auto(USER_RGB_MODE_MIC_MUTE_OFF, &_G_info, USER_RGB_MODE_OFF);
            } else if(get_current_effect() == USER_RGB_MODE_WALK_LE_AND_RI && effect != USER_RGB_MODE_NO_TTS_END_OFF)
            {
                //nothingtodo 不打断语义理解
            } else if (get_current_effect() == USER_RGB_MODE_SIDE_TO_CENTER && effect == USER_RGB_MODE_DYN_02)
            {
                //唤醒且要拾音结束
                wait_wakeup_end();      //等待唤醒完整展示
                set_rgb_mode_auto(effect, &_G_info, USER_RGB_MODE_OFF);
            } else if(
                        (_G_info.cur_ef == USER_RGB_MODE_ALARM_ACTIVE && effect == USER_RGB_MODE_OPEN_BLE_DISCOVER) ||
                        (_G_info.cur_ef == USER_RGB_MODE_ALARM_ACTIVE && effect == USER_RGB_MODE_DYN_03)            ||
                        (_G_info.cur_ef == USER_RGB_MODE_ALARM_ACTIVE && effect == USER_RGB_MODE_DYN_02)
                     )
            { 
                //keep alarm active
            } else
            {
                set_rgb_mode_auto(effect, &_G_info, USER_RGB_MODE_OFF);
            }
        }
        return;
    }

    //删除指定节点
    delete_specified_node(_G_info.led_stack, effect);

    /* 出栈播放灯效后，仍有其他 music 类型灯效， 无需销毁 capalg 线程，减少 capalg 的开关频率*/
    if(effect == USER_RGB_MODE_DYN_01 || effect == USER_RGB_MODE_DYN_TTS || effect == USER_RGB_MODE_DYN_03 || effect == USER_RGB_MODE_MIAOBO_DYN)
    {
        if(effect == USER_RGB_MODE_DYN_TTS) {
            _G_info.tts_rhy  = false;
        }
    
        if( //存在任意播放不销毁
            !(
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_01)  ||
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_TTS) ||
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_03)  ||
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_MIAOBO_DYN)
            )
          )
        {
            destory_play_data_process();
        }
    }

    print_stack(_G_info.led_stack);
    if(empty_stack(_G_info.led_stack))
    {
        if (_G_info.listruc_set)  //还在拾音
        {
            syslog(LOG_INFO, "empty_stack2_pickup\r\n");
            led_effect_distribute_gateway((void *)USER_RGB_MODE_DYN_02);
            return;
        }

        if(get_current_effect() != USER_RGB_MODE_SIDE_TO_CENTER || effect == USER_RGB_MODE_DYN_02)
        {
            if(effect == USER_RGB_MODE_MIC_MUTE)
            {
                set_rgb_mode_auto(USER_RGB_MODE_MIC_MUTE_OFF, &_G_info, USER_RGB_MODE_OFF);
            } else if(get_current_effect() == USER_RGB_MODE_WALK_LE_AND_RI && effect != USER_RGB_MODE_NO_TTS_END_OFF)
            {
                //nothingtodo 不打断语义理解
            }  else if (get_current_effect() == USER_RGB_MODE_SIDE_TO_CENTER && effect == USER_RGB_MODE_DYN_02)
            {
                //唤醒且要拾音结束
                wait_wakeup_end();      //等待唤醒完整展示
                set_rgb_mode_auto(effect, &_G_info, USER_RGB_MODE_OFF);
            } else if(
                        (_G_info.cur_ef == USER_RGB_MODE_ALARM_ACTIVE && effect == USER_RGB_MODE_OPEN_BLE_DISCOVER) ||
                        (_G_info.cur_ef == USER_RGB_MODE_ALARM_ACTIVE && effect == USER_RGB_MODE_DYN_03)            ||
                        (_G_info.cur_ef == USER_RGB_MODE_ALARM_ACTIVE && effect == USER_RGB_MODE_DYN_02)
                     )
            {
                //keep alarm active
            } else {
                set_rgb_mode_auto(effect, &_G_info, USER_RGB_MODE_OFF);
            }
        }
        return;
    } else {  //连续对话关麦后有音乐播放灯效要恢复
    #if 0
        if(top_stack(_G_info.led_stack) == USER_RGB_MODE_DYN_01) {
            led_effect_distribute_gateway((void *)USER_RGB_MODE_DYN_01);
        } else if(top_stack(_G_info.led_stack) == USER_RGB_MODE_MIAOBO_DYN) {
            led_effect_distribute_gateway((void *)USER_RGB_MODE_MIAOBO_DYN);
        }else if (top_stack(_G_info.led_stack) == USER_RGB_MODE_DYN_03) {
            led_effect_distribute_gateway((void *)USER_RGB_MODE_DYN_03);
        } else if(top_stack(_G_info.led_stack) == USER_RGB_MODE_DYN_TTS) {
            led_effect_distribute_gateway((void *)USER_RGB_MODE_DYN_TTS);
        } else
        {
    #endif
            int top = top_stack(_G_info.led_stack);
            int rel_issue = top;
            if(
                (top == USER_RGB_MODE_DYN_01 || top == USER_RGB_MODE_MIAOBO_DYN || top == USER_RGB_MODE_DYN_03 || top == USER_RGB_MODE_DYN_TTS)
                && (_G_client_info.music_recv_runing == false)
              )
            {
                if(top == USER_RGB_MODE_DYN_TTS) {
                    _G_info.tts_rhy  = true;
                } else {
                    _G_info.tts_rhy  = false;
                }
                //capalg 不会被 voice 关闭，但能量接收线程会被关闭，恢复
                _G_client_info.music_recv_runing = true;
            }

            if(top == USER_RGB_MODE_MIPLAY && _G_client_info.miplay_recv_runing == false)
            {
                _G_client_info.miplay_recv_runing = true;
            }

            if((top == USER_RGB_MODE_DYN_01 || top == USER_RGB_MODE_DYN_03) && get_condition() && effect != USER_RGB_MODE_DYN_02)  //全双工，播放灯光关闭，恢复到拾音灯光
            {
                //抢到的灯给到拾音
                trigger_record_data_process();      //启动拾音律动
                rel_issue = USER_RGB_MODE_DYN_02;
            }

            if(effect == USER_RGB_MODE_MIC_MUTE) {
                set_rgb_mode_auto(USER_RGB_MODE_MIC_MUTE_OFF, &_G_info, rel_issue);
            } else {
                if(top == USER_RGB_MODE_WALK_LE_AND_RI) {
                    set_rgb_mode_auto(USER_RGB_MODE_WALK_LE_AND_RI_NO_REST, &_G_info, USER_RGB_MODE_WALK_LE_AND_RI_NO_REST);
                } else {
                    set_rgb_mode_auto(effect, &_G_info, rel_issue);
                }
            }
        // }
    }
    return;
}
#if 0
static void clean_handler1(void* arg)
{
    char *buf = *(char **)arg;
    if (buf != NULL) 
    {
        free(buf);
        buf = NULL;
    }
    _G_info.play_data_pc_thr = 0;
    // sem_post(&_G_info.pc_destory_sem);  //thread destory
}
#endif
/****************************************************************************
 * trigger data process
 ****************************************************************************/
void trigger_play_data_process(void)
{
    /*暂停继续：Musicdismiss + onmusicshow，暂停 capalg返回 -1
     *上下一首：不同于暂停继续，无Musicdismiss，直接onmusicshow，要重新start recorder, 不重新 start capalg 会一直返回 0
    */
    if(_G_client_info.music_recv_runing)
    {
        // _G_client_info.music_recv_runing = false;
        // while ((sem_wait(&_G_client_info.music_recv_end_sem)) < 0 && errno == EINTR);
        send_cmd_to_rrs_client(CMD_MUSIC_DESTORY);  //重新 start
        rhy_old_energy = 0;
    } else 
    {
        // 全双工时，存在播放音乐被拾音打断，此时 capalg是开着的，接着来了 TTS，要先stop capalg
        if(
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_01)  ||
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_TTS) ||
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_DYN_03)  ||
            is_in_stack(_G_info.led_stack, USER_RGB_MODE_MIAOBO_DYN)
          ) 
        {
            send_cmd_to_rrs_client(CMD_MUSIC_DESTORY);  //重新 start
        }
    }
#if 0
    if(_G_client_info.voice_recv_runing)
    {
        _G_client_info.voice_recv_runing = false;
        while ((sem_wait(&_G_client_info.voice_recv_end_sem)) < 0 && errno == EINTR);
        send_cmd_to_rrs_client(CMD_VOICE_DESTORY);
        rhy_old_energy = 0;
    }
#endif
    _G_client_info.voice_recv_runing = false;
    _G_client_info.miplay_recv_runing = false;
    _G_client_info.music_recv_runing = true;
    send_cmd_to_rrs_client(CMD_MUSIC_TRIGGER);
}
void trigger_record_data_process(void)
{
#if  0
    if(_G_client_info.music_recv_runing)
    {
        _G_client_info.music_recv_runing = false;
        while ((sem_wait(&_G_client_info.music_recv_end_sem)) < 0 && errno == EINTR);
        // send_cmd_to_rrs_client(CMD_MUSIC_DESTORY);
        rhy_old_energy = 0;
    }

    if(_G_client_info.voice_recv_runing)
    {
        _G_client_info.voice_recv_runing = false;
        while ((sem_wait(&_G_client_info.voice_recv_end_sem)) < 0 && errno == EINTR);
        send_cmd_to_rrs_client(CMD_VOICE_DESTORY);
        rhy_old_energy = 0;
    }
    _G_client_info.voice_recv_runing = true;
#endif
    _G_client_info.music_recv_runing = false;
    _G_client_info.miplay_recv_runing = false;
    _G_client_info.voice_recv_runing = true;
    send_cmd_to_rrs_client(CMD_VOICE_TRIGGER);       //用在录音数据在a7 上获取
}

void destory_record_data_process(void)
{
#if 0
    if(_G_client_info.voice_recv_runing)
    {
        _G_client_info.voice_recv_runing = false;
        while ((sem_wait(&_G_client_info.voice_recv_end_sem)) < 0 && errno == EINTR);
        rhy_old_energy = 0;
    }
#endif
    _G_client_info.voice_recv_runing = false;
    send_cmd_to_rrs_client(CMD_VOICE_DESTORY);
}
void destory_play_data_process(void)
{
    send_cmd_to_rrs_client(CMD_MUSIC_DESTORY);
    if(_G_client_info.music_recv_runing)
    {
        _G_client_info.music_recv_runing = false;
        // while ((sem_wait(&_G_client_info.music_recv_end_sem)) < 0 && errno == EINTR);
        rhy_old_energy = 0;
    }
}

static FAR void *nm_update_thread(FAR void *arg)
{
    char  light[32];

    while(1)
    {
        _G_volume = am_get_volume(2);  // get volume
        memset(light, 0 ,sizeof(light));
        property_get("persist.nightmode.light", light, "night");
        _G_btVolume = property_get_int32("persist.media.A2dpsnkVolume", 100);
        //总开关 和 灯光开关
        if( sys_sleep_mode_get() == true &&  strcmp("night", light) == 0 ) // get nightmode
        {
            _G_nm = true;
        } else {
            _G_nm = false;
        }
        usleep(150 * 1000);
    }

    return  NULL;
}
FAR void *pv_rhy_exec_thread(FAR void *arg)
{
    int                 nbytes;
    RHYTHM_MSG          msg_buffer;

    mqd_t mqfd = mq_open(PV_RHYTHM_QUEUE_NAME, O_RDONLY);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"pv_rhy_exec: ERROR mq_open failed errno = %d\r\n", errno);
        return (void *)(-1);
    }

    while (1)
    {
        memset(&msg_buffer, 0, RHYTHM_MSG_LEN);
        nbytes = mq_receive(mqfd, (char*)&msg_buffer, RHYTHM_MSG_LEN, NULL);
        if (nbytes < 0)
        {
            /* mq_receive failed.  If the error is because of EINTR then
            * it is not a failure.
            */

            if (errno != EINTR)
            {
                syslog(LOG_ERR,"pv_rhy_exec:ERROR mq_receive failure errno=%d\r\n",errno);
            }
            else
            {
                syslog(LOG_ERR," pv_rhy_exec:ERROR mq_receive mq_receive interrupted!\r\n");
            }
        }
        else if (nbytes != RHYTHM_MSG_LEN)
        {
            syslog(LOG_ERR," pv_rhy_exec:mq_receive return bad size %d \r\n", nbytes);
        } else {
                int  display_num = msg_buffer.dis_num;
                int  display_v = msg_buffer.hsv.v;
                int  left_index = 3;
                int  right_index = 4;

                int i = 0;
                //填充 hsv buff
                memset(_G_info.hsv, 0, sizeof(_G_info.hsv));
                if(display_num < 1 || display_num > 4) {
                    continue;
                }
                for(i = 0; i < display_num; i++)
                {
                    _G_info.hsv[right_index + i].h = 330;
                    _G_info.hsv[right_index + i].s = 70;
                    _G_info.hsv[left_index - i].h = 330;
                    _G_info.hsv[left_index - i].s = 70;

                    //填充每个灯的hsv
                    if(i == display_num - 1) {    //最顶上的灯亮度变化
                        if(display_num == 1) 
                        {
                            if(display_v != 0)
                            {
                                //left
                                _G_info.hsv[left_index - i].v = display_v;
                                //right
                                _G_info.hsv[right_index + i].v = display_v;
                            } else 
                            {
                                //left
                                _G_info.hsv[left_index - i].v = 0;
                                //right
                                _G_info.hsv[right_index + i].v = 0;
                            }
                        } else
                        {
                            //left
                            _G_info.hsv[left_index - i].v = display_v;
                            //right
                            _G_info.hsv[right_index + i].v = display_v;
                        }
                    } else if (i == display_num - 2)// 2.3.4  倒2
                    {
                        switch (display_num)
                        {
                            case 2:
                                //left
                                _G_info.hsv[left_index - i].v = 170;
                                //right
                                _G_info.hsv[right_index + i].v = 170;
                                break;

                            case 3:
                                //left
                                _G_info.hsv[left_index - i].v = 10 > display_v ?  10 : display_v;
                                //right
                                _G_info.hsv[right_index + i].v = 10 > display_v ?  10 : display_v;
                                break;

                            case 4:
                                //left
                                _G_info.hsv[left_index - i].v = 20 > display_v ?  20 : display_v;
                                //right
                                _G_info.hsv[right_index + i].v = 20 > display_v ?  20 : display_v;
                                break;

                            default:
                                break;
                        }
                    }else if(i == display_num - 3) // 3.4  倒3
                    {
                        switch (display_num)
                        {
                            case 3:
                                //left
                                _G_info.hsv[left_index - i].v = 240;
                                //right
                                _G_info.hsv[right_index + i].v = 240;
                                break;

                            case 4:
                                //left
                                _G_info.hsv[left_index - i].v = 220;
                                //right
                                _G_info.hsv[right_index + i].v = 220;
                                break;

                            default:
                                break;
                        }
                    } else if(i == display_num - 4) // 4 倒4
                    {
                        switch (display_num)
                        {
                            case 4:
                                //left
                                _G_info.hsv[left_index - i].v = 240;
                                //right
                                _G_info.hsv[right_index + i].v = 240;
                                break;

                            default:
                                break;
                        }
                    }
                }

                if(_G_client_info.music_recv_runing == false && _G_client_info.voice_recv_runing == false)  //在等待能量发送线程结束时间段内，会存在闪烁的情况
                {
                    memset(_G_info.hsv, 0, sizeof(_G_info.hsv));
                }
                one_by_one_fill_info_buff(&_G_info, LED_NUM); //hsv-->rgb buf
                // syslog(LOG_INFO, "<-----1111 display_num = %d display_v = %d\r\n", display_num, display_v);
                if(_G_info.cur_ef == USER_RGB_MODE_DYN_02)
                {
                    send_rgbbuf_to_spi(&_G_info);
                }
        }

    }

    return  (void *)(0);
}
static void create_pv_rhy_exec_thread(void)
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;

    pthread_attr_init(&attr);
    param.sched_priority = 204;
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setstacksize(&attr, 4096);

    pthread_create(&thread, &attr, pv_rhy_exec_thread, NULL);
    pthread_setname_np(thread, "pv_rhy_exec_thread");
}

static void create_nm_update_thread(void)
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;

    pthread_attr_init(&attr);
    param.sched_priority = 200;
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setstacksize(&attr, 4096);

    pthread_create(&thread, &attr, nm_update_thread, NULL);
    pthread_setname_np(thread, "nm_update_thread");
}

static void refresh_play_index(int index)
{
    static  int  refresh_cnt;
    int newest_index;
    RHYTHM_MSG msg;
    int display_h;
    int display_s;
    newest_index = index;
    refresh_cnt++;
    // printf("newest_index = %d\r\n", newest_index);
    if(refresh_cnt >= 2)  //7 * 2
    {
        output_next_colour(&display_h, &display_s);
        refresh_cnt = 0;
        msg.dis_num = db_map_arr[newest_index].cnt;
        msg.hsv.h = display_h;
        msg.hsv.s = display_s;
        msg.hsv.v = db_map_arr[newest_index].v;
        // printf("newest_index = %d------------\r\n", newest_index);
        msg.pMap = db_map_arr;
        send_rhythm(msg);
    }
}
static void refresh_record_index(int index)
{
    static  int  rc_refresh_cnt;
    int newest_index;
    RHYTHM_MSG msg;
    newest_index = index;
    rc_refresh_cnt++;
    // printf("newest_index = %d\r\n", newest_index);
    if(rc_refresh_cnt >= 2)  //7 * 2
    {
        rc_refresh_cnt = 0;
        msg.dis_num = db_record_map_arr[newest_index].cnt;
        msg.hsv.v = db_record_map_arr[newest_index].v;
        // printf("newest_index = %d------------\r\n", newest_index);
        msg.pMap = db_record_map_arr;
        send_pvrhythm(msg);
    }
}

static FAR void *grad_change_thread(FAR void *arg)
{
    int                 nbytes;
    GRAD_CHANGE_MSG     msg_buffer;
    ArrayPtr            pMap;
    ArrayPtr            prepMap = NULL;
    RHYTHM_MSG          msg;
    int                 display_num = 1;
    int                 display_h = 330;
    int                 display_s = 70; 
    int                 display_v = 150;
    double              rms;
    static  int         preIndex;            //记录在之前 map 中的index
    int                 cur_index = 0;       //记录当前map 中的index，决定渐变
    int                 tmpIndex = 0;
    int                 len = 0;
    float               diffStep;
    float               step_num = 0;
    int                 istep;

    mqd_t mqfd = mq_open(GRAD_CHANGE_QUEUE_NAME, O_RDONLY);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"grad_change_thread: ERROR mq_open failed errno = %d\r\n", errno);
        return (void *)(-1);
    }

    while (1)
    {
        memset(&msg_buffer, 0, GRAD_CHANGE_MSG_LEN);
        nbytes = mq_receive(mqfd, (char*)&msg_buffer, GRAD_CHANGE_MSG_LEN, NULL);
        if (nbytes < 0){
            //TODO
        } else if (nbytes != GRAD_CHANGE_MSG_LEN)
        {
            //TODO
        } else 
        {
            pMap = msg_buffer.pMap;
            rms = msg_buffer.rms;

            if(pMap != NULL)
            {
                if(pMap == db_map_arr)
                {
                    len = (sizeof(db_map_arr) / sizeof(db_map_arr[0]));
                    step_num = MUSIC_PLAY_STEP_NUM;
                    // output_next_colour(&display_h, &display_s);
                } else if(pMap == db_map_arr_tts)
                {
                    len = (sizeof(db_map_arr_tts) / sizeof(db_map_arr_tts[0]));
                    step_num = TTS_PLAY_STEP_NUM;
                } else if(pMap == db_record_map_arr)
                {
                    len = (sizeof(db_record_map_arr) / sizeof(db_record_map_arr[0]));
                    step_num = VOICE_PICK_STEP_NUM;
                }

                //拿到 map 的index  和 DIS-V
                if (rms < pMap[0].db)
                {
                    cur_index = 0;
                    display_num = 1;
                    if(pMap == db_map_arr || pMap == db_map_arr_tts)
                    {  //播放 和 tts
                        display_v = 150;    //低于最小值时候，保持亮度，避免闪烁
                    } else { //拾音
                        display_v = 150;
                    }
                } else 
                {
                    int i = 0;
                    for(i = len - 1; i >= 0; i--)
                    {
                        if(rms >= pMap[i].db)
                        {
                            display_num = pMap[i].cnt;
                            display_v = pMap[i].v;
                            cur_index = i;
                            break;
                        }
                    }
                }
                // printf("preIndex = %d ---cur_index = %d\r\n",preIndex, cur_index);

                #if 0
                if(pMap == db_map_arr)
                {
                    if(display_num == 2)
                    {
                        if(mid_cnt <= 40) {             //长时间保持在中间值，下拉一个单位，使变换更明显
                            mid_cnt++;
                        } else {
                            display_num = 1;
                            display_v = 150;
                        }
                    } else {
                        mid_cnt = 0;
                    }
                }
                #endif

                tmpIndex = cur_index;
                if(prepMap != pMap) {  //直接更新
                    //重置
                    preIndex = 0;
                    cur_index = 0;
                    _G_info.cur_percent = 0;
                    rhy_old_energy = 0;
                    display_num = 1;
                    msg.dis_num = display_num;
                    msg.hsv.h = display_h;
                    msg.hsv.s = display_s;
                    msg.hsv.v = display_v;
                    msg.pMap = pMap;
                    send_rhythm(msg);
                } else {    //same map
                    //从preindex--》cur_index
                    // if(pMap == db_map_arr_tts || pMap == db_map_arr )      //TTS 特殊情况，要求缓慢
                    if(1)
                    {
                        if(cur_index < preIndex)
                        {
                            #if 0
                            if(preIndex - cur_index >= 10)
                            { 
                                cur_index =  preIndex - 3;
                            } else if(preIndex - cur_index >= 2)
                            {
                            #endif
                                cur_index = preIndex - 1;
                            // }

                            if(cur_index < tmpIndex)
                            {
                                cur_index = tmpIndex;
                            }
                        } else 
                        {
                            #if 0
                            if(cur_index - preIndex >= 10)
                            { 
                                cur_index = preIndex + 3;
                            } else if(cur_index - preIndex >= 2)
                            {
                            #endif
                                cur_index = preIndex + 1;
                            // }

                            if(cur_index > tmpIndex)
                            {
                                cur_index = tmpIndex;
                            }
                        }
                        if(cur_index < 0) {cur_index = 0;}
                        if(cur_index >= len) {cur_index = len -1;}

                        if(pMap == db_map_arr_tts)
                        {
                            msg.dis_num = pMap[cur_index].cnt;
                            msg.hsv.h = display_h;
                            msg.hsv.s = display_s;
                            msg.hsv.v = pMap[cur_index].v;
                            msg.pMap = pMap;
                            send_rhythm(msg);
                        } else if(pMap == db_map_arr)
                        {
                            //Refresh the played index
                            refresh_play_index(cur_index);
                        } else if(pMap == db_record_map_arr)
                        {
                            //Refresh the pick_voice index
                            refresh_record_index(cur_index);
                        }
                    } else {                //voice_pick
                        diffStep = (preIndex - cur_index) / (float)(step_num);

                        for (istep = 0; istep <= step_num; istep++) 
                        {
                            int tmp_index = preIndex - istep * diffStep;

                            // printf("tmp_index = %d\r\n", tmp_index);

                            if(tmp_index < 0) {tmp_index = 0;}
                            if(tmp_index >= len) {tmp_index = len - 1;}

                            msg.dis_num = pMap[tmp_index].cnt;
                            msg.hsv.h = display_h;
                            msg.hsv.s = display_s;
                            msg.hsv.v = pMap[tmp_index].v;
                            msg.pMap = pMap;
                            send_pvrhythm(msg);
                            usleep(8 * 1000);
                        }
                    }
                }
                preIndex = cur_index;
                prepMap = pMap;
            }
        }
    }
}

static void create_grad_change_thread(void)
{
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;

    pthread_attr_init(&attr);
    param.sched_priority = 202;
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setstacksize(&attr, 20480);

    pthread_create(&thread, &attr, grad_change_thread, NULL);
    pthread_setname_np(thread, "grad_change_thread");
}

void set_listen_instruc(void) 
{
    pthread_mutex_lock(&exec_ctx.cmd_mutex);
    if (_G_info.listruc_set)        //拦截多余的设置
    {
        pthread_mutex_unlock(&exec_ctx.cmd_mutex);
        return;
    }
    //false
    if(!is_in_stack(_G_info.led_stack, USER_RGB_MODE_NET_OFF))  //断网时拦截声音灯光下发
    {
        syslog(LOG_INFO, "SET_START_PICK_VIOCE\r\n");
        _G_info.listruc_set = true;
        exec_ctx.con_met = 1;
        pthread_cond_signal(&exec_ctx.exec_cond);
    }
    pthread_mutex_unlock(&exec_ctx.cmd_mutex);
}

void set_pre_config_mode(void) 
{
    led_effect_distribute_gateway((void *)USER_RGB_SET_PRE_CONFIG_MODE);
}

static void* set_lisen_intruc_exec_thread(void *arg) 
{
    exec_ctx_t *ctx = (exec_ctx_t *)arg;

    while(1) 
    {
        pthread_mutex_lock(&ctx->cmd_mutex);
        while (!exec_ctx.con_met) {
            pthread_cond_wait(&ctx->exec_cond, &ctx->cmd_mutex);
        }
        exec_ctx.con_met = 0;   //reset condition
        pthread_mutex_unlock(&ctx->cmd_mutex);

        usleep(400 * 1000);
        syslog(LOG_INFO, "%s:get cond, listruc_set=%d\n", __func__, _G_info.listruc_set);
        if(_G_info.listruc_set) {
            led_effect_distribute_gateway((void *)USER_RGB_MODE_DYN_02);
        }
    }
    return NULL;
}

int set_listen_instruc_exec_init(void)
{
    syslog(LOG_INFO, "%s\n", __func__);
    memset(&exec_ctx, 0, sizeof(exec_ctx_t));
    pthread_mutex_init(&exec_ctx.cmd_mutex, NULL);
    pthread_cond_init(&exec_ctx.exec_cond, NULL);
    exec_ctx.con_met = 0;
    int ret = pthread_create(&exec_ctx.tid, NULL, set_lisen_intruc_exec_thread, (void *)&exec_ctx);
    if (ret < 0) {
        syslog(LOG_ERR, "%s %d nxtask create failed, errno %d\n", __FILE__, __LINE__, errno);
    }
    return ret;
}
void set_play_led(bool enable)
{
    _G_info.pl_enable = enable;

    if(get_continuous_diag_stat())  //全双工开启
    {
        if(enable) {
            //开启
            led_effect_distribute_gateway((void *)USER_RGB_MODE_DYN_01_BY_PASS);
        } else {
            led_effect_distribute_gateway((void *)USER_RGB_MODE_DYN_02_BY_PASS);
        }
    }
}

bool get_play_led(void)
{
    return _G_info.pl_enable;
}

static FAR void *light_exec_thread(FAR void *arg)
{
    int         nbytes;
    EXEC_MSG    msg;

    mqd_t mqfd = mq_open(EXEC_MSG_QUEUE, O_RDONLY);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"light_exec_thread: ERROR mq_open failed errno = %d\r\n", errno);
        return (void *)(-1);
    }

    while (1)
    {
        memset(&msg, 0, EXEC_MSG_LEN);
        nbytes = mq_receive(mqfd, (char*)&msg, EXEC_MSG_LEN, NULL);
        if (nbytes < 0)
        {
            /* mq_receive failed.  If the error is because of EINTR then
            * it is not a failure.
            */

            if (errno != EINTR) {
                syslog(LOG_ERR,"light_exec_thread:ERROR mq_receive failure errno=%d\r\n",errno);
            } else {
                syslog(LOG_ERR," light_exec_thread:ERROR mq_receive mq_receive interrupted!\r\n");
            }
        } else if (nbytes != EXEC_MSG_LEN) {
            syslog(LOG_ERR," light_exec_thread:mq_receive return bad size %d \r\n", nbytes);
        } else {
            if(access("/tmp/led_mask", F_OK) == 0) {
                continue;
            }
            if(msg.type == TYPE_DISTRIBUTE) {
                distribute_gateway_exec(msg.effect);
            } else if (msg.type == TYPE_SHUT) {
                shut_gateway_exec(msg.effect);
            }
        }
    }
}
void  light_exec_init(void)
{
    struct mq_attr      attr_q;
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;

    attr_q.mq_maxmsg  = 100;
    attr_q.mq_msgsize = EXEC_MSG_LEN;
    attr_q.mq_flags   = 0;                                    /* block        */

    mqd_t mqfd = mq_open(EXEC_MSG_QUEUE, O_RDWR | O_CREAT, 0666, &attr_q);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"EXEC_MSG_QUEUE: ERROR mq_open failed, errno = %d\r\n", errno);
        return;
    }
    mq_close(mqfd);
    pthread_mutex_init(&_G_info.exec_mutex, NULL);

    pthread_attr_init(&attr);
    param.sched_priority = 200;
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setstacksize(&attr, 40960);

    pthread_create(&thread, &attr, light_exec_thread, NULL);
    pthread_setname_np(thread, "light_exec_thread");
}

void set_boot_success(void)
{
    set_rgb_mode_auto(USER_RGB_MODE_BOOT_SUCCESS, &_G_info, USER_RGB_MODE_BOOT_SUCCESS);
}

/****************************************************************************
 * ws2812_main
 ****************************************************************************/
FAR void *rhythm_exec(FAR void *arg);
void env_setup(void)
{
    /* 把律动事件丢给线程处理*/
    struct mq_attr      attr_q;

    attr_q.mq_maxmsg  = 100;
    attr_q.mq_msgsize = RHYTHM_MSG_LEN;
    attr_q.mq_flags   = 0;                                    /* block        */

    mqd_t mqfd = mq_open(RHYTHM_QUEUE_NAME, O_RDWR | O_CREAT, 0666, &attr_q);     //发送要亮的灯的个数和颜色
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"rhythm_exec: ERROR mq_open failed, errno = %d\r\n", errno);
        return;
    }

    mqfd = mq_open(PV_RHYTHM_QUEUE_NAME, O_RDWR | O_CREAT, 0666, &attr_q);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"PV_RHYTHM_QUEUE_NAME: ERROR mq_open failed, errno = %d\r\n", errno);
        return;
    }

    attr_q.mq_maxmsg  = 100;
    attr_q.mq_msgsize = GRAD_CHANGE_MSG_LEN;
    attr_q.mq_flags   = 0;                                    /* block        */

    mqfd = mq_open(GRAD_CHANGE_QUEUE_NAME, O_RDWR | O_CREAT, 0666, &attr_q);     //发送 NUM & V

    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"GRAD_CHANGE_QUEUE_NAME : ERROR mq_open failed, errno = %d\r\n", errno);
        return;
    }

#if 0
    struct mq_attr      attr_q1;
    attr_q1.mq_maxmsg  = 1;
    attr_q1.mq_msgsize = RHYTHM_RECV_ENERGY_MSG_LEN;
    attr_q1.mq_flags   = O_NONBLOCK;                          /* no block        */

    mqd_t mq_recv_energy = mq_open(RHYTHM_RECV_ENERGY_QUEUE, O_RDWR | O_CREAT, 0666, &attr_q1);     //接收数据处理线程发来的能量直
    if (mq_recv_energy == (mqd_t)-1)
    {
        syslog(LOG_ERR,"rhythm_exec: ERROR open mq_recv_energy failed, errno = %d\r\n", errno);
        return;
    }
#endif
	syslog(LOG_INFO,"rhythm_exec tasks open success\r\n");

    RGB_MODE_INFO *info = &_G_info;

    info->cur_percent = 0;
    pthread_mutex_init(&info->cur_percent_mutex, NULL);
    pthread_mutex_init(&info->rgb_buf_mutex, NULL);
    pthread_mutex_init(&info->info_para_mutex, NULL);
    pthread_mutex_init(&stack_mutex, NULL);

    info->pl_enable = true;
    //初始化灯的运行数据结构环境
    info->time_m = 100;
    info->time_d = 100;

    info->run_table = &rgb_mode_run_table;

    if(!info->timer_cm)info->timer_cm = (info->time_m)?info->time_m : 100;
    if(!info->timer_cd)info->timer_cd = (info->time_d)?info->time_d : 100;

    struct sigevent        evp_m;
    struct sigevent        evp_d;
    struct itimerspec      ts_d;
    struct itimerspec      ts_m;

    memset(&evp_d, 0, sizeof(evp_d));
    evp_d.sigev_value.sival_ptr = &info->id_d;
    evp_d.sigev_notify          = SIGEV_THREAD;
    evp_d.sigev_notify_function = rgb_dyn;
    evp_d.sigev_value.sival_int = 0;

    memset(&evp_m, 0, sizeof(evp_m));
    evp_m.sigev_value.sival_ptr = &info->id_m;
    evp_m.sigev_notify          = SIGEV_THREAD;
    evp_m.sigev_notify_function = rgb_scan;
    evp_m.sigev_value.sival_int = 0;

    //创建定时器
    timer_create(CLOCK_REALTIME, &evp_d, &info->id_d);
    timer_create(CLOCK_REALTIME, &evp_m, &info->id_m);

    //启动并设置重装
    ts_d.it_value.tv_sec        = 0;
    ts_d.it_value.tv_nsec       = info->timer_cd * 1000 * 1000;
    ts_d.it_interval.tv_sec     = 0;
    ts_d.it_interval.tv_nsec    = info->timer_cd * 1000 * 1000;

    ts_m.it_value.tv_sec        = 0;
    ts_m.it_value.tv_nsec       = info->timer_cm * 1000 * 1000;
    ts_m.it_interval.tv_sec     = 0;
    ts_m.it_interval.tv_nsec    = info->timer_cm * 1000 * 1000;

    timer_settime(info->id_d, 0, &ts_d, NULL);
    timer_settime(info->id_m, 0, &ts_m, NULL);

    info->led_stack = creat_stack();
#if 0
    set_rgb_mode_auto(info,USER_RGB_MODE_WHILT_BREATH);
    usleep(15 * 100 * 1000);

    int  reboot_para = reboot_param_oh2_get();
    if(!(
        (reboot_para & (REBOOT_PARAM_OH2_SLIENT | 0xFFFF)) == (REBOOT_PARAM_OH2_SLIENT | REBOOT_PARAM_OH2_MASK) ||
        (reboot_para & (REBOOT_PARAM_CRASH | 0xFFFF)) == (REBOOT_PARAM_CRASH | REBOOT_PARAM_OH2_MASK)
        ))
    {
        set_rgb_mode_auto(USER_RGB_MODE_BOOT_SUCCESS, info, USER_RGB_MODE_BOOT_SUCCESS);
    }
#endif

    /* 创建律动处理线程，优先级要高，
       不然在被调度走时，灯的时序会受影响，出现杂色
     */
    struct sched_param param;
    pthread_attr_t attr;
    pthread_t thread;

    pthread_attr_init(&attr);
    param.sched_priority = 204;
    pthread_attr_setschedparam(&attr, &param);
    pthread_attr_setstacksize(&attr, 4096);

    pthread_create(&thread, &attr, rhythm_exec, NULL);
    pthread_setname_np(thread, "rhythm_exec");

    create_pv_rhy_exec_thread();
    create_nm_update_thread();
    create_grad_change_thread();
    //connetct to rhythm record  service run on  a7
    rp_connect_to_rrs();
    a72ap_channel_init();
    miplay_data_recv_channel();
    voice_channel_init();

    light_exec_init();
    set_listen_instruc_exec_init();

    sleep(10);
    reboot_param_oh2_clear();

    return;
}
/****************************************************************************
 * 律动灯光处理线程
 ****************************************************************************/
FAR void *rhythm_exec(FAR void *arg)
{
    int                 nbytes;
    RHYTHM_MSG          msg_buffer;

    mqd_t mqfd = mq_open(RHYTHM_QUEUE_NAME, O_RDONLY);
    if (mqfd == (mqd_t)-1)
    {
        syslog(LOG_ERR,"rhythm_exec: ERROR mq_open failed errno = %d\r\n", errno);
        return (void *)(-1);
    }

    while (1)
    {
        memset(&msg_buffer, 0, RHYTHM_MSG_LEN);
        nbytes = mq_receive(mqfd, (char*)&msg_buffer, RHYTHM_MSG_LEN, NULL);
        if (nbytes < 0)
        {
            /* mq_receive failed.  If the error is because of EINTR then
            * it is not a failure.
            */

            if (errno != EINTR)
            {
                syslog(LOG_ERR,"rhythm_exec:ERROR mq_receive failure errno=%d\r\n",errno);
            }
            else
            {
                syslog(LOG_ERR," rhythm_exec:ERROR mq_receive mq_receive interrupted!\r\n");
            }
        }
        else if (nbytes != RHYTHM_MSG_LEN)
        {
            syslog(LOG_ERR," rhythm_exec:mq_receive return bad size %d \r\n", nbytes);
        } else {
                int  display_num = msg_buffer.dis_num;
                int  display_h = msg_buffer.hsv.h;
                int  display_s = msg_buffer.hsv.s;
                int  display_v = msg_buffer.hsv.v;
                int  left_index = 3;
                int  right_index = 4;

                int i = 0;
                //填充 hsv buff
                memset(_G_info.hsv, 0, sizeof(_G_info.hsv));
                if(display_num < 1 || display_num > 4) {
                    continue;
                }
                for(i = 0; i < display_num; i++)
                {
                    if((_G_client_info.music_recv_runing || _G_client_info.miplay_recv_runing) && _G_info.tts_rhy == false)   // 播放颜色轮换
                    {
                        _G_info.hsv[right_index + i].h = display_h;
                        _G_info.hsv[right_index + i].s = display_s;
                        _G_info.hsv[left_index - i].h = display_h;
                        _G_info.hsv[left_index - i].s = display_s;
                    } else //拾音 和 tts 白色
                    {
                        _G_info.hsv[right_index + i].h = 330;
                        _G_info.hsv[right_index + i].s = 70;
                        _G_info.hsv[left_index - i].h = 330;
                        _G_info.hsv[left_index - i].s = 70;
                    }

                    //填充每个灯的hsv
                    if(i == display_num - 1) {    //最顶上的灯亮度变化
                        if(display_num == 1) 
                        {
                            if(display_v != 0)
                            {
                                //left
                                _G_info.hsv[left_index - i].v = display_v;
                                //right
                                _G_info.hsv[right_index + i].v = display_v;
                            } else 
                            {
                                //left
                                _G_info.hsv[left_index - i].v = 0;
                                //right
                                _G_info.hsv[right_index + i].v = 0;
                            }
                        } else
                        {
                            //left
                            _G_info.hsv[left_index - i].v = display_v;
                            //right
                            _G_info.hsv[right_index + i].v = display_v;
                        }
                    } else if (i == display_num - 2)// 2.3.4  倒2
                    {
                        switch (display_num)
                        {
                            case 2:
                                //left
                                _G_info.hsv[left_index - i].v = 170;
                                //right
                                _G_info.hsv[right_index + i].v = 170;
                                break;

                            case 3:
                                //left
                                _G_info.hsv[left_index - i].v = 10 > display_v ?  10 : display_v;
                                //right
                                _G_info.hsv[right_index + i].v = 10 > display_v ?  10 : display_v;
                                break;

                            case 4:
                                //left
                                _G_info.hsv[left_index - i].v = 20 > display_v ?  20 : display_v;
                                //right
                                _G_info.hsv[right_index + i].v = 20 > display_v ?  20 : display_v;
                                break;

                            default:
                                break;
                        }
                    }else if(i == display_num - 3) // 3.4  倒3
                    {
                        switch (display_num)
                        {
                            case 3:
                                //left
                                _G_info.hsv[left_index - i].v = 240;
                                //right
                                _G_info.hsv[right_index + i].v = 240;
                                break;

                            case 4:
                                //left
                                _G_info.hsv[left_index - i].v = 220;
                                //right
                                _G_info.hsv[right_index + i].v = 220;
                                break;

                            default:
                                break;
                        }
                    } else if(i == display_num - 4) // 4 倒4
                    {
                        switch (display_num)
                        {
                            case 4:
                                //left
                                _G_info.hsv[left_index - i].v = 240;
                                //right
                                _G_info.hsv[right_index + i].v = 240;
                                break;

                            default:
                                break;
                        }
                    }
                }

                if(!_G_info.pl_enable && (_G_client_info.music_recv_runing || _G_client_info.miplay_recv_runing == false) && !_G_info.tts_rhy)  //仅播放音乐关闭
                {
                    memset(_G_info.hsv, 0, sizeof(_G_info.hsv));
                }
                if(_G_client_info.music_recv_runing == false && _G_client_info.voice_recv_runing == false && _G_client_info.miplay_recv_runing == false)  //在等待能量发送线程结束时间段内，会存在闪烁的情况
                {
                    memset(_G_info.hsv, 0, sizeof(_G_info.hsv));
                }
                one_by_one_fill_info_buff(&_G_info, LED_NUM); //hsv-->rgb buf
                // syslog(LOG_INFO, "<-----1111 display_num = %d display_v = %d\r\n", display_num, display_v);
                if(
                    _G_info.cur_ef == USER_RGB_MODE_DYN_01 ||
                    _G_info.cur_ef == USER_RGB_MODE_DYN_03 ||
                    _G_info.cur_ef == USER_RGB_MODE_MIPLAY ||
                    _G_info.cur_ef == USER_RGB_MODE_DYN_TTS
                  )
                  {
                      send_rgbbuf_to_spi(&_G_info); //发送一帧
                  }
        }

    }

    return  (void *)(0);
}

/*
 * Copyright (c) 2020 xiaomi.
 *
 * Unpublished copyright. All rights reserved. This material contains
 * proprietary information that should be used or copied only within
 * xiaomi, except with written permission of xiaomi.
 */

#ifndef __WS2812_APP_H__
#define __WS2812_APP_H__

#ifdef __cplusplus
extern "C" {
#endif
#include <mqueue.h>

//clock_gettime
#define  LED_NUM                        (8)
/***MODE_SPE****/
#define USER_RGB_MODE_MIX               (0)   //模式最小值
#define USER_RGB_MODE_BLUE_BREATH       (1)   //蓝色呼吸
#define USER_RGB_MODE_NO_SOUND_DETECT   (2)   //1 灯和 -1 灯常亮，之后超时返回上一灯效
#define USER_RGB_MODE_GREEN_STAY        (3)   //绿色常亮
#define USER_RGB_MODE_BLE_CONN_OK       (4)   //蓝牙连接成功，持续1s 后熄灭
#define USER_RGB_MODE_BT_DISCONNECTED   (5)   //蓝牙断开

#define USER_RGB_MODE_SPE_MAX           (7)   //特殊模式的最大id
/***MODE_NOR****/
#define USER_RGB_MODE_NOR_MIN           (9)   //用户模式最小id
#define USER_RGB_MODE_OFF               (10)  //关灯
#define USER_RGB_MODE_DYN_TTS           (11)  //tts拾音律动
#define USER_RGB_MODE_WALK_LE_AND_RI    (12)  //左右游走灯效，用在语意理解，文本tts生成
#define USER_RGB_MODE_SIDE_TO_CENTER    (13)  //两边向中间靠拢，用在语音唤醒时
#define USER_RGB_MODE_BOOT_SUCCESS      (14)  //开机成功，两侧灯珠向中心汇合，汇集完成后左右伸展至全部点亮后渐灭
#define USER_RGB_MODE_WHILT_BREATH      (15)  //白色呼吸,预加载使用
#define USER_RGB_MODE_INIT_CONFIG       (16)  //初始化/配置，wifi连接过程，单向从左向右游走，橙色,query 后等待tts下发
#define USER_RGB_MODE_OTA_UPGRADE       (17)  //监测到新版本，含安装包下载，自动升级,blue
#define USER_RGB_MODE_VOIP_DIAL         (18)  //VOIP通话，拨号中/来电响铃时
#define USER_RGB_MODE_NET_OFF           (19)  //音箱断网，橙色闪烁
#define USER_RGB_MODE_COMB_PLAY         (20)  //组合播放，呼吸单次
#define USER_RGB_MODE_PLAING_LOAD_SOURCE    (21)  //歌曲切换时加载资源。2.5s 呼吸一次

#define USER_RGB_MODE_VOIP_BUSY         (23)  //VOIP通话中
#define USER_RGB_MODE_SYSTEM_FAULT      (24)  //系统故障
#define USER_RGB_MODE_ALARM_ACTIVE      (25)  //闹钟激活
#define USER_RGB_MODE_MIC_MUTE          (26)  //mute mic
#define USER_RGB_MODE_OTA_UPGRADE_1     (27)  //监测到新版本，含安装包下载，自动升级,blue
#define USER_RGB_MODE_OPEN_BLE_DISCOVER (28)  //蓝牙可发现打开

#define USER_RGB_MODE_DYN_01            (29)  //播放律动
#define USER_RGB_MODE_DUMMY_RESUME      (30)  //假动作恢复上次
#define USER_RGB_MODE_DYN_02            (31)  //拾音律动
#define USER_RGB_MODE_DYN_03            (32)  //蓝牙律动
#define USER_RGB_MODE_MIAOBO_DYN        (33)  //妙播律动
#define USER_RGB_SET_PRE_CONFIG_MODE    (34)
#define USER_RGB_MODE_MIC_MUTE_OFF      (35)  //mute mic off
#define USER_RGB_MODE_NO_TTS_END_OFF    (36)
#define USER_RGB_MODE_WALK_LE_AND_RI_NO_REST    (37)

#define USER_RGB_MODE_DYN_01_BY_PASS    (38)  //播放律动
#define USER_RGB_MODE_DYN_02_BY_PASS    (39)  //拾音律动
#define USER_RGB_MODE_MIPLAY            (40)  //miplay 灯效


#define USER_RGB_MODE_MAX               (50)  //模式最大值
#define RGB_MODE_TABLE_SIZE             (52)

typedef enum {
    MODE_INIT,//模式 init
    MODE_RUN,//模式 run
    MODE_DYN,//模式 动态 dynamic
    MODE_MAX,
}MODE_FUN;

typedef struct node
{
    int data;
    struct node* next;
}Node;

typedef struct line_stack 
{
    Node* top;
    int len;
}Stack;

typedef enum {
    MODE_SPE,//特殊模式
    MODE_KEEP,//维持模式
    MODE_NOR,//用户模式
}MODE_CLASS;

#define MAX_H	(360)//色调最大值
#define MAX_S	(255)//饱和度最大值
#define MAX_V	(255)//明度最大值

#define MAX_H_F	(360.0f)  //色调最大浮点值
#define MAX_S_F	(255.0f) //饱和度最大浮点值
#define MAX_V_F	(255.0f) //明度最大浮点值
#define MAX_RGB_F (255.0f)//R\G\B最大取值

#define m_min(a, b, c)  (((a) < (b))? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)));
#define m_max(a, b, c)  (((a) > (b))? ((a) > (c) ? (a) : (c)) : ((b) > (c) ? (b) : (c)));
#define ofb_h(a) ( (a) >= 0 ? ((a) % MAX_H) : (MAX_H + (a) % MAX_H) )
#define ofb_s(a) ( (a) < 0 ? 0: ((a) >= MAX_S ? MAX_S : (a)))
#define ofb_v(a) ( (a) < 0 ? 0: ((a) >= MAX_V ? MAX_V : (a)))
//HSV 颜色
typedef struct _USER_COLOUR_HSV_{
   int h;//色调(H)
   int s;//饱和度(H)
   int v;//明度(V)
}COLOUR_HSV;
typedef struct db_map
{
    double db;
    int cnt;
    int v;
}DB_MAP;

typedef DB_MAP* ArrayPtr;

typedef struct rhythm_msg {
    COLOUR_HSV hsv;
    int        dis_num;
    ArrayPtr   pMap;
} RHYTHM_MSG;
typedef struct grad_change_msg 
{
    double rms;
    ArrayPtr pMap;
} GRAD_CHANGE_MSG;
typedef struct exec_msg {
    int type;
    int effect;
} EXEC_MSG;
typedef struct recv_energy_msg {
    double  energy;
} RECV_ENERGY_MSG;

typedef struct _RGB_MODE_INFO_{
    //mode
    unsigned long long  Special_mode_time;//特殊模式显示中止时间
    //优先级：特殊模式->维持特殊模式->普通模式
    unsigned char Special_mode;//特殊模式
    unsigned char Special_keep;//维持特殊模式
    unsigned char Normal_mode;//普通模式
    unsigned char Normal_table[RGB_MODE_TABLE_SIZE];//用户模式table
    unsigned char  index;//table index

    //scan
    timer_t id_m;//scan m id
    timer_t id_d;//scan d id
    timer_t id_nm;

    unsigned int timer_cm;//当前模式扫描时间
    unsigned int timer_cd;//当前频点，能量，动态扫描时间
    unsigned int time_m;//目标模式扫描时间
    unsigned int time_d;//目标频点、能量 动态扫描时间

    //mult data
    unsigned int mult_u32[10];
    int mult_int[10];
    long long mult_priv_data[10];

    int  (*(*run_table)[USER_RGB_MODE_MAX][MODE_MAX])(struct _RGB_MODE_INFO_ *);

    uint32_t   reserve0[LED_NUM];
    uint32_t   rgbBuff[LED_NUM];                       //每个灯一个 32bit 的rgb值 0x00rrggbb
    uint32_t   reserve1[LED_NUM];
    COLOUR_HSV hsv[LED_NUM];                           //每个灯单独的 hsv,用在律动过程中存储单个灯的 hsv 值

    //当前整体的 hsv 值，init 中设置该值，然后会在 run 中完成转换，并填充buff
    int  h;
    int  s;
    int  v;
    pthread_mutex_t  rgb_buf_mutex;
    pthread_mutex_t  info_para_mutex;
    /* 执行队列*/
    pthread_mutex_t  exec_mutex;

    Stack  *led_stack;
    int     cur_ef;

    bool    listruc_set;                               // 指令聆听状态
    bool    tts_rhy;

    /* 能量百分比*/
    double  cur_percent;
    pthread_mutex_t  cur_percent_mutex;

    bool  pl_enable;                                  //播放灯光使能

}RGB_MODE_INFO;

void env_setup(void);
int  set_rgb_mode_auto(int wang, RGB_MODE_INFO *info, int mode);
void trigger_play_data_process(void);
void destory_play_data_process(void);
void trigger_record_data_process(void);
void destory_record_data_process(void);
void destory_rrs_data_process(void);
int send_energy2rhythm(RECV_ENERGY_MSG  msg);
int effect_convert(int effect);
void led_effect_distribute_gateway(void* eff);
void led_effect_shut_gateway(void *eff);
int get_current_effect(void);
void set_listen_instruc(void);
void set_boot_success(void);

#ifdef __cplusplus
}
#endif

#endif /* __WS2812_APP_H__ */

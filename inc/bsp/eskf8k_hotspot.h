#ifndef ESKF8K_HOTSPOT_H
#define ESKF8K_HOTSPOT_H
#include <stdint.h>
#ifndef APP_ESKF8K_HOTSPOT
#define APP_ESKF8K_HOTSPOT 0
#endif
enum {
 H_DRDY_ENTRY,H_DRDY_STAMP,H_DRDY_STATS,H_DRDY_DMA,H_DRDY_TAIL,
 H_DMA_FLAGS,H_DMA_TRANSACTION,H_DMA_COPY,H_DMA_INSPECT,H_DMA_RING,H_DMA_CLEANUP,H_DMA_STATS,
 H_TEMP,H_CONVERT,H_CAL_FEED,H_RATE_FILTER,H_PREDICT,H_ACCEL,H_MAG,
 H_INS,H_TEMP_LEARN,H_MAG_IO,H_LIVE,H_EULER,H_ATTITUDE,H_PACK,H_CRC,
 H_USB_ENQUEUE,H_UART_ENQUEUE,H_COMMANDS,H_CAL_SERVICE,H_TEMP_SERVICE,H_WS2812,H_CAN,H_COMMON,H_EMPTY,H_COUNT
};
#if APP_ESKF8K_HOTSPOT
#ifndef APP_ESKF8K_HOT_GROUP
#define APP_ESKF8K_HOT_GROUP 0
#endif
/* Compile inactive scopes out, including call-site register spilling. */
#define HOT_SELECTED(id) ((id)==H_EMPTY || APP_ESKF8K_HOT_GROUP==0 || \
 (APP_ESKF8K_HOT_GROUP==1 && (id)<=H_DRDY_TAIL) || \
 (APP_ESKF8K_HOT_GROUP==2 && (id)>=H_DMA_FLAGS && (id)<=H_DMA_STATS) || \
 (APP_ESKF8K_HOT_GROUP==3 && (id)>=H_TEMP && (id)<=H_RATE_FILTER) || \
 (APP_ESKF8K_HOT_GROUP==4 && (id)>=H_PREDICT && (id)<=H_TEMP_LEARN) || \
 (APP_ESKF8K_HOT_GROUP==5 && (id)>=H_MAG_IO && (id)<=H_UART_ENQUEUE) || \
 (APP_ESKF8K_HOT_GROUP==6 && (id)>=H_COMMANDS && (id)<=H_COMMON))
#ifdef __cplusplus
extern "C" {
#endif
uint64_t eskf8k_cpu_exclusive_now(void);
uint64_t eskf8k_cpu_total_now(void);
uint64_t hot_begin_impl(unsigned id);
void hot_end_impl(unsigned id,uint64_t start);
void hot_end_acc(uint64_t start,unsigned observation_accepted);
#define hot_begin(id) (HOT_SELECTED(id)?hot_begin_impl(id):(uint64_t)0)
#define hot_end(id,start) do { if(HOT_SELECTED(id))hot_end_impl(id,start);else (void)(start); } while(0)
void hot_prepare(void);
void hot_sync(void);
#ifdef __cplusplus
}
#endif
#else
#define hot_begin(id) ((uint64_t)0)
#define hot_end(id,start) ((void)(start))
#define hot_prepare() ((void)0)
#define hot_sync() ((void)0)
#endif
#endif

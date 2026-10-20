/* Experimental full-application DMA capture. Derived from this repository's
 * measured diagnostic capture; not from the GPL reference project.
 * PB0/TMR3_CH3 timestamps actual DRDY edges; SPI1 DMA uses channels 2/3.
 * All overflow, stale, busy and edge-loss counters remain visible over SWD.
 */
#include "imu8k_capture.h"
#include "at32f423_dma.h"
#include "at32f423_spi.h"
#include "at32f423_scfg.h"
#include "eskf8k_hotspot.h"
#include "sram_capture.h"
/* The trimmed repository has no SCFG .c. This benchmark routes only PB0. */
static void imu8k_exint_line_config(scfg_port_source_type p,scfg_pins_source_type pin) {
    (void)pin;SCFG->exintc1=(SCFG->exintc1&~0x0fU)|(uint32_t)p;
}
#define scfg_exint_line_config imu8k_exint_line_config
int instrumented_read_raw(lsm6dsv_raw_t* r);
#define EXINT0_IRQHandler original_exint0
#define lsm6dsv_read_raw instrumented_read_raw
#include "lsm6dsv_8k.c"
#undef EXINT0_IRQHandler
#undef lsm6dsv_read_raw
#undef scfg_exint_line_config
#if APP_ESKF8K_VALIDATE
#define EXINT0_IRQHandler imu8k_exint_body
#define DMA1_Channel2_IRQHandler imu8k_dma_body
#endif

volatile capture_stats_t capture_stats;
volatile uint32_t capture_registers[6];
#if APP_SRAM_CAPTURE
extern volatile unsigned eskf8k_counts[8];
/* Publish at most 10 Hz so a slow SWD reader can obtain a coherent record.
 * [sequence,produced,consumed,gyro pairs,producer hash,consumer hash,
 *  last DRDY timestamp,check DWT,error]. All math has finished at this call. */
volatile uint32_t sram_empty_snapshot[9];
static uint32_t sram_gyro_origin,sram_last_publish;
#endif
volatile uint32_t capture_edge[135],capture_timer_period_n;
volatile uint64_t capture_edge_latency_sum,capture_timer_period_sum;
#define BTMR(off) REG32(TMR3_BASE+(off))
static uint32_t last_capture;
static void edge_timer_begin(void) {
    gpio_init_type g;gpio_default_para_init(&g);g.gpio_mode=GPIO_MODE_MUX;g.gpio_pull=GPIO_PULL_DOWN;g.gpio_pins=GPIO_PINS_0;gpio_init(GPIOB,&g);gpio_pin_mux_config(GPIOB,GPIO_PINS_SOURCE0,GPIO_MUX_2);
    crm_periph_clock_enable(CRM_TMR3_PERIPH_CLOCK,TRUE);
    crm_periph_reset(CRM_TMR3_PERIPH_RESET,TRUE);crm_periph_reset(CRM_TMR3_PERIPH_RESET,FALSE);
    /* APB1=75 MHz, timer x2=150 MHz. Runtime period ratio is checked by host. */
    BTMR(0x00)=0;BTMR(0x0c)=0;BTMR(0x28)=0;BTMR(0x2c)=0xffff;
    BTMR(0x1c)=1; /* CC3 input mapped directly to TI3, no prescaler/filter. */
    BTMR(0x20)=1U<<8; /* CH3 enabled, rising edge. */
    BTMR(0x14)=1;BTMR(0x24)=0;BTMR(0x10)=0;BTMR(0x00)=1;
    memset((void*)capture_edge,0,sizeof(capture_edge));capture_edge_latency_sum=0;capture_timer_period_sum=0;capture_timer_period_n=0;last_capture=0;
}
static RAM_CODE uint32_t edge_stamp(uint32_t irq_entry) {
    uint32_t flags=BTMR(0x10),edge=BTMR(0x3c)&0xffff;
    uint32_t before=dwt_cycles(),now=BTMR(0x24)&0xffff,after=dwt_cycles();
    BTMR(0x10)=~(8U|0x800U);
    uint32_t bracket=after-before;
    if(bracket>capture_edge[5])capture_edge[5]=bracket;
    if(!(flags&8U)){capture_edge[1]++;return irq_entry;}
    uint32_t ticks=(now-edge)&0xffff;
    uint32_t stamp=before+bracket/2U-ticks;
    uint32_t latency=(uint32_t)(irq_entry-stamp);
    if(latency>system_core_clock/1000U){capture_edge[1]++;return irq_entry;}
    if(capture_edge[0] && !(flags&0x800U)) {
        capture_timer_period_sum+=(edge-last_capture)&0xffff;capture_timer_period_n++;
    }
    if(capture_edge[0] && (flags&0x800U))capture_edge[2]++;
    if(!capture_edge[0]||latency<capture_edge[3])capture_edge[3]=latency;
    if(latency>capture_edge[4])capture_edge[4]=latency;
    capture_edge[0]++;capture_edge_latency_sum+=latency;
    unsigned b=latency/64;if(b>127)b=127;capture_edge[7+b]++;
    last_capture=edge;return stamp;
}
static unsigned transport;
static uint16_t dma_tx[9]={0x9e00,0,0,0,0,0,0,0,0};
#if APP_ESKF_CAPTURE_OPT
static volatile uint16_t dma_rx[9];
#else
static uint16_t dma_rx[9];
#endif
static volatile uint32_t dma_busy,dma_stamp,dma_transfer_start;
static uint32_t last_edge;
static lsm6dsv_raw_t last_raw;
static int have_raw;
static RAM_CODE void histogram(volatile uint32_t h[128],uint32_t v){unsigned b=v/64;if(b>127)b=127;h[b]++;}
static RAM_CODE void spi_cost(uint32_t c){if(!capture_stats.spi_n||c<capture_stats.spi_min)capture_stats.spi_min=c;if(c>capture_stats.spi_max)capture_stats.spi_max=c;capture_stats.spi_n++;capture_stats.spi_sum+=c;histogram(capture_stats.spi_hist,c);}
static RAM_CODE void inspect_raw(const lsm6dsv_raw_t* r) {
#if APP_ESKF_CAPTURE_OPT
    if(have_raw && last_raw.gyr[0]==r->gyr[0] && last_raw.gyr[1]==r->gyr[1] && last_raw.gyr[2]==r->gyr[2])capture_stats.repeated_raw_n++;
    for(unsigned i=0;i<3;i++)last_raw.gyr[i]=r->gyr[i];
    have_raw=1;
#else
    if(have_raw&&memcmp(last_raw.gyr,r->gyr,sizeof(r->gyr))==0)capture_stats.repeated_raw_n++;
    last_raw=*r;have_raw=1;
#endif
}
int instrumented_read_raw(lsm6dsv_raw_t* r) {
    if(!active)return lsm6dsv_read_raw(r);
    uint8_t status=0;
    if(lsm6dsv_read_reg(0x1e,&status)||!(status&2U))capture_stats.stale_n++;
    uint32_t start=dwt_cycles();int rc=lsm6dsv_read_raw(r);spi_cost(dwt_cycles()-start);
    if(!rc){capture_stats.valid_n++;inspect_raw(r);}return rc;
}
RAM_CODE void EXINT0_IRQHandler(void) {
    uint64_t h=hot_begin(H_DRDY_ENTRY);
    uint32_t start=dwt_cycles();
    if(!(EXINT_PENDING&LINE0)){hot_end(H_DRDY_ENTRY,h);return;}
    hot_end(H_DRDY_ENTRY,h);h=hot_begin(H_DRDY_STAMP);
    uint32_t stamp=edge_stamp(start);
    hot_end(H_DRDY_STAMP,h);h=hot_begin(H_DRDY_STATS);
    if(last_edge){uint32_t d=stamp-last_edge;capture_stats.interval_n++;capture_stats.interval_sum+=d;if(!capture_stats.min_interval||d<capture_stats.min_interval)capture_stats.min_interval=d;if(d>capture_stats.max_interval)capture_stats.max_interval=d;if(d>system_core_clock/8000U*3U/2U){capture_stats.gap_n++;capture_edge[6]+=(d+system_core_clock/8000U/2U)/(system_core_clock/8000U)-1U;}}
    last_edge=stamp;capture_stats.irq_n++;
    hot_end(H_DRDY_STATS,h);h=hot_begin(H_DRDY_DMA);
#if !APP_SRAM_CAPTURE
    if(transport!=2) {
        uint32_t head=write_n;original_exint0();
        /* Single core: foreground cannot pop until this ISR returns. */
        if(write_n!=head){ring[head&RING_MASK].cycles=stamp;__DMB();}
    }
    else {
#else
    {
#endif
        EXINT_PENDING=LINE0;
        if(active) {
            irq_n++;
            if(dma_busy)capture_stats.busy_n++;
            else {
                dma_busy=1;dma_stamp=stamp;
#if APP_ESKF_CAPTURE_OPT
                /* SDK ctrl2 DMAR/DMAW bits 0/1, channel enable bit 0.
                 * Only CH2/3 flags are cleared; UART CH1/4 are independent. */
                SPI1->ctrl2 &= ~3U;
                while(SPI1->sts&SPI_I2S_RDBF_FLAG)(void)SPI1->dt;
                DMA1_CHANNEL2->ctrl &= ~1U;DMA1_CHANNEL3->ctrl &= ~1U;
                DMA1->clr=DMA1_FDT2_FLAG|DMA1_DTERR2_FLAG|DMA1_FDT3_FLAG|DMA1_DTERR3_FLAG;
                DMA1_CHANNEL2->dtcnt=9;DMA1_CHANNEL3->dtcnt=9;
                GPIOA->clr=GPIO_PINS_4;
                DMA1_CHANNEL2->ctrl |= 1U;DMA1_CHANNEL3->ctrl |= 1U;
                dma_transfer_start=dwt_cycles();SPI1->ctrl2 |= 3U;
#else
                spi_i2s_dma_receiver_enable(SPI1,FALSE);spi_i2s_dma_transmitter_enable(SPI1,FALSE);
                while(SPI1->sts&SPI_I2S_RDBF_FLAG)(void)SPI1->dt;
                dma_channel_enable(DMA1_CHANNEL2,FALSE);dma_channel_enable(DMA1_CHANNEL3,FALSE);
                dma_flag_clear(DMA1_FDT2_FLAG|DMA1_DTERR2_FLAG|DMA1_FDT3_FLAG|DMA1_DTERR3_FLAG);
                dma_data_number_set(DMA1_CHANNEL2,9);dma_data_number_set(DMA1_CHANNEL3,9);
                gpio_bits_reset(GPIOA,GPIO_PINS_4);
                dma_channel_enable(DMA1_CHANNEL2,TRUE);dma_channel_enable(DMA1_CHANNEL3,TRUE);
                dma_transfer_start=dwt_cycles();spi_i2s_dma_receiver_enable(SPI1,TRUE);spi_i2s_dma_transmitter_enable(SPI1,TRUE);
#endif
            }
        }
    }
    hot_end(H_DRDY_DMA,h);h=hot_begin(H_DRDY_TAIL);
    uint32_t depth=write_n-read_n;if(depth>capture_stats.max_depth)capture_stats.max_depth=depth;
    uint32_t c=dwt_cycles()-start;capture_stats.irq_body_n++;capture_stats.irq_body_sum+=c;if(c>capture_stats.irq_body_max)capture_stats.irq_body_max=c;histogram(capture_stats.irq_hist,c);
    hot_end(H_DRDY_TAIL,h);
}
RAM_CODE void DMA1_Channel2_IRQHandler(void) {
    uint64_t h=hot_begin(H_DMA_FLAGS);
    uint32_t start=dwt_cycles();
    int valid=1;
#if APP_ESKF_CAPTURE_OPT
    uint32_t flags=DMA1->sts;
    if(flags&(DMA1_DTERR2_FLAG|DMA1_DTERR3_FLAG)){spi_error_n++;capture_stats.spi_error_n++;valid=0;}
#else
    if(dma_flag_get(DMA1_DTERR2_FLAG)||dma_flag_get(DMA1_DTERR3_FLAG)){spi_error_n++;capture_stats.spi_error_n++;valid=0;}
#endif
    hot_end(H_DMA_FLAGS,h);
#if APP_ESKF_CAPTURE_OPT
    if(flags&DMA1_FDT2_FLAG) {
#else
    if(dma_flag_get(DMA1_FDT2_FLAG)) {
#endif
        h=hot_begin(H_DMA_TRANSACTION);
        /* RX complete means last word received. Bound the bus-idle wait. */
        uint32_t limit=start+system_core_clock/1000000U*5U;
        while((SPI1->sts&SPI_I2S_BF_FLAG)&&(int32_t)(dwt_cycles()-limit)<0){}
        if(SPI1->sts&SPI_I2S_BF_FLAG){spi_error_n++;capture_stats.spi_error_n++;valid=0;}
        hot_end(H_DMA_TRANSACTION,h);h=hot_begin(H_DMA_STATS);
        spi_cost(dwt_cycles()-dma_transfer_start);
        hot_end(H_DMA_STATS,h);h=hot_begin(H_DMA_COPY);
#if APP_ESKF_CAPTURE_OPT
        /* SPI MSB-first halfwords: TEMP low is byte 3, high is byte 4.
         * Decode the exact original byte stream without an 18-byte staging copy. */
        lsm6dsv_raw_t r;
        r.temp_raw=(int16_t)((dma_rx[1]&255U)|(dma_rx[2]&0xff00U));
        for(unsigned i=0;i<3;i++)r.gyr[i]=(int16_t)((dma_rx[i+2]&255U)|(dma_rx[i+3]&0xff00U));
        for(unsigned i=0;i<3;i++)r.acc[i]=(int16_t)((dma_rx[i+5]&255U)|(dma_rx[i+6]&0xff00U));
        if(!(dma_rx[0]&2U)){capture_stats.stale_n++;valid=0;}
#else
        uint8_t b[18];for(unsigned i=0;i<9;i++){b[2*i]=(uint8_t)(dma_rx[i]>>8);b[2*i+1]=(uint8_t)dma_rx[i];}
        lsm6dsv_raw_t r;memcpy(&r,b+3,14); /* command echo, STATUS, reserved 1F, TEMP..ACC */
        if(!(b[1]&2U)){capture_stats.stale_n++;valid=0;}
#endif
        hot_end(H_DMA_COPY,h);
        if(valid){
            h=hot_begin(H_DMA_INSPECT);
            capture_stats.valid_n++;inspect_raw(&r);
            hot_end(H_DMA_INSPECT,h);h=hot_begin(H_DMA_RING);
#if APP_ESKF_CAPTURE_OPT
            uint32_t head=write_n;
            if(head-read_n>=RING_LEN)overrun_n++;
            else {ring[head&RING_MASK].raw=r;ring[head&RING_MASK].cycles=dma_stamp;
#if APP_SRAM_CAPTURE
                sram_live[SR_PRODUCED_HASH]=sram_sample_hash(sram_live[SR_PRODUCED_HASH],&r,dma_stamp);
#endif
                __DMB();write_n=head+1U;}
#else
            if(write_n-read_n>=RING_LEN)overrun_n++;
            else {ring[write_n&RING_MASK].raw=r;ring[write_n&RING_MASK].cycles=dma_stamp;__DMB();write_n++;}
#endif
            hot_end(H_DMA_RING,h);
        }
        uint32_t depth=write_n-read_n;if(depth>capture_stats.max_depth)capture_stats.max_depth=depth;
    }
    h=hot_begin(H_DMA_CLEANUP);
#if APP_ESKF_CAPTURE_OPT
    SPI1->ctrl2 &= ~3U;
    DMA1_CHANNEL2->ctrl &= ~1U;DMA1_CHANNEL3->ctrl &= ~1U;
    GPIOA->scr=GPIO_PINS_4;
    DMA1->clr=DMA1_FDT2_FLAG|DMA1_DTERR2_FLAG|DMA1_FDT3_FLAG|DMA1_DTERR3_FLAG;dma_busy=0;
#else
    spi_i2s_dma_receiver_enable(SPI1,FALSE);spi_i2s_dma_transmitter_enable(SPI1,FALSE);
    dma_channel_enable(DMA1_CHANNEL2,FALSE);dma_channel_enable(DMA1_CHANNEL3,FALSE);
    gpio_bits_set(GPIOA,GPIO_PINS_4);
    dma_flag_clear(DMA1_FDT2_FLAG|DMA1_DTERR2_FLAG|DMA1_FDT3_FLAG|DMA1_DTERR3_FLAG);dma_busy=0;
#endif
    hot_end(H_DMA_CLEANUP,h);h=hot_begin(H_DMA_STATS);
    uint32_t c=dwt_cycles()-start;capture_stats.dma_body_n++;capture_stats.dma_body_sum+=c;if(c>capture_stats.dma_body_max)capture_stats.dma_body_max=c;histogram(capture_stats.dma_hist,c);
    hot_end(H_DMA_STATS,h);
}
int imu8k_begin(unsigned gyro_range,unsigned acc_range) {
    imu8k_end();transport=2;lsm6dsv_spi_init();
    if(lsm6dsv_init_2khz_ranges(gyro_range,acc_range))return -1;
    if(lsm6dsv_write_reg(0x10,0)||lsm6dsv_write_reg(0x11,0)||lsm6dsv_write_reg(0x62,1)||lsm6dsv_write_reg(0x13,0x0a)||lsm6dsv_write_reg(0x10,0x1c)||lsm6dsv_write_reg(0x11,0x1c))return -2;
    delay_ms(30);
    const uint8_t addresses[6]={0x0f,0x10,0x11,0x62,0x13,0x0d};
    for(unsigned i=0;i<6;i++){uint8_t v=0;if(lsm6dsv_read_reg(addresses[i],&v))return -3;capture_registers[i]=v;}
    if(capture_registers[0]!=0x70||capture_registers[1]!=0x1c||capture_registers[2]!=0x1c||capture_registers[3]!=1)return -4;
    if(transport) {spi_enable(SPI1,FALSE);SPI1->ctrl1_bit.mdiv_l=SPI_MCLK_DIV_8;spi_enable(SPI1,TRUE);}
    memset((void*)&capture_stats,0,sizeof(capture_stats));last_edge=0;have_raw=0;dma_busy=0;
#if APP_SRAM_CAPTURE
    sram_live[SR_FRAME_BYTES]=sizeof(sample_t);sram_live[SR_RING_CAPACITY]=RING_LEN;
    sram_live[SR_PRODUCED_HASH]=sram_live[SR_CONSUMED_HASH]=2166136261U;
    sram_gyro_origin=eskf8k_counts[0];sram_last_publish=0;
#endif
    if(transport==2) {
        dma_init_type d;crm_periph_clock_enable(CRM_DMA1_PERIPH_CLOCK,TRUE);dmamux_enable(DMA1,TRUE);
        dma_reset(DMA1_CHANNEL2);dma_reset(DMA1_CHANNEL3);
        dmamux_init(DMA1MUX_CHANNEL2,DMAMUX_DMAREQ_ID_SPI1_RX);dmamux_init(DMA1MUX_CHANNEL3,DMAMUX_DMAREQ_ID_SPI1_TX);
        dma_default_para_init(&d);d.peripheral_base_addr=(uint32_t)&SPI1->dt;d.memory_base_addr=(uint32_t)dma_rx;d.direction=DMA_DIR_PERIPHERAL_TO_MEMORY;d.buffer_size=9;d.peripheral_inc_enable=FALSE;d.memory_inc_enable=TRUE;d.peripheral_data_width=DMA_PERIPHERAL_DATA_WIDTH_HALFWORD;d.memory_data_width=DMA_MEMORY_DATA_WIDTH_HALFWORD;d.priority=DMA_PRIORITY_VERY_HIGH;d.loop_mode_enable=FALSE;dma_init(DMA1_CHANNEL2,&d);
        d.memory_base_addr=(uint32_t)dma_tx;d.direction=DMA_DIR_MEMORY_TO_PERIPHERAL;dma_init(DMA1_CHANNEL3,&d);
        dma_interrupt_enable(DMA1_CHANNEL2,DMA_FDT_INT|DMA_DTERR_INT,TRUE);nvic_irq_enable(DMA1_Channel2_IRQn,0,0);
        /* Keep requests OFF for capture_start's polled dummy read. Otherwise
         * DMAMUX can retain a stale RX request, shifting every DMA packet. */
    }
    edge_timer_begin();lsm6dsv_8k_capture_start();return 0;
}
void imu8k_end(void) {
    lsm6dsv_8k_capture_stop();
    uint32_t deadline=dwt_cycles()+system_core_clock/1000U;
    while(dma_busy && (int32_t)(dwt_cycles()-deadline)<0){}
    if(dma_busy)spi_error_n++;
    dma_channel_enable(DMA1_CHANNEL2,FALSE);dma_channel_enable(DMA1_CHANNEL3,FALSE);nvic_irq_disable(DMA1_Channel2_IRQn);spi_i2s_dma_receiver_enable(SPI1,FALSE);spi_i2s_dma_transmitter_enable(SPI1,FALSE);gpio_bits_set(GPIOA,GPIO_PINS_4);dma_busy=0;BTMR(0x00)=0;
    capture_stats.overrun_n=overrun_n;capture_stats.spi_error_n=spi_error_n;
}
int imu8k_pop(lsm6dsv_raw_t* r,uint32_t* t) {int n=lsm6dsv_8k_pop(r,t);if(n)capture_stats.consumed_n++;return n;}
RAM_CODE int imu8k_pending(void){return (int)(write_n-read_n);}
#if APP_SRAM_CAPTURE
void imu8k_hash_check(void) {
    if(!active)return;
    uint32_t mask=__get_PRIMASK();__disable_irq();
    if(write_n==read_n){
        uint32_t gyro_n=eskf8k_counts[0]-sram_gyro_origin,now=dwt_cycles();
        uint32_t error=(sram_live[SR_PRODUCED_HASH]!=sram_live[SR_CONSUMED_HASH] || gyro_n!=read_n);
        sram_live[SR_EMPTY_CHECKS]++;if(error)sram_live[SR_HASH_ERRORS]++;
        if(!sram_empty_snapshot[0] || (uint32_t)(now-sram_last_publish)>=system_core_clock/10U){
            sram_empty_snapshot[0]++;__DMB();
            sram_empty_snapshot[1]=write_n;sram_empty_snapshot[2]=read_n;sram_empty_snapshot[3]=gyro_n;
            sram_empty_snapshot[4]=sram_live[SR_PRODUCED_HASH];sram_empty_snapshot[5]=sram_live[SR_CONSUMED_HASH];
            sram_empty_snapshot[6]=read_n?ring[(read_n-1U)&RING_MASK].cycles:0;
            sram_empty_snapshot[7]=now;sram_empty_snapshot[8]=error;
            __DMB();sram_empty_snapshot[0]++;sram_last_publish=now;
        }
    }
    __set_PRIMASK(mask);
}
#endif

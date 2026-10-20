#include "gyro_temp_learning.h"
#include "gyro_temp_comp.h"
#include "app_config.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

#define DECIMATE (APP_CAL_FEED_HZ/APP_TEMP_LEARN_HZ)
#define CAPTURE_BLOCKS (APP_TEMP_LEARN_CAPTURE_MS/1000U)
_Static_assert(APP_CAL_FEED_HZ%APP_TEMP_LEARN_HZ==0U,"learning decimation");
_Static_assert(APP_TEMP_LEARN_CAPTURE_MS%1000U==0U,"whole learning blocks");
_Static_assert(APP_TEMP_LEARN_REST_MS%1000U==0U,"whole rest blocks");
_Static_assert(APP_TEMP_LEARN_TEMP_RATE_BLOCKS>=3U,"temperature trend window");
_Static_assert(offsetof(ahrs_payload_temp_learn_status_t,uptime_ms)==56U,"status offset");
_Static_assert(offsetof(ahrs_payload_temp_learn_page_t,entry)==10U,"page offset");
typedef struct { float mean[3], sigma[3], temp; } observation_block_t;
static observation_block_t blocks[CAPTURE_BLOCKS];
static uint32_t episode, bin_episode[TEMP_BIN_COUNT], last_us;
static uint32_t rest_points, capture_points;
static uint32_t rest_false_samples, rest_pause_samples, round_start_us;
static unsigned decimate_n, block_n, collected_blocks;
static uint8_t enabled, phase, last_reject, have_sample, have_raw, round_active;
static uint8_t motion_latched, finished_episode, safe_to_save, pending_save, saving;
static float sum_raw[3], sum_seed[3], sum_corrected[3], sum_acc[3], sum_temp;
static float block_mean[3], block_m2[3], block_temp;
static float previous_raw[3], acc_reference[3], reference_mean[3];
static float current_temp, learning_temp, temp_min, temp_max, previous_block_temp;
static uint8_t have_block_temp, have_reference_mean;
static float temp_history[APP_TEMP_LEARN_TEMP_RATE_BLOCKS];
static float time_history[APP_TEMP_LEARN_TEMP_RATE_BLOCKS];
static unsigned temp_history_n;

static void clear_decimation(void)
{
  decimate_n=0U; sum_temp=0.f;
  memset(sum_raw,0,sizeof(sum_raw)); memset(sum_seed,0,sizeof(sum_seed));
  memset(sum_corrected,0,sizeof(sum_corrected)); memset(sum_acc,0,sizeof(sum_acc));
}
static void clear_block(void)
{
  block_n=0U; block_temp=0.f;
  memset(block_mean,0,sizeof(block_mean)); memset(block_m2,0,sizeof(block_m2));
}
static void cancel_round(uint8_t reason, int motion)
{
  if(motion&&!motion_latched) { episode++; motion_latched=1U; finished_episode=0U; }
  round_active=0U; rest_points=capture_points=0U; collected_blocks=0U;
  have_block_temp=have_reference_mean=0U; learning_temp=NAN;
  rest_pause_samples=0U; temp_history_n=0U;
  clear_decimation(); clear_block(); last_reject=reason;
  phase=enabled?TEMP_WAIT:TEMP_PAUSED;
}
void gyro_temp_learning_init(void)
{
  enabled=(uint8_t)(APP_GYR_TEMP_LEARNING_ENABLE&&APP_GYR_TEMP_LEARNING_DEFAULT);
#ifdef APP_USER_BL_UPDATE
  enabled=0U;
#endif
  episode=0U; memset(bin_episode,0,sizeof(bin_episode));
  have_sample=have_raw=motion_latched=finished_episode=0U;
  rest_false_samples=0U;
  safe_to_save=pending_save=saving=0U; current_temp=gyro_temp_comp_boot_temperature();
  cancel_round(TEMP_REJECT_NONE,0);
}
void gyro_temp_learning_gap(void)
{
  /* Missing samples never constitute an independent stationary episode. */
  have_sample=have_raw=0U; cancel_round(TEMP_REJECT_GAP,0);
  rest_false_samples=0U;
}
static unsigned temp_bin(float t) { return (unsigned)((t+40.f)/5.f+0.5f); }
static float trimmed_mean(unsigned axis)
{
  float values[CAPTURE_BLOCKS];
  for(unsigned i=0;i<CAPTURE_BLOCKS;++i) {
    float x=blocks[i].mean[axis]; unsigned j=i;
    while(j&&values[j-1U]>x) { values[j]=values[j-1U]; --j; }
    values[j]=x;
  }
  unsigned trim=CAPTURE_BLOCKS/10U;
  float sum=0.f;
  for(unsigned i=trim;i<CAPTURE_BLOCKS-trim;++i) sum+=values[i];
  return sum/(float)(CAPTURE_BLOCKS-2U*trim);
}
static void accept_observation(void)
{
  temp_bin_t observed={0};
  float temperature_sum=0.f;
  for(unsigned i=0;i<CAPTURE_BLOCKS;++i) {
    temperature_sum+=blocks[i].temp;
    for(unsigned a=0;a<3;++a) observed.sigma_dps[a]+=blocks[i].sigma[a]*blocks[i].sigma[a]/(float)CAPTURE_BLOCKS;
  }
  observed.temperature_c=temperature_sum/(float)CAPTURE_BLOCKS;
  if(!isfinite(observed.temperature_c)||observed.temperature_c<APP_TEMP_LEARN_MIN_C||
     observed.temperature_c>APP_TEMP_LEARN_MAX_C)
    { cancel_round(TEMP_REJECT_COVERAGE,0); return; }
  for(unsigned a=0;a<3;++a) {
    observed.bias_dps[a]=trimmed_mean(a);
    observed.sigma_dps[a]=sqrtf(observed.sigma_dps[a]);
    if(fabsf(observed.bias_dps[a])>APP_TEMP_LEARN_MAX_BIAS_DPS)
      { cancel_round(TEMP_REJECT_NOISE,0); return; }
  }
  unsigned index=temp_bin(observed.temperature_c);
  /* A crossing bin boundary within one window is not a usable observation. */
  if(index>=TEMP_BIN_COUNT||temp_bin(temp_min)!=index||temp_bin(temp_max)!=index)
    { cancel_round(TEMP_REJECT_COVERAGE,0); return; }
  temp_bin_t old=gyro_temp_comp_working()->bins[index];
  learning_temp=observed.temperature_c;
  if(old.flags!=TEMP_BIN_EMPTY&&bin_episode[index]==episode) {
    finished_episode=1U; round_active=0U; phase=TEMP_CANDIDATE; return;
  }
  int conflict=0;
  for(unsigned a=0;a<3;++a)
    if(old.flags!=TEMP_BIN_EMPTY&&fabsf(observed.bias_dps[a]-old.bias_dps[a])>APP_TEMP_LEARN_CONFLICT_DPS) conflict=1;
  if(conflict) {
    old.flags=TEMP_BIN_QUARANTINE;
    gyro_temp_comp_set_bin(index,&old); last_reject=TEMP_REJECT_CONFLICT;
  } else if(old.flags==TEMP_BIN_EMPTY||old.flags==TEMP_BIN_QUARANTINE) {
    observed.flags=TEMP_BIN_CANDIDATE; observed.accepted_windows=1U;
    gyro_temp_comp_set_bin(index,&observed); last_reject=TEMP_REJECT_NONE;
  } else {
    float gain=old.flags==TEMP_BIN_CONFIRMED?APP_TEMP_LEARN_UPDATE_ALPHA:0.5f;
    if(gain>0.2f&&old.flags==TEMP_BIN_CONFIRMED) gain=0.2f;
    if(gain<0.f) gain=0.f;
    old.temperature_c+=gain*(observed.temperature_c-old.temperature_c);
    for(unsigned a=0;a<3;++a) {
      old.bias_dps[a]+=gain*(observed.bias_dps[a]-old.bias_dps[a]);
      old.sigma_dps[a]+=gain*(observed.sigma_dps[a]-old.sigma_dps[a]);
    }
    if(old.accepted_windows<UINT16_MAX) old.accepted_windows++;
    old.flags=TEMP_BIN_CONFIRMED; gyro_temp_comp_set_bin(index,&old); last_reject=TEMP_REJECT_NONE;
  }
  bin_episode[index]=episode; finished_episode=1U; round_active=0U;
  phase=old.flags==TEMP_BIN_CONFIRMED&&!conflict?TEMP_WAIT:TEMP_CANDIDATE;
}
static void feed_100hz(const float gyro[3],const float seed[3],const float corrected[3],
                       const float acc[3],float temp)
{
  float s2=0.f,c2=0.f;
  for(unsigned a=0;a<3;++a) { s2+=seed[a]*seed[a]; c2+=corrected[a]*corrected[a]; }
  if(s2>APP_TEMP_LEARN_RESIDUAL_DPS*APP_TEMP_LEARN_RESIDUAL_DPS||
     c2>APP_TEMP_LEARN_RESIDUAL_DPS*APP_TEMP_LEARN_RESIDUAL_DPS)
    { cancel_round(TEMP_REJECT_MOTION,1); return; }
  float a2=0.f,direction2=0.f;
  for(unsigned a=0;a<3;++a) {
    a2+=acc[a]*acc[a];
    if(round_active) { float d=acc[a]-acc_reference[a]; direction2+=d*d; }
  }
  const float lo=9.80665f-APP_TEMP_LEARN_ACC_DEV_MS2,hi=9.80665f+APP_TEMP_LEARN_ACC_DEV_MS2;
  if(a2<lo*lo||a2>hi*hi||direction2>APP_TEMP_LEARN_ACC_DIRECTION_MS2*APP_TEMP_LEARN_ACC_DIRECTION_MS2)
    { cancel_round(TEMP_REJECT_MOTION,1); return; }
  if(finished_episode) return;
  if(!round_active) {
    round_active=1U; temp_min=temp_max=temp; memcpy(acc_reference,acc,12U);
    round_start_us=last_us;
    phase=TEMP_REST_CONFIRM; clear_block();
  }
  if(temp<temp_min) temp_min=temp;
  if(temp>temp_max) temp_max=temp;
  if(temp_max-temp_min>APP_TEMP_LEARN_TEMP_SPAN_C)
    { cancel_round(TEMP_REJECT_TEMP_UNSTABLE,0); return; }
  learning_temp=temp;
  ++block_n;
  block_temp+=(temp-block_temp)/(float)block_n;
  for(unsigned a=0;a<3;++a) {
    float d=gyro[a]-block_mean[a]; block_mean[a]+=d/(float)block_n;
    block_m2[a]+=d*(gyro[a]-block_mean[a]);
  }
  if(phase==TEMP_REST_CONFIRM) rest_points++; else capture_points++;
  if(block_n<APP_TEMP_LEARN_HZ) return;
  if(have_block_temp&&fabsf(block_temp-previous_block_temp)>APP_TEMP_LEARN_TEMP_STEP_C)
    { cancel_round(TEMP_REJECT_TEMP_UNSTABLE,0); return; }
  previous_block_temp=block_temp; have_block_temp=1U;
  /* Least-squares trend of five one-second block means. Actual elapsed
   * time includes excluded VQF dropouts, so pauses cannot inflate the rate. */
  if(temp_history_n==APP_TEMP_LEARN_TEMP_RATE_BLOCKS) {
    memmove(temp_history,temp_history+1,(APP_TEMP_LEARN_TEMP_RATE_BLOCKS-1U)*sizeof(float));
    memmove(time_history,time_history+1,(APP_TEMP_LEARN_TEMP_RATE_BLOCKS-1U)*sizeof(float));
    temp_history_n--;
  }
  temp_history[temp_history_n]=block_temp;
  time_history[temp_history_n++]=(float)(uint32_t)(last_us-round_start_us)*1.e-6f;
  if(temp_history_n==APP_TEMP_LEARN_TEMP_RATE_BLOCKS) {
    float mean_t=0.f,mean_temp=0.f,cov=0.f,var=0.f;
    for(unsigned i=0;i<temp_history_n;++i) {mean_t+=time_history[i];mean_temp+=temp_history[i];}
    mean_t/=(float)temp_history_n; mean_temp/=(float)temp_history_n;
    for(unsigned i=0;i<temp_history_n;++i) {
      float dt=time_history[i]-mean_t; cov+=dt*(temp_history[i]-mean_temp);var+=dt*dt;
    }
    if(var<=0.f||fabsf(cov/var)>APP_TEMP_LEARN_TEMP_RATE_C_S)
      { cancel_round(TEMP_REJECT_TEMP_UNSTABLE,0); return; }
  }
  observation_block_t block;
  block.temp=block_temp;
  for(unsigned a=0;a<3;++a) {
    block.mean[a]=block_mean[a]; block.sigma[a]=sqrtf(block_m2[a]/(float)(block_n-1U));
    if(block.sigma[a]>APP_TEMP_LEARN_SIGMA_DPS)
      { cancel_round(TEMP_REJECT_NOISE,0); return; }
    if(have_reference_mean&&fabsf(block.mean[a]-reference_mean[a])>APP_TEMP_LEARN_BLOCK_DRIFT_DPS)
      { cancel_round(TEMP_REJECT_NOISE,0); return; }
  }
  if(!have_reference_mean) { memcpy(reference_mean,block.mean,12U); have_reference_mean=1U; }
  motion_latched=0U; /* one complete qualified second ends the movement episode */
  if(phase==TEMP_REST_CONFIRM) {
    if(rest_points>=APP_TEMP_LEARN_REST_MS*APP_TEMP_LEARN_HZ/1000U) {
      unsigned index=temp_bin(temp);
      phase=index<TEMP_BIN_COUNT&&gyro_temp_comp_working()->bins[index].flags!=TEMP_BIN_EMPTY?TEMP_VERIFY:TEMP_CAPTURE;
    }
  } else {
    blocks[collected_blocks++]=block;
    if(collected_blocks==CAPTURE_BLOCKS) accept_observation();
  }
  clear_block();
}
void gyro_temp_learning_sample(uint32_t now_ms,uint32_t us,const float raw[3],
    const float seed[3],const float corrected[3],const float acc[3],float temp,
    uint8_t rest,uint8_t blocked)
{
  (void)now_ms;
  current_temp=temp;
  if(have_sample&&(uint32_t)(us-last_us)>APP_TEMP_LEARN_GAP_US) gyro_temp_learning_gap();
  last_us=us; have_sample=1U;
  if(!enabled||blocked||saving) {
    cancel_round(TEMP_REJECT_NONE,0); phase=TEMP_PAUSED; have_raw=0U; return;
  }
  if(!isfinite(temp)||temp<APP_TEMP_LEARN_MIN_C||temp>APP_TEMP_LEARN_MAX_C)
    { cancel_round(TEMP_REJECT_COVERAGE,0); return; }
  float a2=0.f,direction2=0.f,seed2=0.f,corrected2=0.f;
  for(unsigned a=0;a<3;++a) {
    if(!isfinite(raw[a])||!isfinite(seed[a])||!isfinite(corrected[a])||!isfinite(acc[a]))
      { have_raw=0U; cancel_round(TEMP_REJECT_NOISE,0); return; }
    a2+=acc[a]*acc[a];
    seed2+=seed[a]*seed[a]; corrected2+=corrected[a]*corrected[a];
    if(round_active) { float d=acc[a]-acc_reference[a]; direction2+=d*d; }
    if(have_raw&&fabsf(raw[a]-previous_raw[a])>APP_TEMP_LEARN_GROSS_STEP_DPS)
      { memcpy(previous_raw,raw,12U); cancel_round(TEMP_REJECT_MOTION,1); return; }
  }
  memcpy(previous_raw,raw,12U); have_raw=1U;
  /* Raw shock guards stay immediate. Ordinary gravity/direction gates use
   * the same 100 Hz means as the bias statistics, rather than a noisy LSB. */
  const float lo=9.80665f-APP_TEMP_LEARN_ACC_GROSS_MS2,hi=9.80665f+APP_TEMP_LEARN_ACC_GROSS_MS2;
  if(a2<lo*lo||a2>hi*hi||direction2>APP_TEMP_LEARN_DIRECTION_GROSS_MS2*APP_TEMP_LEARN_DIRECTION_GROSS_MS2||
     seed2>APP_TEMP_LEARN_GROSS_RESIDUAL_DPS*APP_TEMP_LEARN_GROSS_RESIDUAL_DPS||
     corrected2>APP_TEMP_LEARN_GROSS_RESIDUAL_DPS*APP_TEMP_LEARN_GROSS_RESIDUAL_DPS)
    { cancel_round(TEMP_REJECT_MOTION,1); return; }
  if(!rest) {
    const uint32_t grace=APP_TEMP_LEARN_REST_GRACE_MS*APP_CAL_FEED_HZ/1000U;
    if(rest_false_samples<=grace) rest_false_samples++;
    clear_decimation(); /* never admit non-rest samples or bridge a partial mean */
    if(!round_active||rest_false_samples>grace||
       ++rest_pause_samples>APP_TEMP_LEARN_REST_PAUSE_MAX_MS*APP_CAL_FEED_HZ/1000U)
      cancel_round(TEMP_REJECT_MOTION,0); /* VQF-only ambiguity is not independent movement */
    return;
  }
  rest_false_samples=0U;
  for(unsigned a=0;a<3;++a) {
    sum_raw[a]+=raw[a]; sum_seed[a]+=seed[a]; sum_corrected[a]+=corrected[a]; sum_acc[a]+=acc[a];
  }
  sum_temp+=temp;
  if(++decimate_n==DECIMATE) {
    float gyro[3],residual[3],corr[3],gravity[3],t=sum_temp/(float)DECIMATE;
    for(unsigned a=0;a<3;++a) {
      gyro[a]=sum_raw[a]/(float)DECIMATE; residual[a]=sum_seed[a]/(float)DECIMATE;
      corr[a]=sum_corrected[a]/(float)DECIMATE; gravity[a]=sum_acc[a]/(float)DECIMATE;
    }
    clear_decimation(); feed_100hz(gyro,residual,corr,gravity,t);
  }
}
void gyro_temp_learning_environment(uint32_t now,uint8_t safe)
{
  safe_to_save=safe;
  if(saving&&!safe) saving=0U; /* queued request remains pending if rest ends */
#if APP_TEMP_FLASH_AUTO_SAVE_ENABLE && !defined(APP_USER_BL_UPDATE)
  if(!pending_save&&gyro_temp_comp_save_allowed(now,0)) pending_save=1U;
#endif
}
int gyro_temp_learning_save_begin(uint32_t now)
{
  if(saving||!pending_save||!safe_to_save||!gyro_temp_comp_save_allowed(now,1)) return 0;
  saving=1U; return 1;
}
void gyro_temp_learning_save_finish(uint32_t now)
{
  int result=gyro_temp_comp_commit(now);
  saving=pending_save=0U;
  gyro_temp_learning_gap();
  if(result) { last_reject=TEMP_REJECT_FLASH; phase=TEMP_ERROR; }
}
int gyro_temp_learning_saving(void) { return saving; }
int gyro_temp_learning_wants_save(void) { return pending_save && !saving; }
void gyro_temp_learning_status(uint32_t now,ahrs_payload_temp_learn_status_t *s)
{
  temp_comp_info_t info; gyro_temp_comp_info(&info);
  memset(s,0,sizeof(*s)); s->version=1U;
  s->flags=(uint8_t)((enabled?1U:0U)|(gyro_temp_comp_active()?2U:0U)|(info.dirty?4U:0U)|
     (info.flash_valid?8U:0U)|(info.last_flash_error?16U:0U)|(info.reboot_required?32U:0U)|(safe_to_save?64U:0U));
  s->state=saving?TEMP_SAVING:pending_save?TEMP_PENDING_SAVE:phase; s->last_reject=last_reject;
  const temp_comp_record_v4_t *r=gyro_temp_comp_working();
  for(unsigned i=0;i<TEMP_BIN_COUNT;++i) {
    if(r->bins[i].flags==TEMP_BIN_CONFIRMED) s->confirmed_bins++;
    if(r->bins[i].flags==TEMP_BIN_CANDIDATE) s->candidate_bins++;
  }
  s->flash_slot=info.flash_slot; s->flash_writes_this_boot=info.writes;
  s->flash_sequence=info.sequence; s->table_revision=info.revision;
  s->current_temp_c=current_temp; s->boot_temp_c=gyro_temp_comp_boot_temperature(); s->learning_temp_c=learning_temp;
  s->rest_elapsed_ms=rest_points*1000U/APP_TEMP_LEARN_HZ;
  s->collect_elapsed_ms=capture_points*1000U/APP_TEMP_LEARN_HZ;
  for(unsigned a=0;a<3;++a) s->applied_delta_bias_dps[a]=gyro_temp_comp_delta(a);
  s->last_flash_error=info.last_flash_error; s->total_bins=TEMP_BIN_COUNT; s->uptime_ms=now;
}
static void reply_ack(temp_learn_reply_t *r,uint8_t cmd,uint8_t status,uint16_t detail)
{
  ahrs_payload_ack_t ack={cmd,status,detail}; r->id=AHRS_MSG_ACK; r->len=sizeof(ack); memcpy(r->payload,&ack,sizeof(ack));
}
void gyro_temp_learning_command(uint8_t id,uint8_t len,const uint8_t *payload,
    uint8_t seq_unused,uint8_t settings,uint8_t busy,uint32_t now,temp_learn_reply_t *r)
{
  (void)seq_unused; memset(r,0,sizeof(*r));
  if(id==AHRS_CMD_QUERY_TEMP_LEARN_STATUS) {
    if(len) { reply_ack(r,id,AHRS_ACK_INVALID_PARAM,0U); return; }
    ahrs_payload_temp_learn_status_t s; gyro_temp_learning_status(now,&s);
    r->id=AHRS_MSG_TEMP_LEARN_STATUS; r->len=sizeof(s); memcpy(r->payload,&s,sizeof(s)); return;
  }
  if(id==AHRS_CMD_QUERY_TEMP_LEARN_PAGE) {
    if(len!=1U||!payload||payload[0]>TEMP_BIN_COUNT) { reply_ack(r,id,AHRS_ACK_INVALID_PARAM,0U); return; }
    ahrs_payload_temp_learn_page_t p; temp_comp_info_t info; gyro_temp_comp_info(&info);
    memset(&p,0,sizeof(p)); p.version=1U; p.model_schema=4U; p.total_bins=TEMP_BIN_COUNT;
    p.offset=payload[0]; p.table_revision=info.revision;
    for(unsigned i=p.offset;i<TEMP_BIN_COUNT&&p.entry_count<2U;++i) {
      const temp_bin_t *b=&gyro_temp_comp_working()->bins[i];
      ahrs_temp_learn_entry_t *e=&p.entry[p.entry_count++]; e->bin_index=(uint8_t)i; e->state=(uint8_t)b->flags;
      if(b->flags!=TEMP_BIN_EMPTY) {
        e->accepted_windows=b->accepted_windows; e->mean_temp_c=b->temperature_c; memcpy(e->bias_dps,b->bias_dps,12U);
        e->sigma_rms_dps=sqrtf((b->sigma_dps[0]*b->sigma_dps[0]+b->sigma_dps[1]*b->sigma_dps[1]+b->sigma_dps[2]*b->sigma_dps[2])/3.f);
      }
    }
    r->id=AHRS_MSG_TEMP_LEARN_PAGE; r->len=(uint8_t)(10U+24U*p.entry_count); memcpy(r->payload,&p,r->len); return;
  }
  if(id!=AHRS_CMD_TEMP_LEARN_CONTROL) { reply_ack(r,id,AHRS_ACK_UNKNOWN_CMD,0U); return; }
  if(len!=2U||!payload||payload[0]!=1U||payload[1]<1U||payload[1]>5U)
    { reply_ack(r,id,AHRS_ACK_INVALID_PARAM,0U); return; }
  if(!settings) { reply_ack(r,id,AHRS_ACK_EXEC_FAILED,TEMP_DETAIL_MODE); return; }
  if(busy||saving) { reply_ack(r,id,AHRS_ACK_EXEC_FAILED,TEMP_DETAIL_BUSY); return; }
  uint8_t action=payload[1]; temp_comp_info_t info; gyro_temp_comp_info(&info);
  if(action==1U) {
    if(!APP_GYR_TEMP_LEARNING_ENABLE) { reply_ack(r,id,AHRS_ACK_EXEC_FAILED,TEMP_DETAIL_DISABLED); return; }
    enabled=1U; cancel_round(TEMP_REJECT_NONE,0);
  } else if(action==2U) { enabled=0U; cancel_round(TEMP_REJECT_NONE,0); }
  else if(action==3U||action==5U) {
    if(action==3U&&!info.dirty) { reply_ack(r,id,AHRS_ACK_EXEC_FAILED,TEMP_DETAIL_NOT_DIRTY); return; }
    /* Clear also consumes the same interval and wear budget. */
    if(!gyro_temp_comp_write_allowed(now,1))
      { reply_ack(r,id,AHRS_ACK_EXEC_FAILED,TEMP_DETAIL_RATE_LIMIT); return; }
    if(action==5U) gyro_temp_comp_clear();
    if(!gyro_temp_comp_save_allowed(now,1)) { reply_ack(r,id,AHRS_ACK_EXEC_FAILED,TEMP_DETAIL_RATE_LIMIT); return; }
    pending_save=1U; cancel_round(TEMP_REJECT_NONE,0);
    reply_ack(r,id,AHRS_ACK_PERSIST_PENDING,0U); return;
  } else {
    gyro_temp_comp_discard_candidates(); cancel_round(TEMP_REJECT_NONE,0);
    for(unsigned i=0;i<TEMP_BIN_COUNT;++i) bin_episode[i]=episode;
  }
  reply_ack(r,id,AHRS_ACK_SUCCESS,0U);
}

#include "static6_diag.h"
#include "bsp.h"
#include <stddef.h>
#if APP_STATIC6_DIAG
static6_block_t static6_work;
volatile static6_block_t static6_published;
volatile uint32_t static6_seq,static6_enabled;
_Static_assert(sizeof(static6_block_t)==272,"static6 host ABI v2");
void static6_raw(const int16_t g[3],const int16_t a[3],int16_t temp,uint32_t cy)
{
  static6_block_t *s=&static6_work;
  if(!s->n)s->first_cy=cy;
  s->last_cy=cy;s->n++;
  for(unsigned i=0;i<7;i++) {
    int32_t x=i<3?g[i]:(i<6?a[i-3]:temp);
    if(s->n==1) {s->sum[i]=0;s->square[i]=0;s->lo[i]=s->hi[i]=(int16_t)x;}
    s->sum[i]+=x;s->square[i]+=(uint32_t)(x*x);
    if(x<s->lo[i])s->lo[i]=(int16_t)x;
    if(x>s->hi[i])s->hi[i]=(int16_t)x;
  }
}
void static6_rate(const float corrected[3])
{
  static6_block_t *s=&static6_work;
  float inv=1.f/(float)s->n;
  for(unsigned i=0;i<3;i++) {
    float x=corrected[i];
    if(s->n==1) {s->mean[i]=x;s->m2[i]=0;s->minimum[i]=s->maximum[i]=x;}
    else {float d=x-s->mean[i];s->mean[i]+=d*inv;s->m2[i]+=d*(x-s->mean[i]);}
    if(x<s->minimum[i])s->minimum[i]=x;
    if(x>s->maximum[i])s->maximum[i]=x;
  }
}
void static6_finish(void)
{
  if(static6_work.n!=8000U)return;
  static6_work.window++;
  static6_seq++;__DMB();static6_published=static6_work;__DMB();static6_seq++;
  static6_work.n=0;
}
#endif

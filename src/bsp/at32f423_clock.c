/**
 * AT32F423 system clock: internal HICK -> PLL -> 150 MHz.
 *
 * The flash wait-state and 1.3 V LDO settings must be applied after
 * crm_reset() and before switching SYSCLK to the PLL.
 */
#include "at32f423_clock.h"

void system_clock_config(void)
{
  uint16_t pll_ms = 0U;
  uint16_t pll_ns = 0U;
  uint16_t pll_fr = 0U;

  crm_reset();

  crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
  pwc_ldo_output_voltage_set(PWC_LDO_OUTPUT_1V3);
  flash_psr_set(FLASH_WAIT_CYCLE_4);

  crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK, TRUE);
  while(crm_flag_get(CRM_HICK_STABLE_FLAG) != SET)
  {
  }

  crm_ahb_div_set(CRM_AHB_DIV_1);
#if SYSTEM_APB2_DIV_VALUE == 1U
  crm_apb2_div_set(CRM_APB2_DIV_1);
#elif SYSTEM_APB2_DIV_VALUE == 2U
  crm_apb2_div_set(CRM_APB2_DIV_2);
#elif SYSTEM_APB2_DIV_VALUE == 4U
  crm_apb2_div_set(CRM_APB2_DIV_4);
#elif SYSTEM_APB2_DIV_VALUE == 8U
  crm_apb2_div_set(CRM_APB2_DIV_8);
#elif SYSTEM_APB2_DIV_VALUE == 16U
  crm_apb2_div_set(CRM_APB2_DIV_16);
#else
#error "SYSTEM_APB2_DIV_VALUE must be 1, 2, 4, 8, or 16"
#endif
  crm_apb1_div_set(CRM_APB1_DIV_2);

  crm_hick_divider_select(CRM_HICK48_DIV6);

  if(crm_pll_parameter_calculate(CRM_PLL_SOURCE_HICK,
                                 SYSTEM_CLOCK_TARGET_HZ,
                                 &pll_ms,
                                 &pll_ns,
                                 &pll_fr) != SUCCESS)
  {
#if SYSTEM_CLOCK_TARGET_HZ == 150000000U
    pll_ns = 75U;
    pll_ms = 1U;
    pll_fr = (uint16_t)CRM_PLL_FR_2;
#endif
    /* For other targets the library already returned the closest tuple. */
  }
  crm_pll_config(CRM_PLL_SOURCE_HICK,
                 pll_ns,
                 pll_ms,
                 (crm_pll_fr_type)pll_fr);

  crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
  while(crm_flag_get(CRM_PLL_STABLE_FLAG) != SET)
  {
  }

  crm_auto_step_mode_enable(TRUE);
  crm_sysclk_switch(CRM_SCLK_PLL);
  while(crm_sysclk_switch_status_get() != CRM_SCLK_PLL)
  {
  }
  crm_auto_step_mode_enable(FALSE);

  system_core_clock_update();
}




#include "at32f423_int.h"
#include "bsp.h"
#include "usb_cdc.h"
#include "perf_meter.h"

void NMI_Handler(void)
{
}

void HardFault_Handler(void)
{
  while(1)
  {
  }
}

void MemManage_Handler(void)
{
  while(1)
  {
  }
}

void BusFault_Handler(void)
{
  while(1)
  {
  }
}

void UsageFault_Handler(void)
{
  while(1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  uint32_t mark=perf_irq_enter();
  bsp_systick_tick();
  perf_irq_exit(PERF_SYSTICK_IRQ,mark);
}

void OTGFS1_IRQHandler(void)
{
  uint32_t mark=perf_irq_enter();
  usb_cdc_isr();
  perf_irq_exit(PERF_USB_IRQ,mark);
}



void USART4_IRQHandler(void)
{
  uint32_t mark=perf_irq_enter();
  uart_rx_isr();
  perf_irq_exit(PERF_UART_IRQ,mark);
}

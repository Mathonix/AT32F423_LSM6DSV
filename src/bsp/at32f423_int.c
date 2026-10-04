#include "at32f423_int.h"
#include "bsp.h"
#include "usb_cdc.h"

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
  bsp_systick_tick();
}

void OTGFS1_IRQHandler(void)
{
  usb_cdc_isr();
}



void USART4_IRQHandler(void)
{
  uart_rx_isr();
}

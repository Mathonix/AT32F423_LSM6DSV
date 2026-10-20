#include "at32f423_int.h"
#include "bsp.h"
#include "usb_cdc.h"
#include "eskf8k_validation.h"
#include "sram_capture.h"

RAM_CODE void NMI_Handler(void)
{
}

RAM_CODE void HardFault_Handler(void)
{
  while(1)
  {
  }
}

RAM_CODE void MemManage_Handler(void)
{
  while(1)
  {
  }
}

RAM_CODE void BusFault_Handler(void)
{
  while(1)
  {
  }
}

RAM_CODE void UsageFault_Handler(void)
{
  while(1)
  {
  }
}

RAM_CODE void SVC_Handler(void)
{
}

RAM_CODE void DebugMon_Handler(void)
{
}

RAM_CODE void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  eskf8k_cpu_enter(CPU_TICK);
  bsp_systick_tick();
  eskf8k_cpu_leave();
}

void OTGFS1_IRQHandler(void)
{
  eskf8k_cpu_enter(CPU_USB);
  usb_cdc_isr();
  eskf8k_cpu_leave();
}



void USART4_IRQHandler(void)
{
  eskf8k_cpu_enter(CPU_UART);
  uart_rx_isr();
  eskf8k_cpu_leave();
}

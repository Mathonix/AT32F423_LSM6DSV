#ifndef SPI_MATRIX_H
#define SPI_MATRIX_H

#include <stdint.h>

#define SPI_MATRIX_MAGIC        0x5350494DU /* "SPIM" */
#define SPI_MATRIX_VERSION      1U
#define SPI_MATRIX_RESULT_MAX   768U
#define SPI_MATRIX_READS        100U
#define SPI_MATRIX_REF_READS    10U

typedef struct
{
  uint32_t config;
  uint32_t ref_total_ok;   /* total[31:16], correct 0x70[15:0] */
  uint32_t ok_ff;          /* correct 0x70[31:16], 0xFF[15:0] */
  uint32_t zero_other;     /* 0x00[31:16], other[15:0] */
  uint32_t timeout_first;  /* timeouts[31:16], first unexpected byte[7:0] */
  uint32_t first_rx;       /* address/frame RX[31:16], data RX[15:0] */
  uint32_t ctrl1;
  uint32_t ctrl2;
  uint32_t sts_or;
  uint32_t gpio_cfgr;
  uint32_t gpio_muxl;
} spi_matrix_result_t;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  volatile uint32_t state;       /* 0=startup, 1=running, 2=done, 3=fatal */
  volatile uint32_t result_count;
  uint32_t result_max;
  uint32_t reads_per_result;
  uint32_t ref_reads;
  uint32_t core_hz;
  uint32_t apb2_hz;
  volatile uint32_t current_config;
  volatile uint32_t reference_fail_groups;
  volatile uint32_t fatal_code;
  uint32_t reserved[4];
  spi_matrix_result_t result[SPI_MATRIX_RESULT_MAX];
} spi_matrix_log_t;

extern volatile spi_matrix_log_t spi_matrix_log;

#endif

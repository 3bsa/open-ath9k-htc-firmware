/*************************************************************************/
/*  Copyright (c) 2006 Atheros Communications, Inc., All Rights Reserved */
/*                                                                       */
/*  Module Name : sys_cfg.h                                              */
/*                                                                       */
/*  Abstract                                                             */
/*      This file contains definition of platform and sysmte config   .  */
/*                                                                       */
/*  NOTES                                                                */
/*      None                                                             */
/*                                                                       */
/*************************************************************************/

#ifndef _SYS_CFG_H_
#define _SYS_CFG_H_

/************************** FPGA version **************************/
#define MAGPIE_FPGA_RAM_256K         1

/************************** ROM DEFINE ***************************/

#if defined(_ROM_)
#include "rom_cfg.h"

#if MAGPIE_FPGA_RAM_256K == 1 
#undef  MAX_BUF_NUM 
#define MAX_BUF_NUM                100
#endif

#elif defined(_RAM_)

#include "rom_cfg.h"
#include "magpie_mem.h"

/************************* Resource DEFS ***********************/
#define MAX_DESC_NUM               100

#ifdef RX_SCATTER
#define MAX_BUF_NUM                60
#else
#define MAX_BUF_NUM                40
#endif

#if MAGPIE_FPGA_RAM_256K == 1 
#undef  MAX_BUF_NUM 
#define MAX_BUF_NUM                100
#endif

#undef 	SYSTEM_MODULE_DBG
#define SYSTEM_MODULE_DBG               1

/************************* WLAN DEFS ***************************/
#define MAGPIE_ENABLE_WLAN              1
#define MAGPIE_ENABLE_PCIE              1
#define MAGPIE_ENABLE_WLAN_IN_TARGET    0
#define MAGPIE_ENABLE_WLAN_SELF_TX      0
#define MAGPIE_ENABLE_WLAN_RATE_CTRL    1
#define WLAN_MAX_RXBUF                  15
#define WLAN_MAX_TXBUF                  10

/****************************** WATCH DOG *******************************/
#define WDT_DEFAULT_TIMEOUT_VALUE   3*ONE_MSEC*1000 // Initial value is 3 seconds, firmware changes it to 65 milliseconds

#endif


#endif /* _SYS_CFG_H_ */


#include "usb_defs.h"
#include "usb_type.h"
#include "usb_pre.h"
#include "usb_extr.h"
#include "usb_std.h"
#include "reg_defs.h"
#include "athos_api.h"
#include "usbfifo_api.h"

#include "sys_cfg.h"

#define USB_EP4_MAX_PKT_SIZE bUSB_EP_MAX_PKT_SIZE_64
#define USB_EP3_MAX_PKT_SIZE bUSB_EP_MAX_PKT_SIZE_64

extern USB_FIFO_CONFIG usbFifoConf;
extern Action eUsbCxFinishAction;

void cold_reboot(void)
{
	A_PRINTF("Cold reboot initiated.");
#if defined(PROJECT_MAGPIE)
	HAL_WORD_REG_WRITE(WATCH_DOG_MAGIC_PATTERN_ADDR, 0);
#elif defined(PROJECT_K2)
	HAL_WORD_REG_WRITE(MAGPIE_REG_RST_STATUS_ADDR, 0);
#endif /* #if defined(PROJECT_MAGPIE) */
	A_USB_JUMP_BOOT();
}

/*
 * support more than 64 bytes command on ep3
 */
void usb_status_in_patch(void)
{
	uint16_t count;
	uint16_t remainder;
	uint16_t reg_buf_len;
	static uint16_t buf_len;
	static VBUF *evntbuf = NULL;
	static volatile uint32_t *regaddr;
	static BOOLEAN cmd_is_new = TRUE;
	BOOLEAN cmd_end = FALSE;

	if (cmd_is_new) {
		evntbuf = usbFifoConf.get_event_buf();
		if (evntbuf != NULL) {
			regaddr = (uint32_t *)VBUF_GET_DATA_ADDR(evntbuf);
			buf_len = evntbuf->buf_length;
		} else {
			mUSB_STATUS_IN_INT_DISABLE();
			return;
		}

		cmd_is_new = FALSE;
	}

	if (buf_len > USB_EP3_MAX_PKT_SIZE) {
		reg_buf_len = USB_EP3_MAX_PKT_SIZE;
		buf_len -= USB_EP3_MAX_PKT_SIZE;
	}
	/* TODO: 64 bytes...
	 * controller supposed will take care of zero-length? */
	else {
		reg_buf_len = buf_len;
		cmd_end = TRUE;
	}

	/* INT use EP3 */
	for (count = 0; count < (reg_buf_len / 4); count++)
	{
		USB_WORD_REG_WRITE(ZM_EP3_DATA_OFFSET, *regaddr);
		regaddr++;
	}

	remainder = reg_buf_len % 4;

	if (remainder) {
		switch(remainder) {
		case 3:
			USB_WORD_REG_WRITE(ZM_CBUS_FIFO_SIZE_OFFSET, 0x7);
			break;
		case 2:
			USB_WORD_REG_WRITE(ZM_CBUS_FIFO_SIZE_OFFSET, 0x3);
			break;
		case 1:
			USB_WORD_REG_WRITE(ZM_CBUS_FIFO_SIZE_OFFSET, 0x1);
			break;
		}

		USB_WORD_REG_WRITE(ZM_EP3_DATA_OFFSET, *regaddr);

		/* Restore CBus FIFO size to word size */
		USB_WORD_REG_WRITE(ZM_CBUS_FIFO_SIZE_OFFSET, 0xF);
	}

	mUSB_EP3_XFER_DONE();

	if (evntbuf != NULL && cmd_end) {
		usbFifoConf.send_event_done(evntbuf);
		cmd_is_new = TRUE;
	}
}

/*
 * support more than 64 bytes command on ep4 
 */
void usb_reg_out_patch(void)
{
	uint16_t usbfifolen;
	uint16_t ii;
	uint32_t ep4_data;
	static volatile uint32_t *regaddr;
	static uint16_t cmd_len;
	static VBUF *buf;
	BOOLEAN cmd_is_last = FALSE;
	static BOOLEAN cmd_is_new = TRUE;

	/* get the size of this transcation */
	usbfifolen = USB_BYTE_REG_READ(ZM_EP4_BYTE_COUNT_LOW_OFFSET);

	if (usbfifolen > USB_EP4_MAX_PKT_SIZE) {
		A_PRINTF("EP4 FIFO Bug? Buffer is too big: %x\n", usbfifolen);
		cold_reboot();
	}

	/* check is command is new */
	if(cmd_is_new) {

		buf = usbFifoConf.get_command_buf();
		cmd_len = 0;

		if(!buf) {
			A_PRINTF("%s: Filed to get new buffer.\n", __func__);
			goto err;
		}

		/* copy free, assignment buffer of the address */
		regaddr = (uint32_t *)buf->desc_list->buf_addr;

		cmd_is_new = FALSE;
	}

	/* just in case, suppose should not happen */
	if(!buf)
		goto err;

	/* if size is smaller, this is the last command!
	 * zero-length supposed should be set through 0x27/bit7->0x19/bit4, not here
	 */
	if(usbfifolen < USB_EP4_MAX_PKT_SIZE)
		cmd_is_last = TRUE;

	/* accumulate the size */
	cmd_len += usbfifolen;

	if (cmd_len > buf->desc_list->buf_size) {
		A_PRINTF("%s: Data length on EP4 FIFO is bigger as "
			 "allocated buffer data! Drop it!\n", __func__);
		goto err;
	}

	/* round it to alignment */
	if(usbfifolen % 4)
		usbfifolen = (usbfifolen >> 2) + 1;
	else
		usbfifolen = usbfifolen >> 2;

	/* retrieve the data from fifo */
	for(ii = 0; ii < usbfifolen; ii++) {
		/* read fifo data out */
		ep4_data = USB_WORD_REG_READ(ZM_EP4_DATA_OFFSET);
		*regaddr = ep4_data;
		regaddr++;
	}

	/* if this is the last command, callback to HTC */
	if (cmd_is_last) {
		buf->desc_list->next_desc = NULL;
		buf->desc_list->data_offset = 0;
		buf->desc_list->data_size = cmd_len;
		buf->desc_list->control = 0;
		buf->next_buf = NULL;
		buf->buf_length = cmd_len;

		usbFifoConf.recv_command(buf);

		cmd_is_new = TRUE;
	}

	goto done;
err:
	/* we might get no command buffer here?
	 * but if we return here, the ep4 fifo will be lock out,
	 * so that we still read them out but just drop it? */
	for(ii = 0; ii < usbfifolen; ii++)
		ep4_data = USB_WORD_REG_READ(ZM_EP4_DATA_OFFSET);

done:
	/* mUSB_STATUS_IN_INT_ENABLE(); */
	;
}

/*
 * usb1.1 ep6 fix
 * TODO:
 * - theoretically ep6 configured same way as ep1
 * so, if there are some problems we should have it
 * there too.
 * - do we really need support usb1.1?
 */
extern uint16_t		u8UsbConfigValue;
extern uint16_t		u8UsbInterfaceValue;
extern uint16_t		u8UsbInterfaceAlternateSetting;
extern SetupPacket	ControlCmd;
extern void		vUsbClrEPx(void);

#undef FS_C1_I0_A0_EP_NUMBER
#define FS_C1_I0_A0_EP_NUMBER 6

#define FS_C1_I0_A0_EP6_BLKSIZE    BLK512BYTE
#define FS_C1_I0_A0_EP6_BLKNO      DOUBLE_BLK
#define FS_C1_I0_A0_EP6_DIRECTION  DIRECTION_OUT
#define FS_C1_I0_A0_EP6_TYPE       TF_TYPE_BULK
#define FS_C1_I0_A0_EP6_MAX_PACKET 0x0040
#define FS_C1_I0_A0_EP6_bInterval  0

/* EP6 */
#define FS_C1_I0_A0_EP6_FIFO_START	\
	 (FS_C1_I0_A0_EP5_FIFO_START + FS_C1_I0_A0_EP5_FIFO_NO)
#define FS_C1_I0_A0_EP6_FIFO_NO		\
	 (FS_C1_I0_A0_EP6_BLKNO * FS_C1_I0_A0_EP6_BLKSIZE)
#define FS_C1_I0_A0_EP6_FIFO_CONFIG	\
	 (0x80 | ((FS_C1_I0_A0_EP6_BLKSIZE - 1) << 4) | \
	  ((FS_C1_I0_A0_EP6_BLKNO - 1) << 2) | FS_C1_I0_A0_EP6_TYPE)
#define FS_C1_I0_A0_EP6_FIFO_MAP	\
	 (((1 - FS_C1_I0_A0_EP6_DIRECTION) << 4) | EP6)
#define FS_C1_I0_A0_EP6_MAP		\
	 (FS_C1_I0_A0_EP6_FIFO_START | (FS_C1_I0_A0_EP6_FIFO_START << 4) | \
	  (MASK_F0 >> (4*FS_C1_I0_A0_EP6_DIRECTION)))

void vUSBFIFO_EP6Cfg_FS_patch(void)
{
#if (FS_C1_I0_A0_EP_NUMBER >= 6)
	int i;

	/* EP0X06 */
	mUsbEPMap(EP6, FS_C1_I0_A0_EP6_MAP);
	mUsbFIFOMap(FS_C1_I0_A0_EP6_FIFO_START, FS_C1_I0_A0_EP6_FIFO_MAP);
	mUsbFIFOConfig(FS_C1_I0_A0_EP6_FIFO_START, FS_C1_I0_A0_EP6_FIFO_CONFIG);

	for(i = FS_C1_I0_A0_EP6_FIFO_START + 1 ;
            i < FS_C1_I0_A0_EP6_FIFO_START + FS_C1_I0_A0_EP6_FIFO_NO ; i ++)
	{
		mUsbFIFOConfig(i, (FS_C1_I0_A0_EP6_FIFO_CONFIG & (~BIT7)) );
	}

	mUsbEPMxPtSzHigh(EP6, FS_C1_I0_A0_EP6_DIRECTION,
			 (FS_C1_I0_A0_EP6_MAX_PACKET & 0x7ff));
	mUsbEPMxPtSzLow(EP6, FS_C1_I0_A0_EP6_DIRECTION,
			(FS_C1_I0_A0_EP6_MAX_PACKET & 0x7ff));
	mUsbEPinHighBandSet(EP6, FS_C1_I0_A0_EP6_DIRECTION,
			    FS_C1_I0_A0_EP6_MAX_PACKET);
#endif
}

void vUsbFIFO_EPxCfg_FS_patch(void)
{
	switch (u8UsbConfigValue)
	{
#if (FS_CONFIGURATION_NUMBER >= 1)
		/* Configuration 0X01 */
        case 0X01:
		switch (u8UsbInterfaceValue)
		{
#if (FS_C1_INTERFACE_NUMBER >= 1)
			/* Interface 0 */
                case 0:
			switch (u8UsbInterfaceAlternateSetting)
			{

#if (FS_C1_I0_ALT_NUMBER >= 1)
				/* AlternateSetting 0 */
                        case 0:

				/* snapped.... */

				/* patch up this ep6_fs config */
				vUSBFIFO_EP6Cfg_FS_patch();

				break;

#endif
                        default:
				break;
			}
			break;
#endif
                default:
			break;
		}
		break;
#endif
        default:
		break;
	}
	/* mCHECK_STACK(); */
}

BOOLEAN bSet_configuration_patch(void)
{
	/* do some defaul configuration */
	bSet_configuration();

	/* overwrite defaul FIFO configuration for FullSpeed USB */
	if ((mLOW_BYTE(mDEV_REQ_VALUE()) != 0) && !mUsbHighSpeedST())
			vUsbFIFO_EPxCfg_FS_patch();

	eUsbCxFinishAction = ACT_DONE;
	return TRUE;
}

extern BOOLEAN bStandardCommand(void);

BOOLEAN bStandardCommand_patch(void)
{
	if (mDEV_REQ_REQ() == USB_SET_CONFIGURATION) {
		A_USB_SET_CONFIG();

#if ENABLE_SWAP_DATA_MODE
		/* SWAP FUNCTION should be enabled while DMA engine
		 * is not working, the best place to enable it
		 * is before we trigger the DMA */
		MAGPIE_REG_USB_RX0_SWAP_DATA = 0x1;
		MAGPIE_REG_USB_TX0_SWAP_DATA = 0x1;

#if SYSTEM_MODULE_HP_EP5
		MAGPIE_REG_USB_RX1_SWAP_DATA = 0x1;
#endif

#if SYSTEM_MODULE_HP_EP6
		MAGPIE_REG_USB_RX2_SWAP_DATA = 0x1;
#endif

#endif /* ENABLE_SWAP_DATA_MODE */
		return TRUE;
	} else
		return bStandardCommand();
}


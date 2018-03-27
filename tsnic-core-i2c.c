/*
 * Copyright (c) 2018, Kontron Europe GmbH
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/if_ether.h>
#include <linux/string.h>

#include "tsnic-core.h"

#define EEP_ADDR 0x51
#define EEP_SIZE 256
#define EEP_PAGE_SIZE 8
#define PCI_BAR 5
#define CSR_OFFSET 0x02000100
#define CORE_CLK 100000000ULL

enum {
	TFR_CMD                  = (CSR_OFFSET + 0x0000),
	RX_DATA                  = (CSR_OFFSET + 0x0004),
	CTRL                     = (CSR_OFFSET + 0x0008),
	ISER                     = (CSR_OFFSET + 0x000c),
	ISR                      = (CSR_OFFSET + 0x0010),
	STATUS                   = (CSR_OFFSET + 0x0014),
	TFR_CMD_FIFO_LVL         = (CSR_OFFSET + 0x0018),
	RX_DATA_FIFO_LVL         = (CSR_OFFSET + 0x001c),
	SCL_LOW                  = (CSR_OFFSET + 0x0020),
	SCL_HIGH                 = (CSR_OFFSET + 0x0024),
	SDA_HOLD                 = (CSR_OFFSET + 0x0028),
};

#define TFR_RW_D           (1 << 0)
#define TFR_AD_SHIFT       1
#define TFR_AD_MASK        0xfe
#define TFR_CMD_STO        (1 << 8)
#define TFR_CMD_RSTA       (1 << 9)
#define TFR_CMD_STA        (1 << 10)

#define CTRL_EN            (1 << 0)
#define CTRL_BUS_SPEED     (1 << 1)

#define ISR_TX_READY       (1 << 0)
#define ISR_RX_READY       (1 << 1)
#define ISR_NACK_DET       (1 << 2)
#define ISR_ARBLOST_DET    (1 << 3)
#define ISR_RX_OVER        (1 << 4)

#define STATUS_CORE_STATUS (1 << 0)


static inline u32 csr_read(void * __iomem io_addr, int offset)
{
	return *(u32*)(io_addr + offset);
}

static inline void csr_write(void * __iomem io_addr, int offset, u32 value)
{
	*(u32*)(io_addr + offset) = value;
}

static int i2c_tx(void *io_addr, u32 val)
{
	int tout = 100000;
	while (--tout && !(csr_read(io_addr, ISR) & ISR_TX_READY))
		udelay(1);
	if (!tout) {
		return -EIO;
	}

	csr_write(io_addr, TFR_CMD, val);
	udelay(1);

	return 0;
}

static int i2c_rx(void *io_addr, u8 *c)
{
	int tout = 100000;
	while (--tout && !(csr_read(io_addr, ISR) & ISR_RX_READY))
		udelay(1);
	if (!tout) {
		return -EIO;
	}

	*c = csr_read(io_addr, RX_DATA) & 0xff;

	return 0;
}

static void i2c_init(void *io_addr)
{
	csr_write(io_addr, CTRL, 0);
	csr_write(io_addr, SCL_LOW,  (CORE_CLK / 200000));
	csr_write(io_addr, SCL_HIGH, (CORE_CLK / 200000));
	csr_write(io_addr, SDA_HOLD, (CORE_CLK / 3333333));
	csr_write(io_addr, ISR, 0xff);
	csr_write(io_addr, CTRL, CTRL_EN);
	udelay(10);
}

static struct vpd {
	int err;
	u8 eep[EEP_SIZE];
} vpd;

int tsnic_vpd_init(void * io_addr)
{
	int num = EEP_SIZE;

	vpd.err = 0;

	i2c_init(io_addr);

	csr_write(io_addr, ISR, 0xff);

	/* eep address */
	vpd.err = i2c_tx(io_addr, TFR_CMD_STA | (EEP_ADDR << TFR_AD_SHIFT));
	if (vpd.err)
		goto out;

	/* offset */
	vpd.err = i2c_tx(io_addr, 0);
	if (vpd.err)
		goto out;

	/* repeated start */
	vpd.err = i2c_tx(io_addr, TFR_CMD_RSTA | (EEP_ADDR << TFR_AD_SHIFT) | TFR_RW_D);
	if (vpd.err)
		goto out;

	while (num--) {
		vpd.err = i2c_tx(io_addr, num ? 0 : TFR_CMD_STO);
		if (vpd.err)
			goto out;

		vpd.err = i2c_rx(io_addr, &vpd.eep[EEP_SIZE - (num + 1)]);
		if (vpd.err)
			goto out;
	}

out:
	csr_write(io_addr, CTRL, 0);

	return vpd.err;
}

int tsnic_vpd_eth_hw_addr(u8 *addr)
{
	if (vpd.err)
		return vpd.err;

	switch (vpd.eep[0]) {
	case 0:
		memcpy(addr, &vpd.eep[1], ETH_ALEN);
		return 0;

	default:
		return -EINVAL;
	}

	/* should not reach this end */
	return -EINVAL;
}

int tsnic_vpd_asset_tag(u8 *asset, size_t size)
{
	if (vpd.err)
		return vpd.err;

	switch (vpd.eep[0]) {
	case 0:
		if (size < 11) {
			return -ENOMEM;
		}
		memcpy(asset, &vpd.eep[7], 10);
		asset[17] = 0;
		return 0;

	default:
		return -EINVAL;
	}

	/* should not reach this end */
	return -EINVAL;
}

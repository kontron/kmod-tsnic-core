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

#ifndef TSNIC_CORE_H_
#define TSNIC_CORE_H_

#include <linux/types.h>

#define TSNIC_SNO_LEN 10

int tsnic_vpd_init(void * io_addr);
int tsnic_vpd_eth_hw_addr(u8 *addr);
int tsnic_vpd_asset_tag(char *asset, size_t len);


#endif /* TSNIC_CORE_H_ */

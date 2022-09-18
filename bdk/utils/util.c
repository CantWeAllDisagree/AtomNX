/*
* Copyright (c) 2018 naehrwert
* Copyright (c) 2018-2022 CTCaer
* Copyright (c) 2020 Storm
* Copyright (c) 2020 CantWeAllDisagree

* This program is free software; you can redistribute it and/or modify it
* under the terms and conditions of the GNU General Public License,
* version 2, as published by the Free Software Foundation.
*
* This program is distributed in the hope it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <string.h>

#include <mem/heap.h>
#include <power/max77620.h>
#include <rtc/max77620-rtc.h>
#include <soc/bpmp.h>
#include <soc/hw_init.h>
#include <soc/i2c.h>
#include <soc/pmc.h>
#include <soc/timer.h>
#include <soc/t210.h>
#include <storage/sd.h>
#include <utils/util.h>

#define USE_RTC_TIMER

u8 bit_count(u32 val)
{
	u8 cnt = 0;
	for (u32 i = 0; i < 32; i++)
	{
		if ((val >> i) & 1)
			cnt++;
	}

	return cnt;
}

u32 bit_count_mask(u8 bits)
{
	u32 val = 0;
	for (u32 i = 0; i < bits; i++)
		val |= 1 << i;

	return val;
}

char *strcpy_ns(char *dst, char *src)
{
	if (!src || !dst)
		return NULL;

	// Remove starting space.
	u32 len = strlen(src);
	if (len && src[0] == ' ')
	{
		len--;
		src++;
	}

	strcpy(dst, src);

	// Remove trailing space.
	if (len && dst[len - 1] == ' ')
		dst[len - 1] = 0;

	return dst;
}

// Approximate square root finder for a 64-bit number.
u64 sqrt64(u64 num)
{
	u64 base = 0;
	u64 limit = num;
	u64 square_root = 0;

	while (base <= limit)
	{
		u64 tmp_sqrt = (base + limit) / 2;

		if (tmp_sqrt * tmp_sqrt == num) {
			square_root = tmp_sqrt;
			break;
		}

		if (tmp_sqrt * tmp_sqrt < num)
		{
			square_root = base;
			base = tmp_sqrt + 1;
		}
		else
			limit = tmp_sqrt - 1;
	}

	return square_root;
}

void exec_cfg(u32 *base, const cfg_op_t *ops, u32 num_ops)
{
	for(u32 i = 0; i < num_ops; i++)
		base[ops[i].off] = ops[i].val;
}

u32 crc32_calc(u32 crc, const u8 *buf, u32 len)
{
	const u8 *p, *q;
	static u32 *table = NULL;

	// Calculate CRC table.
	if (!table)
	{
		table = calloc(256, sizeof(u32));
		for (u32 i = 0; i < 256; i++)
		{
			u32 rem = i;
			for (u32 j = 0; j < 8; j++)
			{
				if (rem & 1)
				{
					rem >>= 1;
					rem ^= 0xedb88320;
				}
				else
					rem >>= 1;
			}
			table[i] = rem;
		}
	}

	crc = ~crc;
	q = buf + len;
	for (p = buf; p < q; p++)
	{
		u8 oct = *p;
		crc = (crc >> 8) ^ table[(crc & 0xff) ^ oct];
	}

	return ~crc;
}

void panic(u32 val)
{
	// Set panic code.
	PMC(APBDEV_PMC_SCRATCH200) = val;

	// Disable SE.
	//PMC(APBDEV_PMC_CRYPTO_OP) = PMC_CRYPTO_OP_SE_DISABLE;

	// Immediately cause a full system reset.
	watchdog_start(0, TIMER_PMCRESET_EN);

	while (true);
}

void power_set_state(power_state_t state)
{
	u8 reg;

	// Unmount and power down sd card.
	sd_end();

	// De-initialize and power down various hardware.
	hw_reinit_workaround(false, 0);

	// Stop the alarm, in case we injected and powered off too fast.
	max77620_rtc_stop_alarm();

	// Set power state.
	switch (state)
	{
	case REBOOT_RCM:
		PMC(APBDEV_PMC_SCRATCH0) = PMC_SCRATCH0_MODE_RCM; // Enable RCM path.
		PMC(APBDEV_PMC_CNTRL)   |= PMC_CNTRL_MAIN_RST;    // PMC reset.
		break;

	case REBOOT_BYPASS_FUSES:
		panic(0x21); // Bypass fuse programming in package1.
		break;

	case POWER_OFF:
		// Initiate power down sequence and do not generate a reset (regulators retain state after POR).
		i2c_send_byte(I2C_5, MAX77620_I2C_ADDR, MAX77620_REG_ONOFFCNFG1, MAX77620_ONOFFCNFG1_PWR_OFF);
		break;

	case POWER_OFF_RESET:
	case POWER_OFF_REBOOT:
	default:
		// Enable/Disable soft reset wake event.
		reg = i2c_recv_byte(I2C_5, MAX77620_I2C_ADDR, MAX77620_REG_ONOFFCNFG2);
		if (state == POWER_OFF_RESET) // Do not wake up after power off.
			reg &= ~(MAX77620_ONOFFCNFG2_SFT_RST_WK | MAX77620_ONOFFCNFG2_WK_ALARM1 | MAX77620_ONOFFCNFG2_WK_ALARM2);
		else // POWER_OFF_REBOOT. Wake up after power off.
			reg |= MAX77620_ONOFFCNFG2_SFT_RST_WK;
		i2c_send_byte(I2C_5, MAX77620_I2C_ADDR, MAX77620_REG_ONOFFCNFG2, reg);

		// Initiate power down sequence and generate a reset (regulators' state resets after POR).
		i2c_send_byte(I2C_5, MAX77620_I2C_ADDR, MAX77620_REG_ONOFFCNFG1, MAX77620_ONOFFCNFG1_SFT_RST);
		break;
	}

	while (true)
		bpmp_halt();
}

void power_set_state_ex(void *param)
{
	power_state_t *state = (power_state_t *)param;
	power_set_state(*state);
}


// String Replace AtomNX
#include <string.h>

char *str_replace(char *orig, char *rep, char *with)
{
	char *result;  // the return string
	char *ins;     // the next insert point
	char *tmp;     // varies
	int len_rep;   // length of rep (the string to remove)
	int len_with;  // length of with (the string to replace rep with)
	int len_front; // distance between rep and end of last rep
	int count;     // number of replacements

	// sanity checks and initialization
	if (!orig || !rep)
		return NULL;
	len_rep = strlen(rep);
	if (len_rep == 0)
		return NULL; // empty rep causes infinite loop during count
	if (!with)
		with = "";
	len_with = strlen(with);

	// count the number of replacements needed
	ins = orig;
	for (count = 0; (tmp = strstr(ins, rep)); ++count)
	{
		ins = tmp + len_rep;
	}

	tmp = result = malloc(strlen(orig) + (len_with - len_rep) * count + 1);

	if (!result)
		return NULL;

	// first time through the loop, all the variable are set correctly
	// from here on,
	//    tmp points to the end of the result string
	//    ins points to the next occurrence of rep in orig
	//    orig points to the remainder of orig after "end of rep"
	while (count--)
	{
		ins = strstr(orig, rep);
		len_front = ins - orig;
		tmp = strncpy(tmp, orig, len_front) + len_front;
		tmp = strcpy(tmp, with) + len_with;
		orig += len_front + len_rep; // move to next "end of rep"
	}
	strcpy(tmp, orig);
	return result;
}
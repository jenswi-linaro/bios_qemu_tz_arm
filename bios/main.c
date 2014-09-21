/*
 * Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "platform_config.h"

#include <compiler.h>
#include <types_ext.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <libfdt.h>
#include <drivers/uart.h>

/* Round up the even multiple of size, size has to be a multiple of 2 */
#define ROUNDUP(v, size) (((v) + (size - 1)) & ~(size - 1))

#define PAGE_SIZE	4096

extern const uint8_t __text_start;
extern const uint8_t __linker_secure_blob_start;
extern const uint8_t __linker_secure_blob_end;
extern const uint8_t __linker_nsec_blob_start;
extern const uint8_t __linker_nsec_blob_end;
extern const uint8_t __linker_nsec_dtb_start;
extern const uint8_t __linker_nsec_dtb_end;
extern const uint8_t __linker_nsec_rootfs_start;
extern const uint8_t __linker_nsec_rootfs_end;

static uint32_t main_stack[4098]
	__attribute__((section(".bss.prebss.stack"), aligned(8)));

const uint32_t main_stack_top = (uint32_t)main_stack + sizeof(main_stack);

#define CHECK(x) \
	do { \
		if ((x)) \
			check(#x, __FILE__, __LINE__); \
	} while (0)

#ifdef CONSOLE_UART_BASE
static void msg_init(void)
{
	uart_init(CONSOLE_UART_BASE);
}

static void __printf(1, 2) msg(const char *fmt, ...)
{
	va_list ap;
	char buf[128];
	char *p;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	for (p = buf; *p; p++) {
		uart_putc(*p, CONSOLE_UART_BASE);
		if (*p == '\n')
			uart_putc('\r', CONSOLE_UART_BASE);
	}
}
#else
static void msg_init(void)
{
}

static void __printf(1, 2) msg(const char *fmt __unused, ...)
{
}
#endif

static void check(const char *expr, const char *file, int line)
{
	msg("Check \"%s\": %s:%d\n", expr, file, line);
	while (true);
}

static const void *unreloc(const void *addr)
{
	return (void *)((uint32_t)addr - (uint32_t)&__text_start);
}

static uint32_t copy_bios_image(uint32_t dst, const uint8_t *start,
		const uint8_t *end)
{
	size_t l = (size_t)(end - start);

	msg("Copy image from %p to %p\n",
		unreloc(start), (void *)dst);

	memcpy((void *)dst, unreloc(start), l);
	return dst + l;
}

static uint32_t copy_bios_image_dtb(uint32_t dst, const uint8_t *start,
		const uint8_t *end)
{
	size_t l = ROUNDUP((size_t)(end - start), PAGE_SIZE) + PAGE_SIZE;
	int r;

	msg("Copy dtb from %p to %p\n",
		unreloc(start), (void *)dst);

	r = fdt_open_into(unreloc(start), (void *)dst, l);
	CHECK(r < 0);

	return dst + l;
}



uint32_t main_init_sec(void); /* called from assembly only */
uint32_t main_init_sec(void)
{
	uint32_t dst = TZ_RAM_START;

	msg_init();

	/* Copy secure image in place */
	copy_bios_image(dst, &__linker_secure_blob_start,
			&__linker_secure_blob_end);
	return dst;
}

static void setprop_cell(void *fdt, const char *node_path,
		const char *property, uint32_t val)
{
	int offs;
	int r;

	offs = fdt_path_offset(fdt, node_path);
	CHECK(offs < 0);

	r = fdt_setprop_cell(fdt, offs, property, val);
	CHECK(r < 0);
}

static void setprop_string(void *fdt, const char *node_path,
		const char *property, const char *string)
{
	int offs;
	int r;

	offs = fdt_path_offset(fdt, node_path);
	CHECK(offs < 0);

	r = fdt_setprop_string(fdt, offs, property, string);
	CHECK(r < 0);
}

#ifdef TZ_RES_MEM_START

static size_t get_cells_size(void *fdt, int offs, const char *cell_name)
{
	int len;
	const uint32_t *cell = fdt_getprop(fdt, offs, cell_name, &len);
	size_t cells_size;

	CHECK(len != sizeof(uint32_t));

	cells_size = fdt32_to_cpu(*cell);

	CHECK(cells_size != 1 && cells_size != 2);

	return cells_size;
}

static uint64_t get_val(const void *prop, size_t *offs, size_t cell_size)
{
	const void *addr = (const char *)prop + *offs;

	*offs += cell_size * sizeof(uint32_t);

	if (cell_size == 1) {
		uint32_t v;

		memcpy(&v, addr, sizeof(v));
		return fdt32_to_cpu(v);
	} else {
		uint64_t v;

		memcpy(&v, addr, sizeof(v));
		return fdt64_to_cpu(v);
	}
}

static void put_val(void *prop, size_t *offs, size_t cell_size,
		uint64_t val)
{
	void *addr = (char *)prop + *offs;

	*offs += cell_size * sizeof(uint32_t);

	if (cell_size == 1) {
		uint32_t v = cpu_to_fdt32((uint32_t)val);

		memcpy(addr, &v, sizeof(v));
	} else {
		uint64_t v = cpu_to_fdt64((uint32_t)val);

		memcpy(addr, &v, sizeof(v));
	}
}

static void tz_res_mem_carve(const void *prop, size_t plen,
			size_t addr_size, size_t len_size,
			void *data, size_t *dlen)
{
	const uint64_t tz_res_end = (uint64_t)TZ_RES_MEM_START +
				    TZ_RES_MEM_SIZE;
	size_t poffs;
	size_t doffs;
	uint64_t start;
	uint64_t len;
	uint64_t end;

	poffs = 0;
	msg("Original DTB memory: start len\n");
	while (poffs < plen) {
		start = get_val(prop, &poffs, addr_size);
		len = get_val(prop, &poffs, len_size);

		msg("0x%" PRIx64 " 0x%" PRIx64 "\n", start, len);
	}

	poffs = 0;
	doffs = 0;
	while (poffs < plen) {
		start = get_val(prop, &poffs, addr_size);
		len = get_val(prop, &poffs, len_size);
		end = start + len;

		if (TZ_RES_MEM_START == start && TZ_RES_MEM_SIZE == len) {
			/*
			 * Remove a region
			 */

		} else if (TZ_RES_MEM_START > start && tz_res_end < end) {
			/*
			 * Split a region
			 */
			put_val(data, &doffs, addr_size, start);
			put_val(data, &doffs, len_size,
					TZ_RES_MEM_START - start);

			put_val(data, &doffs, addr_size, tz_res_end);
			put_val(data, &doffs, len_size, end - tz_res_end);

		} else if (TZ_RES_MEM_START > start && TZ_RES_MEM_START < end) {
			/*
			 * Chop of the end of a region.
			 */
			put_val(data, &doffs, addr_size, start);
			put_val(data, &doffs, len_size,
					TZ_RES_MEM_START - start);
		} else if (tz_res_end < end) {
			/*
			 * Chop of the begining of a region.
			 */
			put_val(data, &doffs, addr_size, tz_res_end);
			put_val(data, &doffs, len_size, end - tz_res_end);
		}
	}
	CHECK(doffs > *dlen);
	*dlen = doffs;

	doffs = 0;
	msg("Carved out TZ memory from DTB memory: start len\n");
	while (doffs < *dlen) {
		start = get_val(data, &doffs, addr_size);
		len = get_val(data, &doffs, len_size);

		msg("0x%" PRIx64 " 0x%" PRIx64 "\n", start, len);
	}


}



static void tz_res_mem(void *fdt)
{
	int offs;
	const void *prop;
	int len;
	int r;
	size_t addr_size;
	size_t len_size;

	offs = fdt_subnode_offset(fdt, 0, "memory");
	CHECK(offs < 0);

	prop = fdt_getprop(fdt, offs, "reg", &len);
	CHECK(!prop);

	addr_size = get_cells_size(fdt, 0, "#address-cells");
	len_size = get_cells_size(fdt, 0, "#size-cells");

	{
		uint8_t data[len + addr_size + len_size];
		size_t dlen = sizeof(data);

		tz_res_mem_carve(prop, len, addr_size, len_size, data, &dlen);

		r = fdt_setprop(fdt, offs, "reg", data, dlen);
		CHECK(r < 0);
	}
}
#else
static void tz_res_mem(void *fdt __unused)
{
}
#endif


typedef void (*kernel_ep_func)(uint32_t a0, uint32_t a1, uint32_t a2);
static void call_kernel(uint32_t kernel_entry, uint32_t dtb,
		uint32_t rootfs, uint32_t rootfs_end)
{
	kernel_ep_func ep = (kernel_ep_func)kernel_entry;
	void *fdt = (void *)dtb;
	const char cmdline[] = "console=ttyAMA0,115200 dynamic_debug.verbose=1";
	int r;
	const uint32_t a0 = 0;
	const uint32_t a1 = 2272; /*MACH_VEXPRESS*/

	tz_res_mem(fdt);
	setprop_cell(fdt, "/chosen", "linux,initrd-start", rootfs);
	setprop_cell(fdt, "/chosen", "linux,initrd-end", rootfs_end);
	setprop_string(fdt, "/chosen", "bootargs", cmdline);
	r = fdt_pack(fdt);
	CHECK(r < 0);

	msg("kernel command line: \"%s\"\n", cmdline);
	msg("Entering kernel at 0x%x with r0=0x%x r1=0x%x r2=0x%x\n",
		(uintptr_t)ep, a0, a1, dtb);
	ep(a0, a1, dtb);
}

void main_init_ns(void); /* called from assembly only */
void main_init_ns(void)
{
	uint32_t dst;
	/* 32MiB above beginning of RAM */
	uint32_t kernel_entry = BIOS_RAM_START + 32 * 1024 * 1024;
	uint32_t dtb;
	uint32_t rootfs;
	uint32_t rootfs_end;

	/* Copy non-secure image in place */
	dst = copy_bios_image(kernel_entry, &__linker_nsec_blob_start,
			&__linker_nsec_blob_end);

	dtb = ROUNDUP(dst, PAGE_SIZE) + 96 * 1024 * 1024; /* safe spot */
	dst = copy_bios_image_dtb(dtb, &__linker_nsec_dtb_start,
			&__linker_nsec_dtb_end);

	rootfs = ROUNDUP(dst, PAGE_SIZE);
	rootfs_end = copy_bios_image(rootfs, &__linker_nsec_rootfs_start,
				     &__linker_nsec_rootfs_end);

	call_kernel(kernel_entry, dtb, rootfs, rootfs_end);
}


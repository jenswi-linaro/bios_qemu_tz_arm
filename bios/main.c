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

#ifndef MAX
#define MAX(a, b) \
	(__extension__({ __typeof__(a) _a = (a); \
			 __typeof__(b) _b = (b); \
			 _a > _b ? _a : _b; }))

#define MIN(a, b) \
	(__extension__({ __typeof__(a) _a = (a); \
			 __typeof__(b) _b = (b); \
			 _a < _b ? _a : _b; }))
#endif


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

static uint32_t copy_bios_image(const char *name, uint32_t dst,
		const uint8_t *start, const uint8_t *end)
{
	size_t l = (size_t)(end - start);

	msg("Copy image \"%s\" from %p to %p\n",
		name, unreloc(start), (void *)dst);

	memcpy((void *)dst, unreloc(start), l);
	return dst + l;
}

static void *open_fdt(uint32_t dst, const uint8_t *start, const uint8_t *end)
{
	int r;
	const void *s;

	if (start != end) {
		msg("Using hardcoded DTB\n");
		CHECK((size_t)(end - start) > DTB_MAX_SIZE);
		s = unreloc(start);
		msg("Copy dtb from %p to %p\n", s, (void *)dst);
	} else {
		s = (void *)dst;
		msg("Using QEMU provided DTB at %p\n", s);
	}

	r = fdt_open_into(s, (void *)dst, DTB_MAX_SIZE);
	CHECK(r < 0);

	return (void *)dst;
}

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

static void tz_res_mem_check_avail(const void *prop, size_t plen,
			size_t addr_size, size_t len_size)
{
	uint64_t res_start = TZ_RES_MEM_START;
	uint64_t res_end = res_start + TZ_RES_MEM_SIZE;
	uint64_t last_res_start;
	uint64_t last_res_end;
	size_t offs;
	uint64_t start;
	uint64_t len;
	uint64_t end;

	msg("Checking that secure memory is available\n");
	msg("0x%" PRIx64 " .. 0x%" PRIx64 "\n", res_start, res_end);
	msg("Available memory\n");
	for (offs = 0; offs < plen;) {
		start = get_val(prop, &offs, addr_size);
		len = get_val(prop, &offs, len_size);
		end = start + len;
		msg("0x%" PRIx64 " .. 0x%" PRIx64 "\n", start, end);
	}

	do {
		last_res_start = res_start;
		last_res_end = res_end;
		offs = 0;
		while (offs < plen && res_start != res_end) {
			start = get_val(prop, &offs, addr_size);
			len = get_val(prop, &offs, len_size);
			end = start + len;

			if (start <= res_start && end >= res_end)
				goto mem_avail;

			if (start <= res_start && end > res_start) {
				/*
				 * Found beginning of reserved memory in
				 * this memory block.
				 */
				res_start = MIN(end, res_end);
			} else if (end >= res_end && start < res_end) {
				/*
				 * Found end of of reserved memory in
				 * this memory block.
				 */
				res_end = MAX(start, res_start);
			}

			/*
			 * Here either the current memory block doesn't
			 * cover the reserved memroy at all or we would
			 * need to split the reserved memroy region for
			 * book keeping. Instead we're looping through all
			 * memory blocks trying to find of start and end of
			 * reserved memory until everything is accounted
			 * for. That means that we may need to loop over
			 * the memory blocks several times.
			 */

		}

		if (res_start == res_end)
			goto mem_avail;

		/*
		 * Loop through the memory list until there's no changes in
		 * the amount of reserved memory accounted for.
		 */
	} while (last_res_start != res_start || last_res_end != res_end);

	msg("Can't find secure memory\n");
	msg("0x%" PRIx64 " .. 0x%" PRIx64 "\n", res_start, res_end);
	CHECK(1);

mem_avail:
	msg("Secure memory is available\n");
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

	tz_res_mem_check_avail(prop, len, addr_size, len_size);

	{
		uint8_t data[len + (addr_size + len_size) * sizeof(uint32_t)];
		size_t dlen = sizeof(data);

		tz_res_mem_carve(prop, len, addr_size, len_size, data, &dlen);

		r = fdt_setprop(fdt, offs, "reg", data, dlen);
		CHECK(r < 0);
	}
}


#ifdef TZ_UART_SHARED
static void tz_res_uart(void *fdt __unused)
{
	msg("Uart shared between secure and non-secure world\n");
}
#else

static bool node_is_compatible(void *fdt, int offs, const char *compat)
{
	int plen;
	const char *prop;

	prop = fdt_getprop(fdt, offs, "compatible", &plen);
	if (!prop)
		return false;

	while (plen > 0) {
		size_t l;

		if (strcmp(prop, compat) == 0)
			return true;

		l = strlen(prop) + 1;
		prop += l;
		plen -= l;
	}

	return false;
}

static bool has_reg_base(void *fdt, int offs, size_t addr_size,
			uintptr_t base)
{
	int plen;
	size_t poffs = 0;
	const void *prop;
	uintptr_t prop_base;

	prop = fdt_getprop(fdt, offs, "reg", &plen);
	if (!prop)
		return false;

	prop_base = get_val(prop, &poffs, addr_size);
	return prop_base == base;
}

static void tz_res_uart(void *fdt)
{
	int r;
	int offs = 0;
	const char *name;
	size_t addr_size;

	addr_size = get_cells_size(fdt, 0, "#address-cells");

	while (true) {
		offs = fdt_next_node(fdt, offs, NULL);
		if (offs < 0)
			break;
		name = fdt_get_name(fdt, offs, NULL);
		if (!node_is_compatible(fdt, offs, "arm,pl011"))
			continue;
		if (!has_reg_base(fdt, offs, addr_size, UART1_BASE))
			continue;
		msg("Removing node \"%s\" from DTB passed to kernel\n", name);
		r = fdt_del_node(fdt, offs);
		CHECK(r < 0);
		break;
	}
}
#endif

static void tz_add_optee_node(void *fdt)
{
	int offs;
	int ret;

	offs = fdt_path_offset(fdt, "/");
	CHECK(offs < 0);
	offs = fdt_add_subnode(fdt, offs, "firmware");
	CHECK(offs < 0);
	offs = fdt_add_subnode(fdt, offs, "optee");
	CHECK(offs < 0);
	ret = fdt_setprop_string(fdt, offs, "compatible", "linaro,optee-tz");
	CHECK(ret < 0);
}

#define OPTEE_MAGIC		0x4554504f
#define OPTEE_VERSION		1
#define OPTEE_ARCH_ARM32	0
#define OPTEE_ARCH_ARM64	1


struct optee_header {
	uint32_t magic;
	uint8_t version;
	uint8_t arch;
	uint16_t flags;
	uint32_t init_size;
	uint32_t init_load_addr_hi;
	uint32_t init_load_addr_lo;
	uint32_t init_mem_usage;
	uint32_t paged_size;
};


struct sec_entry_arg {
	uint32_t entry;
	uint32_t paged_part;
};
/* called from assembly only */
void main_init_sec(struct sec_entry_arg *arg);
void main_init_sec(struct sec_entry_arg *arg)
{
	uint32_t dst = TZ_RAM_START;
	void *fdt;
	int r;
	const uint8_t *sblob_start = &__linker_secure_blob_start;
	const uint8_t *sblob_end = &__linker_secure_blob_end;
	struct optee_header hdr;

	msg_init();

	/* Find DTB */
	fdt = open_fdt(DTB_START, &__linker_nsec_dtb_start,
			&__linker_nsec_dtb_end);
	tz_res_mem(fdt);
	tz_res_uart(fdt);
	tz_add_optee_node(fdt);
	r = fdt_pack(fdt);
	CHECK(r < 0);

	arg->paged_part = 0;
	arg->entry = dst;

	/* Look for a header first */
	if (((intptr_t)sblob_end - (intptr_t)sblob_start) >=
			(ssize_t)sizeof(hdr)) {
		copy_bios_image("secure header", (uint32_t)&hdr, sblob_start,
				sblob_start + sizeof(hdr));

		if (hdr.magic == OPTEE_MAGIC && hdr.version == OPTEE_VERSION) {
			size_t pg_part_size;
			uint32_t pg_part_dst;

			msg("found secure header\n");
			sblob_start += sizeof(hdr);
			CHECK(hdr.init_load_addr_hi != 0);
			CHECK(hdr.init_load_addr_lo != dst);

			pg_part_size = sblob_end - sblob_start - hdr.init_size;
			pg_part_dst = (size_t)TZ_RES_MEM_START +
					TZ_RES_MEM_SIZE - pg_part_size;

			copy_bios_image("secure paged part", pg_part_dst,
				sblob_start + hdr.init_size, sblob_end);

			sblob_end -= pg_part_size;
			arg->paged_part = pg_part_dst;
			arg->entry = hdr.init_load_addr_lo;
		}
	}

	/* Copy secure image in place */
	copy_bios_image("secure blob", dst, sblob_start, sblob_end);
	msg("Initializing secure world\n");
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

typedef void (*kernel_ep_func)(uint32_t a0, uint32_t a1, uint32_t a2);
static void call_kernel(uint32_t kernel_entry, uint32_t dtb,
		uint32_t rootfs, uint32_t rootfs_end)
{
	kernel_ep_func ep = (kernel_ep_func)kernel_entry;
	void *fdt = (void *)dtb;
	const char cmdline[] =
"console=ttyAMA0,115200 earlyprintk=serial,ttyAMA0,115200 dynamic_debug.verbose=1";
	int r;
	const uint32_t a0 = 0;
	/*MACH_VEXPRESS see linux/arch/arm/tools/mach-types*/
	const uint32_t a1 = 2272;

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

static uint32_t copy_dtb(uint32_t dst, uint32_t src)
{
	int r;

	msg("Relocating DTB for kernel use at %p\n", (void *)dst);
	r = fdt_open_into((void *)src, (void *)dst, DTB_MAX_SIZE);
	CHECK(r < 0);
	return dst + DTB_MAX_SIZE;
}

void main_init_ns(void); /* called from assembly only */
void main_init_ns(void)
{
	uint32_t dst;
	/* 32MiB above beginning of RAM */
	uint32_t kernel_entry = DRAM_START + 32 * 1024 * 1024;
	uint32_t dtb;
	uint32_t rootfs;
	uint32_t rootfs_end;

	/* Copy non-secure image in place */
	dst = copy_bios_image("kernel", kernel_entry, &__linker_nsec_blob_start,
			&__linker_nsec_blob_end);

	dtb = ROUNDUP(dst, PAGE_SIZE) + 96 * 1024 * 1024; /* safe spot */
	dst = copy_dtb(dtb, DTB_START);

	rootfs = ROUNDUP(dst + DTB_MAX_SIZE, PAGE_SIZE);
	rootfs_end = copy_bios_image("rootfs", rootfs,
			&__linker_nsec_rootfs_start, &__linker_nsec_rootfs_end);

	call_kernel(kernel_entry, dtb, rootfs, rootfs_end);
}

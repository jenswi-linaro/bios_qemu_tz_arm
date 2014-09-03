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

#include <types_ext.h>
#include <string.h>
#include <libfdt.h>

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

static const void *unreloc(const void *addr)
{
	return (void *)((uint32_t)addr - (uint32_t)&__text_start);
}

static uint32_t copy_bios_image(uint32_t dst, const uint8_t *start,
		const uint8_t *end)
{
	size_t l = (size_t)(end - start);

	memcpy((void *)dst, unreloc(start), l);
	return dst + l;
}

static uint32_t copy_bios_image_dtb(uint32_t dst, const uint8_t *start,
		const uint8_t *end)
{
	size_t l = ROUNDUP((size_t)(end - start), PAGE_SIZE) + PAGE_SIZE;
	int r;

	r = fdt_open_into(unreloc(start), (void *)dst, l);
	while (r < 0);

	return dst + l;
}



uint32_t main_init_sec(void); /* called from assembly only */
uint32_t main_init_sec(void)
{
	uint32_t dst = TZ_RAM_START;

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
	while (offs < 0);

	r = fdt_setprop_cell(fdt, offs, property, val);
	while (r < 0);
}

static void setprop_string(void *fdt, const char *node_path,
		const char *property, const char *string)
{
	int offs;
	int r;

	offs = fdt_path_offset(fdt, node_path);
	while (offs < 0);

	r = fdt_setprop_string(fdt, offs, property, string);
	while (r < 0);
}

typedef void (*kernel_ep_func)(uint32_t a0, uint32_t a1, uint32_t a2);
static void call_kernel(uint32_t kernel_entry, uint32_t dtb,
		uint32_t rootfs, uint32_t rootfs_end)
{
	kernel_ep_func ep = (kernel_ep_func)kernel_entry;
	void *fdt = (void *)dtb;
	const char cmdline[] = "console=ttyAMA0,115200 dynamic_debug.verbose=1";
	int r;

	(void)&dtb;
	(void)&rootfs;

	setprop_cell(fdt, "/chosen", "linux,initrd-start", rootfs);
	setprop_cell(fdt, "/chosen", "linux,initrd-end", rootfs_end);
	setprop_string(fdt, "/chosen", "bootargs", cmdline);
	r = fdt_pack(fdt);
	while (r < 0);

	ep(0, 2272 /*MACH_VEXPRESS*/, dtb);
}

void main_init_ns(void); /* called from assembly only */
void main_init_ns(void)
{
	uint32_t dst;
	/* 32MiB above beginning of RAM */
	uint32_t kernel_entry = 0x80000000 + 32 * 1024 * 1024;
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


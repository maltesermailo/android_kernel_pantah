// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 */

#include <nvhe/pkvm.h>
#include <nvhe/spinlock.h>

static void (*__hyp_putc)(char c);

static inline void __hyp_putx4(unsigned int x)
{
	x &= 0xf;
	if (x <= 9)
		x += '0';
	else
		x += ('a' - 0xa);

	__hyp_putc(x);
}

static inline void __hyp_putx4n(unsigned long x, int n)
{
	int i = n >> 2;
	while (i--)
		__hyp_putx4(x >> (4 * i));
}

static inline void __hyp_putl(u64 l)
{
	u64 r = 0;
	u32 digits = 0;
	/* Reverse. */
	do {
		r = r * 10 + l % 10;
		l /= 10;
		digits++;
	} while (l);
	/* Display. */
	while (digits--) {
		__hyp_putc(r % 10 + '0');
		r /= 10;
	}
}

static inline void __hyp_puts(const char *s)
{
	while (*s)
		__hyp_putc(*s++);
}

static inline bool hyp_serial_enabled(void)
{
	/* Paired with __pkvm_register_serial_driver()'s cmpxchg */
	return !!smp_load_acquire(&__hyp_putc);
}

void hyp_puts(const char *s)
{
	if (s && hyp_serial_enabled()) {
		__hyp_puts(s);

		__hyp_putc('\n');
		__hyp_putc('\r');
	}
}

void hyp_putx64(u64 x)
{
	if (hyp_serial_enabled()) {
		__hyp_putc('0');
		__hyp_putc('x');

		__hyp_putx4n(x, 64);

		__hyp_putc('\n');
		__hyp_putc('\r');
	}
}

void hyp_putc(char c)
{
	if (hyp_serial_enabled())
		__hyp_putc(c);
}

/*
 * Hypervisor formatted print string.
 * Supported formats: %s %d %ld %lld %u %lu %llu %x %lx %llx.
 * Anything else has undefined behaviour and might(mostly) crash.
 * Only unsigned decimals are supported.
 */
DEFINE_HYP_SPINLOCK(printf_lock);
static const char hyp_printf_prefix[] = "[pKVM EL2] ";

void hyp_printf(const char *fmt, ...)
{
	va_list ap;
	u64 i;
	const char *s;

	if (!fmt || !hyp_serial_enabled())
		return;

	hyp_spin_lock(&printf_lock);

	/* Print the prefix string */
	__hyp_puts(hyp_printf_prefix);

	va_start(ap, fmt);
	while (*fmt) {
		if (*fmt == '%') {
			fmt++;
			if (!*fmt)  /* Handle trailing '%' */
				break;
			switch (*fmt) {
			case 'x':
				i = va_arg(ap, u32);
				__hyp_putx4n(i, 32);
				break;
			case 's':
				s = va_arg(ap, char*);
				if (s)
					__hyp_puts(s);
				else
					__hyp_puts("(null)");
				break;
			case 'd':
			case 'u':
				i = va_arg(ap, u32);
				__hyp_putl(i);
				break;
			case 'l':
				fmt++;
				if (!*fmt)
					break;
				if (*fmt == 'l') {
					fmt++;
					if (!*fmt)
						break;
				}
				i = va_arg(ap, u64);
				if (*fmt == 'x')
					__hyp_putx4n(i, 64);
				else if (*fmt == 'd' || *fmt == 'u')
					__hyp_putl(i);
				break;
			default:
				break;	/* unsupported format */
			}
		} else {
			__hyp_putc(*fmt);
			if (*fmt == '\n')
				__hyp_putc('\r');
		}
		fmt++;
	}
	va_end(ap);

	hyp_spin_unlock(&printf_lock);
}

int __pkvm_register_serial_driver(void (*cb)(char))
{
	/*
	 * Paired with smp_load_acquire(&__hyp_putc) in
	 * hyp_serial_enabled(). Ensure memory stores hapenning during a pKVM
	 * module init are observed before executing the callback.
	 */
	return cmpxchg_release(&__hyp_putc, NULL, cb) ? -EBUSY : 0;
}

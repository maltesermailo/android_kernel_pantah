# Exynos FMP/SMU makefile
#ccflags-y := -DCONFIG_EXYNOS_FMP_FIPS_FUNC_TEST
CC_NO_CLANG := $(shell $(CC) -dM -E -x c /dev/null | grep -Fq "__clang__"; echo $$?)
ifeq ($(CC_NO_CLANG), 0)
ccflags-y += -DCC_USE_CLANG
CFLAGS_fmp_fips_selftest.o = -fno-merge-all-constants
endif
ifeq ($(CC_NO_CLANG), 1)
CFLAGS_fmp_fips_selftest.o = -fno-merge-constants
endif
ccflags-y += $(DISABLE_LTO_CLANG) $(DISABLE_CFI_CLANG) -fno-data-sections -fno-function-sections

obj-$(CONFIG_EXYNOS_FMP)   += fmp-core.o
fmp-core-$(CONFIG_EXYNOS_FMP_FIPS) += first_file.o
fmp-core-$(CONFIG_EXYNOS_FMP) += fmp.o fmp_test.o
fmp-core-$(CONFIG_EXYNOS_FMP_FIPS) += fmp_fips_main.o fmp_fips_fops.o fmp_fips_selftest.o \
				fmp_fips_integrity.o hmac-sha256.o \
				fmp_fips_cipher.o sha256.o
#obj-y += fmp_fips_func_test.o
fmp-core-$(CONFIG_EXYNOS_FMP_FIPS) += last_file.o
fmp-core-$(CONFIG_EXYNOS_FMP_FIPS) += fips_out.o

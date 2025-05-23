# SPDX-License-Identifier: GPL-2.0-only
ccflags-y += -I$(src)	# needed for trace events

obj-$(CONFIG_ANDROID_BINDER_IPC_RUST)	+= rust_binder.o
obj-$(CONFIG_ANDROID_BINDER_IPC_RUST_DUMMY)	+= rust_binder.o

ifdef CONFIG_ANDROID_BINDER_IPC_RUST
rust_binder-y := \
	rust_binder_main.o	\
	rust_binderfs.o		\
	rust_binder_events.o	\
	page_range_helper.o
else
rust_binder-y := dummy.o
endif

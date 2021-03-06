# Get the source directory with symlinks resolved
STMFB_ORIGINAL_SOURCE_PATH = $(shell cd ..;/bin/pwd)
STG_TOPDIR=$(STMFB_ORIGINAL_SOURCE_PATH)


export STMFB_ORIGINAL_SOURCE_PATH
export STG_TOPDIR
export CONFIG_STG_DEBUG
export INSTALL_MOD_PATH
export HDMI_I2C_ADAPTER_ID HDMI_HOTPLUG_PIO STMFB_CLOCK_REF_30MHZ_ERROR STMFB_MAX_DAC123_VOLTAGE STMFB_MAX_DAC456_VOLTAGE HDMI_148MHZ_PRE_EMPHASIS HDMI_148MHZ_PRE_EMPHASIS_ON HDMI_HOTPLUG_PIO_INVERT

KERNELDIR ?= $(error KERNELDIR is not set)

KBUILD_PATH = $(STG_TOPDIR)/linux/kbuild
			
CROSS_COMPILE ?=sh4-stlinux24-linux-gnu-

TEST_TMP1 = $(wildcard $(STG_TOPDIR)/linux/tests/*)
TEST_TMP2 = $(filter-out %/gtk,$(TEST_TMP1))
TEST_SUBDIRS = $(filter-out %/v4l2_helper.h,$(TEST_TMP2))

all: modules tests

modules:
	make -C $(KERNELDIR) $(patsubst %,O=%,$O) M="$(KBUILD_PATH)" ARCH=sh CROSS_COMPILE="$(CROSS_COMPILE)" V=1 modules

.PHONY: modules_install
modules_install:
	make -C $(KERNELDIR) $(patsubst %,O=%,$O) M="$(KBUILD_PATH)" ARCH=sh CROSS_COMPILE="$(CROSS_COMPILE)" modules_install

tests: ACTION = all
tests: $(TEST_SUBDIRS)

clean: ACTION = clean
clean: $(TEST_SUBDIRS)
	find .. \( -name .git -prune \
			-o -name '.*.flags' -o -name '.depend' -o -name '.*.o.d' \
			-o -name '.*.cmd' -o -name '*.mod.c' -o -name '*.[oas]' \
			-o -name '*.ko' -o -name '*.map' -o -name '*.bak' \
			-o -name 'cfb*.c' -o -name Module.symvers \) -type f -print | xargs rm -f
	find . -name .tmp_versions -type d -print | xargs rm -rf

.PHONY: dep
dep:
	make -C $(KERNELDIR) SUBDIRS="$(KERNEL_SUBDIRS)" dep

.PHONY: $(TEST_SUBDIRS)
$(TEST_SUBDIRS):
	make -C $@ $(ACTION)

ifeq ($(subdir),csu)
aeabi_constants = aeabi_lcsts aeabi_sighandlers aeabi_math
aeabi_routines = aeabi_assert aeabi_localeconv aeabi_errno_addr \
	aeabi_mb_cur_max aeabi_atexit aeabi_memclr aeabi_memcpy \
	aeabi_memmove aeabi_memset

sysdep_routines += $(aeabi_constants) $(aeabi_routines)
static-only-routines += $(aeabi_constants)

# get offset to rtld_global._dl_hwcap
gen-as-const-headers += rtld-global-offsets.sym
endif

ifeq ($(subdir),elf)
sysdep_routines += aeabi_unwind_cpp_pr1 find_exidx
shared-only-routines += aeabi_unwind_cpp_pr1
sysdep-rtld-routines += aeabi_unwind_cpp_pr1
endif

ifeq ($(subdir),math)
$(objpfx)libm.so: $(elfobjdir)/ld.so
endif

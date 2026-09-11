savedcmd_../proto/identity.o := gcc -Wp,-MMD,../proto/.identity.o.d -nostdinc -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/generated/uapi -include /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler-version.h -include /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kconfig.h -include /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler_types.h -D__KERNEL__ -mlittle-endian -DCC_USING_PATCHABLE_FUNCTION_ENTRY -DKASAN_SHADOW_SCALE_SHIFT= -fshort-wchar -funsigned-char -fno-common -fno-PIE -fno-strict-aliasing -std=gnu11 -fms-extensions -mgeneral-regs-only -DCONFIG_CC_HAS_K_CONSTRAINT=1 -Wno-psabi -mabi=lp64 -fno-asynchronous-unwind-tables -fno-unwind-tables -mbranch-protection=pac-ret -Wa,-march=armv8.5-a -DARM64_ASM_ARCH='"armv8.5-a"' -DKASAN_SHADOW_SCALE_SHIFT= -fno-delete-null-pointer-checks -O2 -fno-allow-store-data-races -fstack-protector-strong -fno-omit-frame-pointer -fno-optimize-sibling-calls -ftrivial-auto-var-init=zero -fzero-init-padding-bits=all -fno-stack-clash-protection -fdiagnostics-show-context=2 -fpatchable-function-entry=4,2 -fno-inline-functions-called-once -fmin-function-alignment=8 -fstrict-flex-arrays=3 -fno-strict-overflow -fno-stack-check -fconserve-stack -fno-builtin-wcslen -Wall -Wextra -Wundef -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Werror=strict-prototypes -Wno-format-security -Wno-trigraphs -Wno-frame-address -Wno-address-of-packed-member -Wmissing-declarations -Wmissing-prototypes -Wframe-larger-than=2048 -Wno-main -Wno-type-limits -Wno-dangling-pointer -Wvla-larger-than=1 -Wno-pointer-sign -Wcast-function-type -Wno-unterminated-string-initialization -Wno-array-bounds -Wno-stringop-overflow -Wno-alloc-size-larger-than -Wimplicit-fallthrough=5 -Werror=date-time -Werror=incompatible-pointer-types -Werror=designated-init -Wenum-conversion -Wunused -Wno-unused-but-set-variable -Wno-unused-const-variable -Wno-packed-not-aligned -Wno-format-overflow -Wno-format-truncation -Wno-stringop-truncation -Wno-override-init -Wno-missing-field-initializers -Wno-shift-negative-value -Wno-maybe-uninitialized -Wno-sign-compare -Wno-unused-parameter -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=2168 -Wall -Wextra -Wno-unused-parameter -I./. -I././..  -fsanitize=bounds-strict -fsanitize=shift    -DMODULE  -DKBUILD_BASENAME='"identity"' -DKBUILD_MODNAME='"thunderbolt_ibverbs"' -D__KBUILD_MODNAME=thunderbolt_ibverbs -c -o ../proto/identity.o ../proto/identity.c  

source_../proto/identity.o := ../proto/identity.c

deps_../proto/identity.o := \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler-version.h \
    $(wildcard include/config/CC_VERSION_TEXT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kconfig.h \
    $(wildcard include/config/CPU_BIG_ENDIAN) \
    $(wildcard include/config/BOOGER) \
    $(wildcard include/config/FOO) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler_types.h \
    $(wildcard include/config/DEBUG_INFO_BTF) \
    $(wildcard include/config/PAHOLE_HAS_BTF_TAG) \
    $(wildcard include/config/FUNCTION_ALIGNMENT) \
    $(wildcard include/config/CC_HAS_SANE_FUNCTION_ALIGNMENT) \
    $(wildcard include/config/X86_64) \
    $(wildcard include/config/ARM64) \
    $(wildcard include/config/LD_DEAD_CODE_DATA_ELIMINATION) \
    $(wildcard include/config/LTO_CLANG) \
    $(wildcard include/config/HAVE_ARCH_COMPILER_H) \
    $(wildcard include/config/KCSAN) \
    $(wildcard include/config/CC_HAS_ASSUME) \
    $(wildcard include/config/CC_HAS_COUNTED_BY) \
    $(wildcard include/config/FORTIFY_SOURCE) \
    $(wildcard include/config/UBSAN_BOUNDS) \
    $(wildcard include/config/CC_HAS_COUNTED_BY_PTR) \
    $(wildcard include/config/CC_HAS_MULTIDIMENSIONAL_NONSTRING) \
    $(wildcard include/config/CFI) \
    $(wildcard include/config/ARCH_USES_CFI_GENERIC_LLVM_PASS) \
    $(wildcard include/config/CC_HAS_BROKEN_COUNTED_BY_REF) \
    $(wildcard include/config/CC_HAS_ASM_INLINE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler-context-analysis.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler_attributes.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler-gcc.h \
    $(wildcard include/config/ARCH_USE_BUILTIN_BSWAP) \
    $(wildcard include/config/SHADOW_CALL_STACK) \
    $(wildcard include/config/KCOV) \
    $(wildcard include/config/CC_HAS_TYPEOF_UNQUAL) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/compiler.h \
    $(wildcard include/config/ARM64_PTR_AUTH_KERNEL) \
    $(wildcard include/config/ARM64_PTR_AUTH) \
    $(wildcard include/config/BUILTIN_RETURN_ADDRESS_STRIPS_PAC) \
  ../proto/identity.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/errno.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/errno.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi/asm/errno.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/errno.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/errno-base.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/types.h \
    $(wildcard include/config/HAVE_UID16) \
    $(wildcard include/config/UID16) \
    $(wildcard include/config/ARCH_DMA_ADDR_T_64BIT) \
    $(wildcard include/config/PHYS_ADDR_T_64BIT) \
    $(wildcard include/config/64BIT) \
    $(wildcard include/config/ARCH_32BIT_USTAT_F_TINODE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi/asm/types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/int-ll64.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/int-ll64.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/bitsperlong.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitsperlong.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/bitsperlong.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/posix_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/stddef.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/stddef.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/posix_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/posix_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/string.h \
    $(wildcard include/config/BINARY_PRINTF) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/args.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/array_size.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler.h \
    $(wildcard include/config/TRACE_BRANCH_PROFILING) \
    $(wildcard include/config/PROFILE_ALL_BRANCHES) \
    $(wildcard include/config/OBJTOOL) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/rwonce.h \
    $(wildcard include/config/LTO) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/rwonce.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kasan-checks.h \
    $(wildcard include/config/KASAN_GENERIC) \
    $(wildcard include/config/KASAN_SW_TAGS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kcsan-checks.h \
    $(wildcard include/config/KCSAN_WEAK_MEMORY) \
    $(wildcard include/config/KCSAN_IGNORE_ATOMICS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/cleanup.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/err.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/overflow.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/limits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/limits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/limits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/const.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/const.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/const.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/stdarg.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/string.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/string.h \
    $(wildcard include/config/ARCH_HAS_UACCESS_FLUSHCACHE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/fortify-string.h \
    $(wildcard include/config/CC_HAS_KASAN_MEMINTRINSIC_PREFIX) \
    $(wildcard include/config/GENERIC_ENTRY) \
    $(wildcard include/config/KMSAN) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bug.h \
    $(wildcard include/config/GENERIC_BUG) \
    $(wildcard include/config/PRINTK) \
    $(wildcard include/config/BUG_ON_DATA_CORRUPTION) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/bug.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/stringify.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/asm-bug.h \
    $(wildcard include/config/DEBUG_BUGVERBOSE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/brk-imm.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bug.h \
    $(wildcard include/config/DEBUG_BUGVERBOSE_DETAILED) \
    $(wildcard include/config/BUG) \
    $(wildcard include/config/GENERIC_BUG_RELATIVE_POINTERS) \
    $(wildcard include/config/SMP) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/instrumentation.h \
    $(wildcard include/config/NOINSTR_VALIDATION) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/once_lite.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/panic.h \
    $(wildcard include/config/PANIC_TIMEOUT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/printk.h \
    $(wildcard include/config/MESSAGE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_QUIET) \
    $(wildcard include/config/EARLY_PRINTK) \
    $(wildcard include/config/PRINTK_INDEX) \
    $(wildcard include/config/DYNAMIC_DEBUG) \
    $(wildcard include/config/DYNAMIC_DEBUG_CORE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/init.h \
    $(wildcard include/config/MEMORY_HOTPLUG) \
    $(wildcard include/config/HAVE_ARCH_PREL32_RELOCATIONS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/build_bug.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kern_levels.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/linkage.h \
    $(wildcard include/config/ARCH_USE_SYM_ANNOTATIONS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/export.h \
    $(wildcard include/config/MODVERSIONS) \
    $(wildcard include/config/GENDWARFKSYMS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/linkage.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/ratelimit_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/bits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/bits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/param.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/param.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/param.h \
    $(wildcard include/config/HZ) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/param.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/spinlock_types_raw.h \
    $(wildcard include/config/DEBUG_SPINLOCK) \
    $(wildcard include/config/DEBUG_LOCK_ALLOC) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/spinlock_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/qspinlock_types.h \
    $(wildcard include/config/NR_CPUS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/qrwlock_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/byteorder.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/byteorder/little_endian.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/byteorder/little_endian.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/swab.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/swab.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi/asm/swab.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/swab.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/byteorder/generic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/lockdep_types.h \
    $(wildcard include/config/PROVE_RAW_LOCK_NESTING) \
    $(wildcard include/config/LOCKDEP) \
    $(wildcard include/config/LOCK_STAT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/dynamic_debug.h \
    $(wildcard include/config/JUMP_LABEL) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/jump_label.h \
    $(wildcard include/config/HAVE_ARCH_JUMP_LABEL_RELATIVE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/jump_label.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/insn.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/insn-def.h \

../proto/identity.o: $(deps_../proto/identity.o)

$(deps_../proto/identity.o):

savedcmd_.module-common.o := gcc -Wp,-MMD,./..module-common.o.d -nostdinc -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi -I/mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/generated/uapi -include /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler-version.h -include /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kconfig.h -include /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/compiler_types.h -D__KERNEL__ -mlittle-endian -DCC_USING_PATCHABLE_FUNCTION_ENTRY -DKASAN_SHADOW_SCALE_SHIFT= -fshort-wchar -funsigned-char -fno-common -fno-PIE -fno-strict-aliasing -std=gnu11 -fms-extensions -mgeneral-regs-only -DCONFIG_CC_HAS_K_CONSTRAINT=1 -Wno-psabi -mabi=lp64 -fno-asynchronous-unwind-tables -fno-unwind-tables -mbranch-protection=pac-ret -Wa,-march=armv8.5-a -DARM64_ASM_ARCH='"armv8.5-a"' -DKASAN_SHADOW_SCALE_SHIFT= -fno-delete-null-pointer-checks -O2 -fno-allow-store-data-races -fstack-protector-strong -fno-omit-frame-pointer -fno-optimize-sibling-calls -ftrivial-auto-var-init=zero -fzero-init-padding-bits=all -fno-stack-clash-protection -fdiagnostics-show-context=2 -fpatchable-function-entry=4,2 -fno-inline-functions-called-once -fmin-function-alignment=8 -fstrict-flex-arrays=3 -fno-strict-overflow -fno-stack-check -fconserve-stack -fno-builtin-wcslen -Wall -Wextra -Wundef -Werror=implicit-function-declaration -Werror=implicit-int -Werror=return-type -Werror=strict-prototypes -Wno-format-security -Wno-trigraphs -Wno-frame-address -Wno-address-of-packed-member -Wmissing-declarations -Wmissing-prototypes -Wframe-larger-than=2048 -Wno-main -Wno-type-limits -Wno-dangling-pointer -Wvla-larger-than=1 -Wno-pointer-sign -Wcast-function-type -Wno-unterminated-string-initialization -Wno-array-bounds -Wno-stringop-overflow -Wno-alloc-size-larger-than -Wimplicit-fallthrough=5 -Werror=date-time -Werror=incompatible-pointer-types -Werror=designated-init -Wenum-conversion -Wunused -Wno-unused-but-set-variable -Wno-unused-const-variable -Wno-packed-not-aligned -Wno-format-overflow -Wno-format-truncation -Wno-stringop-truncation -Wno-override-init -Wno-missing-field-initializers -Wno-shift-negative-value -Wno-maybe-uninitialized -Wno-sign-compare -Wno-unused-parameter -mstack-protector-guard=sysreg -mstack-protector-guard-reg=sp_el0 -mstack-protector-guard-offset=2168  -fsanitize=bounds-strict -fsanitize=shift    -DMODULE  -DKBUILD_BASENAME='".module_common"' -DKBUILD_MODNAME='".module_common.o"' -D__KBUILD_MODNAME=.module_common.o -c -o .module-common.o /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/scripts/module-common.c  

source_.module-common.o := /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/scripts/module-common.c

deps_.module-common.o := \
    $(wildcard include/config/UNWINDER_ORC) \
    $(wildcard include/config/MITIGATION_RETPOLINE) \
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
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/module.h \
    $(wildcard include/config/MODULES) \
    $(wildcard include/config/SYSFS) \
    $(wildcard include/config/MODULES_TREE_LOOKUP) \
    $(wildcard include/config/LIVEPATCH) \
    $(wildcard include/config/STACKTRACE_BUILD_ID) \
    $(wildcard include/config/ARCH_USES_CFI_TRAPS) \
    $(wildcard include/config/MODULE_SIG) \
    $(wildcard include/config/GENERIC_BUG) \
    $(wildcard include/config/KALLSYMS) \
    $(wildcard include/config/SMP) \
    $(wildcard include/config/TRACEPOINTS) \
    $(wildcard include/config/TREE_SRCU) \
    $(wildcard include/config/BPF_EVENTS) \
    $(wildcard include/config/DEBUG_INFO_BTF_MODULES) \
    $(wildcard include/config/JUMP_LABEL) \
    $(wildcard include/config/TRACING) \
    $(wildcard include/config/EVENT_TRACING) \
    $(wildcard include/config/DYNAMIC_FTRACE) \
    $(wildcard include/config/KPROBES) \
    $(wildcard include/config/HAVE_STATIC_CALL_INLINE) \
    $(wildcard include/config/KUNIT) \
    $(wildcard include/config/PRINTK_INDEX) \
    $(wildcard include/config/MODULE_UNLOAD) \
    $(wildcard include/config/CONSTRUCTORS) \
    $(wildcard include/config/FUNCTION_ERROR_INJECTION) \
    $(wildcard include/config/DYNAMIC_DEBUG_CORE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/list.h \
    $(wildcard include/config/LIST_HARDENED) \
    $(wildcard include/config/DEBUG_LIST) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/container_of.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/build_bug.h \
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
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kcsan-checks.h \
    $(wildcard include/config/KCSAN_WEAK_MEMORY) \
    $(wildcard include/config/KCSAN_IGNORE_ATOMICS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/poison.h \
    $(wildcard include/config/ILLEGAL_POINTER_VALUE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/const.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/const.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/const.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/barrier.h \
    $(wildcard include/config/ARM64_PSEUDO_NMI) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/alternative-macros.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/bits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/cpucaps.h \
    $(wildcard include/config/ARM64_EPAN) \
    $(wildcard include/config/ARM64_SVE) \
    $(wildcard include/config/ARM64_SME) \
    $(wildcard include/config/ARM64_CNP) \
    $(wildcard include/config/ARM64_MTE) \
    $(wildcard include/config/ARM64_BTI) \
    $(wildcard include/config/ARM64_TLB_RANGE) \
    $(wildcard include/config/ARM64_POE) \
    $(wildcard include/config/ARM64_GCS) \
    $(wildcard include/config/ARM64_HAFT) \
    $(wildcard include/config/UNMAP_KERNEL_AT_EL0) \
    $(wildcard include/config/ARM64_ERRATUM_843419) \
    $(wildcard include/config/ARM64_ERRATUM_1742098) \
    $(wildcard include/config/ARM64_ERRATUM_2645198) \
    $(wildcard include/config/ARM64_ERRATUM_2658417) \
    $(wildcard include/config/CAVIUM_ERRATUM_23154) \
    $(wildcard include/config/NVIDIA_CARMEL_CNP_ERRATUM) \
    $(wildcard include/config/ARM64_WORKAROUND_REPEAT_TLBI) \
    $(wildcard include/config/ARM64_ERRATUM_3194386) \
    $(wildcard include/config/ARM64_ERRATUM_4193714) \
    $(wildcard include/config/HW_PERF_EVENTS) \
    $(wildcard include/config/ARM64_LSUI) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/cpucap-defs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/insn-def.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/brk-imm.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/stringify.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/barrier.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/stat.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/stat.h \
    $(wildcard include/config/COMPAT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi/asm/stat.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/stat.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/time.h \
    $(wildcard include/config/POSIX_TIMERS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/cache.h \
    $(wildcard include/config/ARCH_HAS_CACHE_LINE_SIZE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/kernel.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/sysinfo.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/cache.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/cache.h \
    $(wildcard include/config/KASAN_HW_TAGS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bitops.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/bits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/overflow.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/limits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/limits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/limits.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/typecheck.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/generic-non-atomic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/bitops.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/builtin-__ffs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/builtin-ffs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/builtin-__fls.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/builtin-fls.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/ffz.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/fls64.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/sched.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/hweight.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/arch_hweight.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/const_hweight.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/atomic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/atomic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/atomic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/cmpxchg.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/lse.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/atomic_ll_sc.h \
    $(wildcard include/config/CC_HAS_K_CONSTRAINT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/export.h \
    $(wildcard include/config/MODVERSIONS) \
    $(wildcard include/config/GENDWARFKSYMS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/linkage.h \
    $(wildcard include/config/ARCH_USE_SYM_ANNOTATIONS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/linkage.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/alternative.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/init.h \
    $(wildcard include/config/MEMORY_HOTPLUG) \
    $(wildcard include/config/HAVE_ARCH_PREL32_RELOCATIONS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/atomic_lse.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/atomic/atomic-arch-fallback.h \
    $(wildcard include/config/GENERIC_ATOMIC64) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/atomic/atomic-long.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/atomic/atomic-instrumented.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/instrumented.h \
    $(wildcard include/config/DEBUG_ATOMIC) \
    $(wildcard include/config/DEBUG_ATOMIC_LARGEST_ALIGN) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bug.h \
    $(wildcard include/config/PRINTK) \
    $(wildcard include/config/BUG_ON_DATA_CORRUPTION) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/bug.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/asm-bug.h \
    $(wildcard include/config/DEBUG_BUGVERBOSE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bug.h \
    $(wildcard include/config/DEBUG_BUGVERBOSE_DETAILED) \
    $(wildcard include/config/BUG) \
    $(wildcard include/config/GENERIC_BUG_RELATIVE_POINTERS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/instrumentation.h \
    $(wildcard include/config/NOINSTR_VALIDATION) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/once_lite.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/panic.h \
    $(wildcard include/config/PANIC_TIMEOUT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/stdarg.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/printk.h \
    $(wildcard include/config/MESSAGE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_DEFAULT) \
    $(wildcard include/config/CONSOLE_LOGLEVEL_QUIET) \
    $(wildcard include/config/EARLY_PRINTK) \
    $(wildcard include/config/DYNAMIC_DEBUG) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kern_levels.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/ratelimit_types.h \
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
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/jump_label.h \
    $(wildcard include/config/HAVE_ARCH_JUMP_LABEL_RELATIVE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/cleanup.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/err.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi/asm/errno.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/errno.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/errno-base.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/args.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/jump_label.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/insn.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kmsan-checks.h \
    $(wildcard include/config/KMSAN) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/instrumented-atomic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/lock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/instrumented-lock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/non-atomic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/non-instrumented-non-atomic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/le.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/bitops/ext2-atomic-setbit.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kasan-enabled.h \
    $(wildcard include/config/ARCH_DEFER_KASAN) \
    $(wildcard include/config/KASAN) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/static_key.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/cputype.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/sysreg.h \
    $(wildcard include/config/BROKEN_GAS_INST) \
    $(wildcard include/config/ARM64_PA_BITS_52) \
    $(wildcard include/config/ARM64_4K_PAGES) \
    $(wildcard include/config/ARM64_16K_PAGES) \
    $(wildcard include/config/ARM64_64K_PAGES) \
    $(wildcard include/config/AMPERE_ERRATUM_AC04_CPU_23) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kasan-tags.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/gpr-num.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/sysreg-defs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bitfield.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/mte-def.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/math64.h \
    $(wildcard include/config/ARCH_SUPPORTS_INT128) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/math.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/div64.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/div64.h \
    $(wildcard include/config/CC_OPTIMIZE_FOR_PERFORMANCE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/math64.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/time64.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/time64.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/time.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/time_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/time32.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/timex.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/timex.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/timex.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/arch_timer.h \
    $(wildcard include/config/ARM_ARCH_TIMER_OOL_WORKAROUND) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/hwcap.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/hwcap.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/cpufeature.h \
    $(wildcard include/config/ARM64_SW_TTBR0_PAN) \
    $(wildcard include/config/ARM64_DEBUG_PRIORITY_MASKING) \
    $(wildcard include/config/ARM64_BTI_KERNEL) \
    $(wildcard include/config/ARM64_PA_BITS) \
    $(wildcard include/config/ARM64_HW_AFDBM) \
    $(wildcard include/config/ARM64_AMU_EXTN) \
    $(wildcard include/config/ARM64_ACTLR_STATE) \
    $(wildcard include/config/ARM64_LPA2) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kernel.h \
    $(wildcard include/config/PREEMPT_VOLUNTARY_BUILD) \
    $(wildcard include/config/PREEMPT_DYNAMIC) \
    $(wildcard include/config/HAVE_PREEMPT_DYNAMIC_CALL) \
    $(wildcard include/config/HAVE_PREEMPT_DYNAMIC_KEY) \
    $(wildcard include/config/PREEMPT_) \
    $(wildcard include/config/DEBUG_ATOMIC_SLEEP) \
    $(wildcard include/config/MMU) \
    $(wildcard include/config/PROVE_LOCKING) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/align.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/align.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/array_size.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kstrtox.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/log2.h \
    $(wildcard include/config/ARCH_HAS_ILOG2_U32) \
    $(wildcard include/config/ARCH_HAS_ILOG2_U64) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/minmax.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sprintf.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/static_call_types.h \
    $(wildcard include/config/HAVE_STATIC_CALL) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/trace_printk.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/instruction_pointer.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/util_macros.h \
    $(wildcard include/config/FOO_SUSPEND) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/wordpart.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/cpumask.h \
    $(wildcard include/config/FORCE_NR_CPUS) \
    $(wildcard include/config/HOTPLUG_CPU) \
    $(wildcard include/config/DEBUG_PER_CPU_MAPS) \
    $(wildcard include/config/CPUMASK_OFFSTACK) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bitmap.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/errno.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/errno.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/find.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/string.h \
    $(wildcard include/config/BINARY_PRINTF) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/string.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/string.h \
    $(wildcard include/config/ARCH_HAS_UACCESS_FLUSHCACHE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/fortify-string.h \
    $(wildcard include/config/CC_HAS_KASAN_MEMINTRINSIC_PREFIX) \
    $(wildcard include/config/GENERIC_ENTRY) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bitmap-str.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/cpumask_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/threads.h \
    $(wildcard include/config/BASE_SMALL) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/gfp_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/numa.h \
    $(wildcard include/config/NUMA_KEEP_MEMINFO) \
    $(wildcard include/config/NUMA) \
    $(wildcard include/config/HAVE_ARCH_NODE_DEV_GROUP) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/nodemask.h \
    $(wildcard include/config/HIGHMEM) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/nodemask_types.h \
    $(wildcard include/config/NODES_SHIFT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/random.h \
    $(wildcard include/config/VMGENID) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/random.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/ioctl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi/asm/ioctl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/ioctl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/ioctl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/irqnr.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/irqnr.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/sparsemem.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/pgtable-prot.h \
    $(wildcard include/config/HAVE_ARCH_USERFAULTFD_WP) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/memory.h \
    $(wildcard include/config/ARM64_VA_BITS) \
    $(wildcard include/config/KASAN_SHADOW_OFFSET) \
    $(wildcard include/config/RANDOMIZE_BASE) \
    $(wildcard include/config/DEBUG_VIRTUAL) \
    $(wildcard include/config/EFI) \
    $(wildcard include/config/ARM_GIC_V3_ITS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sizes.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/page-def.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/page.h \
    $(wildcard include/config/PAGE_SHIFT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/mmdebug.h \
    $(wildcard include/config/DEBUG_VM) \
    $(wildcard include/config/DEBUG_VM_IRQSOFF) \
    $(wildcard include/config/DEBUG_VM_PGFLAGS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/boot.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/sections.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/sections.h \
    $(wildcard include/config/HAVE_FUNCTION_DESCRIPTORS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/memory_model.h \
    $(wildcard include/config/FLATMEM) \
    $(wildcard include/config/SPARSEMEM_VMEMMAP) \
    $(wildcard include/config/SPARSEMEM) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/pfn.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/pgtable-hwdef.h \
    $(wildcard include/config/PGTABLE_LEVELS) \
    $(wildcard include/config/ARM64_CONT_PTE_SHIFT) \
    $(wildcard include/config/ARM64_CONT_PMD_SHIFT) \
    $(wildcard include/config/ARM64_VA_BITS_52) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/pgtable-types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/pgtable-nop4d.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/rsi.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/rsi_cmds.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/arm-smccc.h \
    $(wildcard include/config/HAVE_ARM_SMCCC) \
    $(wildcard include/config/ARM) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/uuid.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/rsi_smc.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/kernel-hwcap.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/percpu.h \
    $(wildcard include/config/RANDOM_KMALLOC_CACHES) \
    $(wildcard include/config/PAGE_SIZE_4KB) \
    $(wildcard include/config/NEED_PER_CPU_PAGE_FIRST_CHUNK) \
    $(wildcard include/config/HAVE_SETUP_PER_CPU_AREA) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/alloc_tag.h \
    $(wildcard include/config/MEM_ALLOC_PROFILING_DEBUG) \
    $(wildcard include/config/MEM_ALLOC_PROFILING) \
    $(wildcard include/config/ARCH_MODULE_NEEDS_WEAK_PER_CPU) \
    $(wildcard include/config/MEM_ALLOC_PROFILING_ENABLED_BY_DEFAULT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/codetag.h \
    $(wildcard include/config/CODE_TAGGING) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/preempt.h \
    $(wildcard include/config/PREEMPT_RT) \
    $(wildcard include/config/PREEMPT_COUNT) \
    $(wildcard include/config/DEBUG_PREEMPT) \
    $(wildcard include/config/TRACE_PREEMPT_TOGGLE) \
    $(wildcard include/config/PREEMPTION) \
    $(wildcard include/config/PREEMPT_NOTIFIERS) \
    $(wildcard include/config/PREEMPT_NONE) \
    $(wildcard include/config/PREEMPT_VOLUNTARY) \
    $(wildcard include/config/PREEMPT) \
    $(wildcard include/config/PREEMPT_LAZY) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/preempt.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/thread_info.h \
    $(wildcard include/config/THREAD_INFO_IN_TASK) \
    $(wildcard include/config/ARCH_HAS_PREEMPT_LAZY) \
    $(wildcard include/config/HAVE_ARCH_WITHIN_STACK_FRAMES) \
    $(wildcard include/config/SH) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/restart_block.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/current.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/thread_info.h \
    $(wildcard include/config/ARM64_MPAM) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/stack_pointer.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/percpu.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/percpu.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/percpu-defs.h \
    $(wildcard include/config/DEBUG_FORCE_WEAK_PER_CPU) \
    $(wildcard include/config/AMD_MEM_ENCRYPT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/smp.h \
    $(wildcard include/config/UP_LATE_INIT) \
    $(wildcard include/config/CSD_LOCK_WAIT_DEBUG) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/smp_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/llist.h \
    $(wildcard include/config/ARCH_HAVE_NMI_SAFE_CMPXCHG) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/smp.h \
    $(wildcard include/config/ARM64_ACPI_PARKING_PROTOCOL) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/irqflags.h \
    $(wildcard include/config/TRACE_IRQFLAGS) \
    $(wildcard include/config/IRQSOFF_TRACER) \
    $(wildcard include/config/PREEMPT_TRACER) \
    $(wildcard include/config/DEBUG_IRQFLAGS) \
    $(wildcard include/config/TRACE_IRQFLAGS_SUPPORT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/irqflags_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/irqflags.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/ptrace.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/ptrace.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/sve_context.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/irqchip/arm-gic-v3-prio.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/stacktrace/frame.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched.h \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING_NATIVE) \
    $(wildcard include/config/SCHED_INFO) \
    $(wildcard include/config/SCHEDSTATS) \
    $(wildcard include/config/SCHED_CORE) \
    $(wildcard include/config/FAIR_GROUP_SCHED) \
    $(wildcard include/config/RT_GROUP_SCHED) \
    $(wildcard include/config/RT_MUTEXES) \
    $(wildcard include/config/UCLAMP_TASK) \
    $(wildcard include/config/UCLAMP_BUCKETS_COUNT) \
    $(wildcard include/config/KMAP_LOCAL) \
    $(wildcard include/config/SCHED_CLASS_EXT) \
    $(wildcard include/config/CGROUP_SCHED) \
    $(wildcard include/config/CFS_BANDWIDTH) \
    $(wildcard include/config/BLK_DEV_IO_TRACE) \
    $(wildcard include/config/PREEMPT_RCU) \
    $(wildcard include/config/TASKS_RCU) \
    $(wildcard include/config/TASKS_TRACE_RCU) \
    $(wildcard include/config/TRIVIAL_PREEMPT_RCU) \
    $(wildcard include/config/MEMCG_V1) \
    $(wildcard include/config/LRU_GEN) \
    $(wildcard include/config/COMPAT_BRK) \
    $(wildcard include/config/CGROUPS) \
    $(wildcard include/config/BLK_CGROUP) \
    $(wildcard include/config/PSI) \
    $(wildcard include/config/PAGE_OWNER) \
    $(wildcard include/config/EVENTFD) \
    $(wildcard include/config/ARCH_HAS_CPU_PASID) \
    $(wildcard include/config/X86_BUS_LOCK_DETECT) \
    $(wildcard include/config/TASK_DELAY_ACCT) \
    $(wildcard include/config/STACKPROTECTOR) \
    $(wildcard include/config/ARCH_HAS_SCALED_CPUTIME) \
    $(wildcard include/config/VIRT_CPU_ACCOUNTING_GEN) \
    $(wildcard include/config/NO_HZ_FULL) \
    $(wildcard include/config/POSIX_CPUTIMERS) \
    $(wildcard include/config/POSIX_CPU_TIMERS_TASK_WORK) \
    $(wildcard include/config/KEYS) \
    $(wildcard include/config/SYSVIPC) \
    $(wildcard include/config/DETECT_HUNG_TASK) \
    $(wildcard include/config/IO_URING) \
    $(wildcard include/config/AUDIT) \
    $(wildcard include/config/AUDITSYSCALL) \
    $(wildcard include/config/DETECT_HUNG_TASK_BLOCKER) \
    $(wildcard include/config/UBSAN) \
    $(wildcard include/config/UBSAN_TRAP) \
    $(wildcard include/config/COMPACTION) \
    $(wildcard include/config/TASK_XACCT) \
    $(wildcard include/config/CPUSETS) \
    $(wildcard include/config/X86_CPU_RESCTRL) \
    $(wildcard include/config/FUTEX) \
    $(wildcard include/config/PERF_EVENTS) \
    $(wildcard include/config/NUMA_BALANCING) \
    $(wildcard include/config/ARCH_HAS_LAZY_MMU_MODE) \
    $(wildcard include/config/FAULT_INJECTION) \
    $(wildcard include/config/LATENCYTOP) \
    $(wildcard include/config/FUNCTION_GRAPH_TRACER) \
    $(wildcard include/config/MEMCG) \
    $(wildcard include/config/UPROBES) \
    $(wildcard include/config/BCACHE) \
    $(wildcard include/config/VMAP_STACK) \
    $(wildcard include/config/SECURITY) \
    $(wildcard include/config/BPF_SYSCALL) \
    $(wildcard include/config/KSTACK_ERASE) \
    $(wildcard include/config/KSTACK_ERASE_METRICS) \
    $(wildcard include/config/X86_MCE) \
    $(wildcard include/config/KRETPROBES) \
    $(wildcard include/config/RETHOOK) \
    $(wildcard include/config/ARCH_HAS_PARANOID_L1D_FLUSH) \
    $(wildcard include/config/RV) \
    $(wildcard include/config/RV_PER_TASK_MONITORS) \
    $(wildcard include/config/USER_EVENTS) \
    $(wildcard include/config/UNWIND_USER) \
    $(wildcard include/config/SCHED_PROXY_EXEC) \
    $(wildcard include/config/SCHED_MM_CID) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/sched.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/processor.h \
    $(wildcard include/config/KUSER_HELPERS) \
    $(wildcard include/config/ARM64_FORCE_52BIT) \
    $(wildcard include/config/HAVE_HW_BREAKPOINT) \
    $(wildcard include/config/ARM64_TAGGED_ADDR_ABI) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/processor.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/vdso/processor.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/hw_breakpoint.h \
    $(wildcard include/config/CPU_PM) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/virt.h \
    $(wildcard include/config/KVM) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/kasan.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/mte-kasan.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/pointer_auth.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/prctl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/spectre.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/fpsimd.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/sigcontext.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/pid_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sem_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/shm.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/page.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/personality.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/personality.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/getorder.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/shmparam.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/shmparam.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kmsan_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/mutex_types.h \
    $(wildcard include/config/MUTEX_SPIN_ON_OWNER) \
    $(wildcard include/config/DEBUG_MUTEXES) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/osq_lock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/spinlock_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rwlock_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/plist_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/hrtimer_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/timerqueue_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rbtree_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/timer_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/seccomp_types.h \
    $(wildcard include/config/SECCOMP) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/refcount_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/resource.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/resource.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi/asm/resource.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/resource.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/resource.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/latencytop.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/prio.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/signal_types.h \
    $(wildcard include/config/OLD_SIGACTION) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/signal.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/signal.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/signal.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/signal.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/signal.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/signal-defs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/uapi/asm/siginfo.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/siginfo.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/spinlock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bottom_half.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/lockdep.h \
    $(wildcard include/config/DEBUG_LOCKING_API_SELFTESTS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/mmiowb.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/mmiowb.h \
    $(wildcard include/config/MMIOWB) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/spinlock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/qspinlock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/qspinlock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/qrwlock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/qrwlock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rwlock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/spinlock_api_smp.h \
    $(wildcard include/config/INLINE_SPIN_LOCK) \
    $(wildcard include/config/INLINE_SPIN_LOCK_BH) \
    $(wildcard include/config/INLINE_SPIN_LOCK_IRQ) \
    $(wildcard include/config/INLINE_SPIN_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_SPIN_TRYLOCK) \
    $(wildcard include/config/INLINE_SPIN_TRYLOCK_BH) \
    $(wildcard include/config/UNINLINE_SPIN_UNLOCK) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_BH) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_SPIN_UNLOCK_IRQRESTORE) \
    $(wildcard include/config/GENERIC_LOCKBREAK) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rwlock_api_smp.h \
    $(wildcard include/config/INLINE_READ_LOCK) \
    $(wildcard include/config/INLINE_WRITE_LOCK) \
    $(wildcard include/config/INLINE_READ_LOCK_BH) \
    $(wildcard include/config/INLINE_WRITE_LOCK_BH) \
    $(wildcard include/config/INLINE_READ_LOCK_IRQ) \
    $(wildcard include/config/INLINE_WRITE_LOCK_IRQ) \
    $(wildcard include/config/INLINE_READ_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_WRITE_LOCK_IRQSAVE) \
    $(wildcard include/config/INLINE_READ_TRYLOCK) \
    $(wildcard include/config/INLINE_WRITE_TRYLOCK) \
    $(wildcard include/config/INLINE_READ_UNLOCK) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK) \
    $(wildcard include/config/INLINE_READ_UNLOCK_BH) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_BH) \
    $(wildcard include/config/INLINE_READ_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_IRQ) \
    $(wildcard include/config/INLINE_READ_UNLOCK_IRQRESTORE) \
    $(wildcard include/config/INLINE_WRITE_UNLOCK_IRQRESTORE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/syscall_user_dispatch_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/mm_types_task.h \
    $(wildcard include/config/ARCH_WANT_BATCHED_UNMAP_TLB_FLUSH) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/tlbbatch.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/netdevice_xmit.h \
    $(wildcard include/config/NET_ACT_MIRRED) \
    $(wildcard include/config/NET_EGRESS) \
    $(wildcard include/config/NF_DUP_NETDEV) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/task_io_accounting.h \
    $(wildcard include/config/TASK_IO_ACCOUNTING) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/posix-timers_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rseq_types.h \
    $(wildcard include/config/RSEQ) \
    $(wildcard include/config/RSEQ_SLICE_EXTENSION) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/irq_work_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/workqueue_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/seqlock_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kcsan.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rv.h \
    $(wildcard include/config/RV_LTL_MONITOR) \
    $(wildcard include/config/RV_HA_MONITOR) \
    $(wildcard include/config/RV_REACTORS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/uidgid_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/tracepoint-defs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/unwind_deferred_types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/kmap_size.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/kmap_size.h \
    $(wildcard include/config/DEBUG_KMAP_LOCAL) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/generated/rq-offsets.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/ext.h \
    $(wildcard include/config/EXT_GROUP_SCHED) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/clocksource/arm_arch_timer.h \
    $(wildcard include/config/ARM_ARCH_TIMER) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/timecounter.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/timex.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/time32.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/time.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/compat.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/compat.h \
    $(wildcard include/config/COMPAT_FOR_U64_ALIGNMENT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/task_stack.h \
    $(wildcard include/config/STACK_GROWSUP) \
    $(wildcard include/config/DEBUG_STACK_USAGE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/magic.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/refcount.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kasan.h \
    $(wildcard include/config/KASAN_STACK) \
    $(wildcard include/config/KASAN_VMALLOC) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/stat.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/uidgid.h \
    $(wildcard include/config/MULTIUSER) \
    $(wildcard include/config/USER_NS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/highuid.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/buildid.h \
    $(wildcard include/config/VMCORE_INFO) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kmod.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/umh.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/gfp.h \
    $(wildcard include/config/ZONE_DMA) \
    $(wildcard include/config/ZONE_DMA32) \
    $(wildcard include/config/ZONE_DEVICE) \
    $(wildcard include/config/CONTIG_ALLOC) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/mmzone.h \
    $(wildcard include/config/ARCH_FORCE_MAX_ORDER) \
    $(wildcard include/config/PAGE_BLOCK_MAX_ORDER) \
    $(wildcard include/config/HAVE_GIGANTIC_FOLIOS) \
    $(wildcard include/config/HUGETLB_PAGE) \
    $(wildcard include/config/HUGETLB_PAGE_OPTIMIZE_VMEMMAP) \
    $(wildcard include/config/CMA) \
    $(wildcard include/config/MEMORY_ISOLATION) \
    $(wildcard include/config/ZSMALLOC) \
    $(wildcard include/config/UNACCEPTED_MEMORY) \
    $(wildcard include/config/IOMMU_SUPPORT) \
    $(wildcard include/config/SWAP) \
    $(wildcard include/config/TRANSPARENT_HUGEPAGE) \
    $(wildcard include/config/LRU_GEN_STATS) \
    $(wildcard include/config/LRU_GEN_WALKS_MMU) \
    $(wildcard include/config/MEMORY_FAILURE) \
    $(wildcard include/config/PAGE_EXTENSION) \
    $(wildcard include/config/DEFERRED_STRUCT_PAGE_INIT) \
    $(wildcard include/config/HAVE_MEMORYLESS_NODES) \
    $(wildcard include/config/SPARSEMEM_EXTREME) \
    $(wildcard include/config/SPARSEMEM_VMEMMAP_PREINIT) \
    $(wildcard include/config/HAVE_ARCH_PFN_VALID) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/list_nulls.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/wait.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/seqlock.h \
    $(wildcard include/config/CC_IS_GCC) \
    $(wildcard include/config/GCC_VERSION) \
    $(wildcard include/config/UBSAN_ALIGNMENT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/mutex.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/debug_locks.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/pageblock-flags.h \
    $(wildcard include/config/HUGETLB_PAGE_SIZE_VARIABLE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/page-flags-layout.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/generated/bounds.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/mm_types.h \
    $(wildcard include/config/HAVE_ALIGNED_STRUCT_PAGE) \
    $(wildcard include/config/SLAB_OBJ_EXT) \
    $(wildcard include/config/HUGETLB_PMD_PAGE_TABLE_SHARING) \
    $(wildcard include/config/SLAB_FREELIST_HARDENED) \
    $(wildcard include/config/USERFAULTFD) \
    $(wildcard include/config/ANON_VMA_NAME) \
    $(wildcard include/config/PER_VMA_LOCK) \
    $(wildcard include/config/HAVE_ARCH_COMPAT_MMAP_BASES) \
    $(wildcard include/config/MEMBARRIER) \
    $(wildcard include/config/FUTEX_PRIVATE_HASH) \
    $(wildcard include/config/ARCH_HAS_ELF_CORE_EFLAGS) \
    $(wildcard include/config/AIO) \
    $(wildcard include/config/MMU_NOTIFIER) \
    $(wildcard include/config/SPLIT_PMD_PTLOCKS) \
    $(wildcard include/config/IOMMU_MM_DATA) \
    $(wildcard include/config/KSM) \
    $(wildcard include/config/MM_ID) \
    $(wildcard include/config/CORE_DUMP_DEFAULT_ELF_HEADERS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/auxvec.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/auxvec.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/auxvec.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kref.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rbtree.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rcupdate.h \
    $(wildcard include/config/TINY_RCU) \
    $(wildcard include/config/RCU_STRICT_GRACE_PERIOD) \
    $(wildcard include/config/RCU_LAZY) \
    $(wildcard include/config/RCU_STALL_COMMON) \
    $(wildcard include/config/VIRT_XFER_TO_GUEST_WORK) \
    $(wildcard include/config/RCU_NOCB_CPU) \
    $(wildcard include/config/TASKS_RCU_GENERIC) \
    $(wildcard include/config/TASKS_RUDE_RCU) \
    $(wildcard include/config/TREE_RCU) \
    $(wildcard include/config/DEBUG_OBJECTS_RCU_HEAD) \
    $(wildcard include/config/PROVE_RCU) \
    $(wildcard include/config/ARCH_WEAK_RELEASE_ACQUIRE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/context_tracking_irq.h \
    $(wildcard include/config/CONTEXT_TRACKING_IDLE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rcutree.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/maple_tree.h \
    $(wildcard include/config/MAPLE_RCU_DISABLED) \
    $(wildcard include/config/DEBUG_MAPLE_TREE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rwsem.h \
    $(wildcard include/config/RWSEM_SPIN_ON_OWNER) \
    $(wildcard include/config/DEBUG_RWSEMS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/completion.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/swait.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/uprobes.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/timer.h \
    $(wildcard include/config/DEBUG_OBJECTS_TIMERS) \
    $(wildcard include/config/NO_HZ_COMMON) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/ktime.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/jiffies.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/jiffies.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/generated/timeconst.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/vdso/ktime.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/timekeeping.h \
    $(wildcard include/config/POSIX_AUX_CLOCKS) \
    $(wildcard include/config/GENERIC_CMOS_UPDATE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/clocksource_ids.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/debugobjects.h \
    $(wildcard include/config/DEBUG_OBJECTS) \
    $(wildcard include/config/DEBUG_OBJECTS_FREE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/uprobes.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/debug-monitors.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/esr.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/probes.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/workqueue.h \
    $(wildcard include/config/DEBUG_OBJECTS_WORK) \
    $(wildcard include/config/FREEZER) \
    $(wildcard include/config/WQ_WATCHDOG) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/percpu_counter.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/mmu.h \
    $(wildcard include/config/ARM64_E0PD) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/page-flags.h \
    $(wildcard include/config/PAGE_IDLE_FLAG) \
    $(wildcard include/config/ARCH_USES_PG_ARCH_2) \
    $(wildcard include/config/ARCH_USES_PG_ARCH_3) \
    $(wildcard include/config/MIGRATION) \
    $(wildcard include/config/DEBUG_KMAP_LOCAL_FORCE_MAP) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/local_lock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/local_lock_internal.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/zswap.h \
    $(wildcard include/config/ZSWAP) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/memory_hotplug.h \
    $(wildcard include/config/ARCH_HAS_ADD_PAGES) \
    $(wildcard include/config/MEMORY_HOTREMOVE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/notifier.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/srcu.h \
    $(wildcard include/config/TINY_SRCU) \
    $(wildcard include/config/NEED_SRCU_NMI_SAFE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rcu_segcblist.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/srcutree.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rcu_node_tree.h \
    $(wildcard include/config/RCU_FANOUT) \
    $(wildcard include/config/RCU_FANOUT_LEAF) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/mmzone.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/mmzone.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/topology.h \
    $(wildcard include/config/USE_PERCPU_NUMA_NODE_ID) \
    $(wildcard include/config/SCHED_SMT) \
    $(wildcard include/config/GENERIC_ARCH_TOPOLOGY) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/arch_topology.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/topology.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/numa.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/numa.h \
    $(wildcard include/config/NUMA_EMU) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/topology.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sysctl.h \
    $(wildcard include/config/SYSCTL) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/sysctl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/elf.h \
    $(wildcard include/config/ARCH_HAVE_EXTRA_ELF_NOTES) \
    $(wildcard include/config/ARCH_USE_GNU_PROPERTY) \
    $(wildcard include/config/ARCH_HAVE_ELF_PROT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/elf.h \
    $(wildcard include/config/COMPAT_VDSO) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/generated/asm/user.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/user.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/elf.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/elf-em.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/fs.h \
    $(wildcard include/config/FANOTIFY_ACCESS_PERMISSIONS) \
    $(wildcard include/config/READ_ONLY_THP_FOR_FS) \
    $(wildcard include/config/FS_POSIX_ACL) \
    $(wildcard include/config/CGROUP_WRITEBACK) \
    $(wildcard include/config/IMA) \
    $(wildcard include/config/FILE_LOCKING) \
    $(wildcard include/config/FSNOTIFY) \
    $(wildcard include/config/EPOLL) \
    $(wildcard include/config/FS_DAX) \
    $(wildcard include/config/BLOCK) \
    $(wildcard include/config/UNICODE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/fs/super.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/fs/super_types.h \
    $(wildcard include/config/QUOTA) \
    $(wildcard include/config/FS_ENCRYPTION) \
    $(wildcard include/config/FS_VERITY) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/fs_dirent.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/errseq.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/list_lru.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/shrinker.h \
    $(wildcard include/config/SHRINKER_DEBUG) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/xarray.h \
    $(wildcard include/config/XARRAY_MULTI) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/mm.h \
    $(wildcard include/config/MMU_LAZY_TLB_REFCOUNT) \
    $(wildcard include/config/ARCH_HAS_MEMBARRIER_CALLBACKS) \
    $(wildcard include/config/ARCH_HAS_SYNC_CORE_BEFORE_USERMODE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sync_core.h \
    $(wildcard include/config/ARCH_HAS_PREPARE_SYNC_CORE_CMD) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/coredump.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/list_bl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/bit_spinlock.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/percpu-rwsem.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rcuwait.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/signal.h \
    $(wildcard include/config/SCHED_AUTOGROUP) \
    $(wildcard include/config/BSD_PROCESS_ACCT) \
    $(wildcard include/config/TASKSTATS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rculist.h \
    $(wildcard include/config/PROVE_RCU_LIST) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/signal.h \
    $(wildcard include/config/DYNAMIC_SIGFRAME) \
    $(wildcard include/config/PROC_FS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/jobctl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/task.h \
    $(wildcard include/config/HAVE_EXIT_THREAD) \
    $(wildcard include/config/ARCH_WANTS_DYNAMIC_TASK_STRUCT) \
    $(wildcard include/config/HAVE_ARCH_THREAD_STRUCT_WHITELIST) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/uaccess.h \
    $(wildcard include/config/ARCH_HAS_SUBPAGE_FAULTS) \
    $(wildcard include/config/HARDENED_USERCOPY) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/fault-inject-usercopy.h \
    $(wildcard include/config/FAULT_INJECTION_USERCOPY) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/nospec.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/ucopysize.h \
    $(wildcard include/config/HARDENED_USERCOPY_DEFAULT_ON) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/uaccess.h \
    $(wildcard include/config/CC_HAS_ASM_GOTO_OUTPUT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/kernel-pgtable.h \
    $(wildcard include/config/RELOCATABLE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/asm-extable.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/mte.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/extable.h \
    $(wildcard include/config/BPF_JIT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/access_ok.h \
    $(wildcard include/config/ALTERNATE_USER_ADDRESS_SPACE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/cred.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/capability.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/capability.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/key.h \
    $(wildcard include/config/KEY_NOTIFICATIONS) \
    $(wildcard include/config/NET) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/assoc_array.h \
    $(wildcard include/config/ASSOCIATIVE_ARRAY) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/user.h \
    $(wildcard include/config/VFIO_PCI_ZDEV_KVM) \
    $(wildcard include/config/IOMMUFD) \
    $(wildcard include/config/WATCH_QUEUE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/ratelimit.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/pid.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rhashtable-types.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/posix-timers.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/alarmtimer.h \
    $(wildcard include/config/RTC_CLASS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/hrtimer.h \
    $(wildcard include/config/HIGH_RES_TIMERS) \
    $(wildcard include/config/TIME_LOW_RES) \
    $(wildcard include/config/TIMERFD) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/hrtimer_defs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/timerqueue.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/hrtimer_rearm.h \
    $(wildcard include/config/HRTIMER_REARM_DEFERRED) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rcuref.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rcu_sync.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/quota.h \
    $(wildcard include/config/QUOTA_NETLINK_INTERFACE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/dqblk_xfs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/dqblk_v1.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/dqblk_v2.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/dqblk_qtree.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/projid.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/quota.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/unicode.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/dcache.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rculist_bl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/lockref.h \
    $(wildcard include/config/ARCH_USE_CMPXCHG_LOCKREF) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/stringhash.h \
    $(wildcard include/config/DCACHE_WORD_ACCESS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/hash.h \
    $(wildcard include/config/HAVE_ARCH_HASH) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/vfsdebug.h \
    $(wildcard include/config/DEBUG_VFS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/wait_bit.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kdev_t.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/kdev_t.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/path.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/radix-tree.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/semaphore.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/fcntl.h \
    $(wildcard include/config/ARCH_32BIT_OFF_T) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/fcntl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/uapi/asm/fcntl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/asm-generic/fcntl.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/openat2.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/migrate_mode.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/delayed_call.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/ioprio.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sched/rt.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/iocontext.h \
    $(wildcard include/config/BLK_ICQ) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/ioprio.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/mount.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/mnt_idmapping.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/slab.h \
    $(wildcard include/config/FAILSLAB) \
    $(wildcard include/config/KFENCE) \
    $(wildcard include/config/SLUB_TINY) \
    $(wildcard include/config/SLUB_DEBUG) \
    $(wildcard include/config/SLAB_BUCKETS) \
    $(wildcard include/config/KVFREE_RCU_BATCHED) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/percpu-refcount.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rw_hint.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/file_ref.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/uapi/linux/fs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kobject.h \
    $(wildcard include/config/UEVENT_HELPER) \
    $(wildcard include/config/DEBUG_KOBJECT_RELEASE) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/sysfs.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kernfs.h \
    $(wildcard include/config/KERNFS) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/idr.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/kobject_ns.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/moduleparam.h \
    $(wildcard include/config/ALPHA) \
    $(wildcard include/config/PPC64) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/rbtree_latch.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/error-injection.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/error-injection.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/module.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/asm-generic/module.h \
    $(wildcard include/config/HAVE_MOD_ARCH_SPECIFIC) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/build-salt.h \
    $(wildcard include/config/BUILD_SALT) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/elfnote.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/elfnote-lto.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/linux/vermagic.h \
    $(wildcard include/config/PREEMPT_BUILD) \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/include/generated/utsrelease.h \
  /mnt/hordefast/kernel-build/linux-7.1.13-usb4gpu/arch/arm64/include/asm/vermagic.h \

.module-common.o: $(deps_.module-common.o)

$(deps_.module-common.o):

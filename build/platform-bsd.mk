include $(SRC_PATH)build/arch.mk
SHAREDLIBSUFFIX = so
SHAREDLIBSUFFIXFULLVER=$(SHAREDLIBSUFFIX).$(FULL_VERSION)
SHAREDLIBSUFFIXMAJORVER=$(SHAREDLIBSUFFIX).$(SHAREDLIB_MAJORVERSION)
SHLDFLAGS = -Wl,-Bsymbolic -Wl,-soname,$(LIBPREFIX)$(PROJECT_NAME).$(SHAREDLIBSUFFIXMAJORVER)
CFLAGS += -fPIC
ifeq ($(USE_STACK_PROTECTOR), Yes)
CFLAGS += -fstack-protector-all
endif
LDFLAGS += -lpthread
BSD_CXX_RUNTIME ?= $(if $(findstring -lc++,$(shell $(CXX) -### -x c++ /dev/null 2>&1)),-lc++,-lstdc++)
STATIC_LDFLAGS = $(BSD_CXX_RUNTIME) -lpthread -lm
ifeq ($(ASM_ARCH), x86)
ifeq ($(ARCH), x86_64)
ASMFLAGS += -f elf64
else
ASMFLAGS += -f elf
endif
endif


#
# Copyright 2021 Kontain Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

TOP := $(shell git rev-parse --show-toplevel)

LIB := runtime

MALLOC_DIR = mallocng

# following it to compute SOURCES, picking up all .c files from relevant dirs
# (i.e. skipping SKIP_SRC_DIR). Then drop the base implementations for the sake of
# arch specific ones based on ARCH.
SRCDIR := musl
ARCH := x86_64
SKIP_SRC_DIR := $(addprefix $(SRCDIR)/src/, ipc mq)
SRC_DIRS := $(filter-out $(addsuffix /,${SKIP_SRC_DIR}), $(wildcard $(addprefix $(SRCDIR)/,src/*/ crt/ ldso/ src/malloc/$(MALLOC_DIR)/)))

# these musl files will be dropped from all libs (static and dynamic)
KM_REPLACED_SRCS := __set_thread_area.s __unmapself.s syscall.s syscall_cp.s getenv.c preadv.c pwritev.c \
						fcntl.c clone.s getpagesize.c fcntl/open.c string/strdup.c string/strndup.c select/poll.c \
						clock_gettime.c
# These KM files will be added to all libs (static and dynamic)
KM_EXTRA_SRCS := $(wildcard *_km.c) $(wildcard *.s)
# These KM files are not applicable to dynamic (DL or SO) libs, and will be in static only
KM_STATIC_ONLY_SRCS = dlstatic_km.c

PTHREAD_SRCS := pthread_cond_broadcast.c pthread_cond_destroy.c pthread_cond_signal.c pthread_cond_wait.c \
					 pthread_create.c pthread_detach.c pthread_getspecific.c pthread_join.c pthread_key_create.c \
					 pthread_key_delete.c pthread_mutex_lock.c pthread_mutex_unlock.c pthread_once.c pthread_yield_km.c \
					 pthread_rwlock_rdlock.c pthread_rwlock_unlock.c pthread_rwlock_wrlock.c pthread_setspecific.c

# TODO: we need 3 group of sources: GENERIC STATIC_ONLY DYNAMIC_ONLY
# generic
BASE_SRCS := $(wildcard $(addsuffix *.c,$(SRC_DIRS)))
ARCH_SRCS := $(wildcard $(addsuffix $(ARCH)/*.[csS],$(SRC_DIRS)))
# asm/arch specific implementations replacing base ones
REPLACED_SRCS := $(addsuffix .%, $(subst /$(ARCH)/,/,$(basename $(ARCH_SRCS))))
ALL_SOURCES := $(filter-out ${REPLACED_SRCS}, ${BASE_SRCS} ${ARCH_SRCS})
ALL_SOURCES := $(filter-out $(addprefix %, ${KM_REPLACED_SRCS}), ${ALL_SOURCES}) ${KM_EXTRA_SRCS}

# sources for objects used librutime.a - i.e. used in all version (shared and static)
SOURCES := $(filter-out $(addprefix %, ${PTHREAD_SRCS}), $(filter musl/src/%,$(ALL_SOURCES))) ${KM_EXTRA_SRCS}

# pthead-specific sources for a separate lib
PTHREAD_SRCS := $(filter $(addprefix %, ${PTHREAD_SRCS}), $(filter musl/src/%,$(ALL_SOURCES)))

DL_SOURCES := $(filter musl/ldso/%,${ALL_SOURCES})
CRT_SOURCES := $(filter musl/crt/%,$(ALL_SOURCES))

H_DIRS := arch/$(ARCH) arch/generic src/include src/internal src/errno include
# has to be recursive assignment as BLDDIR is defined in an include below
INCLUDES = . ${TOP}/include $(addprefix ${SRCDIR}/, ${H_DIRS}) ${BLDDIR}/${SRCDIR}/include

CWERRS := -Werror=implicit-function-declaration -Werror=implicit-int -Werror=pointer-sign -Werror=pointer-arith
CWARNS := -Wno-parentheses -Wno-uninitialized -Wno-missing-braces -Wno-unused-value -Wno-unused-but-set-variable \
		  -Wno-unknown-pragmas -Wno-pointer-to-int-cast

# -ffunction-sections -fdata-sections work together with linker script, see km.ld
# Other -f... options are to reduce generated code size
COPTS := -Os -ggdb3 -D_XOPEN_SOURCE=700 -pipe -nostdinc \
	-fomit-frame-pointer \
	-ffunction-sections -fdata-sections -fexcess-precision=standard -frounding-math \
	-fno-tree-loop-distribute-patterns -fno-stack-protector \
	${CWERRS} ${CWARNS} -ffile-prefix-map=${TOP}/= -fdebug-prefix-map=${TOP}/=

include ${TOP}/make/actions.mk

# all objects needed in libc.so
SO_OBJS = $(addprefix ${BLDDIR}/, $(addsuffix .lo, \
		$(basename $(filter-out ${KM_STATIC_ONLY_SRCS},${SOURCES}) ${PTHREAD_SRCS})))
# dynlink obj files
DL_OBJS = $(addprefix ${BLDDIR}/, $(addsuffix .lo, $(basename ${DL_SOURCES})))
# c run time init/start and  finish/end obj files
CRT_OBJS = $(addprefix ${BLDDIR}/, $(addsuffix .o, $(basename ${CRT_SOURCES})))
PTHREAD_OBJS = $(addprefix ${BLDDIR}/, $(addsuffix .o, $(basename ${PTHREAD_SRCS})))

$(CRT_OBJS): CFLAGS += -DCRT
$(SO_OBJS) $(DL_OBJS): CFLAGS += -fPIC
${BLDDIR}/musl/crt/Scrt1.o ${BLDDIR}/musl/crt/rcrt1.o: CFLAGS += -fPIC

all: ${BLDDIR}/.finalize

# Force actual build via dependencies.
# After the build, set neccessary symlinks and install to KM_OPT_RT
# symlnks:
# libc.a -> libruntime.a           # needed for '-lc'; we may want to rename going forward
# libc.musl-x86_64.so.1 -> libc.so # needed for Alpine libstdc++
# ld-linux-x86-64.so.2 -> libc.so  # needed for all non-Alpine (eg Fedora) .so
.PHONY: ${BLDDIR}/.finalize
${BLDDIR}/.finalize: $(addprefix ${BLDDIR}/,libm.a libpthread.a libpthread.so libc.so) ${CRT_OBJS} ${KM_OPT_RT}
	@#
	@# set symlinks in ../build
	@#
	@cd ${BLDDIR} ; if [ ! -L libc.a ] ; then \
		echo symlink libc.a ; ln -s ./libruntime.a libc.a ; fi
	@cd ${BLDDIR} ; if [ ! -L libc.musl-x86_64.so.1 ] ; then \
		echo symlink lib.musl.so; ln -s ./libc.so libc.musl-x86_64.so.1 ; fi
	@cd ${BLDDIR} ; if [ ! -L ld-linux-x86-64.so.2 ] ; then \
		echo symlink lib.musl.so; ln -s ./libc.so ld-linux-x86-64.so.2 ; fi
	@#
	@# mkdir and fill /opt/kontain/runtime
	@#
	@rt=${KM_OPT_RT}; if [ ! -w $$rt ] ; then \
		echo -e "${RED}${KM_OPT_RT} is not writeable - run 'make -C tests buildenv-local-fedora'$(NOCOLOR)" ; \
		false; \
	fi
	@echo Copy libs to ${KM_OPT_RT}...; cp --no-dereference --preserve=all --update ${BLDDIR}/lib{*.a,*.so*} ${BLDDIR}/ld-linux* ${KM_OPT_RT}
	@touch $@

$(addprefix ${BLDDIR}/, libm.a):
	@echo "Making " $@; rm -f $@; ${AR} crs $@

$(addprefix ${BLDDIR}/, libpthread.a): ${PTHREAD_OBJS}
	@echo "Making " $@; rm -f $@; ld -r ${PTHREAD_OBJS} -o $*.o; ${AR} crs $@ -o $*.o

# Empty pthread sharedlib just to make 'gcc -shared -pthread' happy
# Note: options here derived from libc.so build
$(addprefix ${BLDDIR}/, libpthread.so): km_pthread_dummy.c
	$(CC) $(CFLAGS) $(LDFLAGS_ALL) -nostdlib -shared -Wl,--gc-sections -Wl,--exclude-libs=ALL -o $@ $^

# Libc.so target. Depends on libgcc.a, which should be of the correct (for other libs) version.
# LIBGCC is something like "/opt/kontain/alpine-lib/usr/lib/gcc/x86_64-alpine-linux-musl/8.3.0/libgcc.a"
# File with path to libgcc.a is generated during the buildenv container creation.
# TODO: maybe just slap a symlink current ->8.3.0 instead of this .txt file ?
LIBGCC = ${KM_OPT_ALPINELIB}$(shell cat ${KM_OPT}/alpine-lib/gcc-libs-path.txt)/libgcc.a
$(addprefix ${BLDDIR}/, libc.so): ${SO_OBJS} ${DL_OBJS}
	@if [ ! -f ${LIBGCC} ] ; then echo -e  "${RED}${LIBGCC} is not found. Check installation${NOCOLOR}"; false; fi
	$(CC) $(CFLAGS) $(LDFLAGS_ALL) -nostdlib -shared -Wl,-e,_dlstart -Wl,--gc-sections -Wl,--exclude-libs=ALL -Wl,--dynamic-list=./musl/dynamic.list -o $@ ${SO_OBJS} ${DL_OBJS} ${LIBGCC}

${DEPS}: | $(addprefix ${BLDDIR}/${SRCDIR}/include/, bits/alltypes.h bits/syscall.h version.h)

EXTRA_OBJDIRS = $(sort $(dir ${DL_OBJS})) $(sort $(dir ${CRT_OBJS}))
${SO_OBJS} ${DL_OBJS}: | ${OBJDIRS}	${EXTRA_OBJDIRS} # order only prerequisite - just make sure it exists

${EXTRA_OBJDIRS}:
	mkdir -p $@

${BLDDIR}/%.lo: %.c
	$(CC) -c ${CFLAGS} $(abspath $<) -o $@

${BLDDIR}/%.lo: %.s
	$(CC) -c ${CFLAGS} $(abspath $<) -o $@

${BLDDIR}/%.lo: %.S
	$(CC) -c ${CFLAGS} $(abspath $<) -o $@

${BLDDIR}/${SRCDIR}/include/bits/alltypes.h: $(SRCDIR)/arch/$(ARCH)/bits/alltypes.h.in $(SRCDIR)/include/alltypes.h.in
	mkdir -p $(dir $@)
	sed -f $(SRCDIR)/tools/mkalltypes.sed $(SRCDIR)/arch/$(ARCH)/bits/alltypes.h.in $(SRCDIR)/include/alltypes.h.in > $@

${BLDDIR}/${SRCDIR}/include/bits/syscall.h: $(SRCDIR)/arch/$(ARCH)/bits/syscall.h.in
	mkdir -p $(dir $@)
	cp $< $@
	sed -n -e s/__NR_/SYS_/p < $< >> $@

${BLDDIR}/${SRCDIR}/include/version.h: $(wildcard $(SRCDIR)/VERSION $(SRCDIR)/.git)
	mkdir -p $(dir $@)
	printf '#define VERSION "%s"\n' "$$(cd $(SRCDIR); sh tools/version.sh)" > $@

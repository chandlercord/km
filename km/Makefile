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

EXEC_SELINUX_CONTEXT := "system_u:object_r:bin_t:s0"
SOURCES := load_elf.c km_cpu_init.c km_main.c km_vcpu_run.c km_hcalls.c km_mem.c km_mmap.c \
		km_gdb_stub.c gdb_kvm_x86_64.c km_signal.c km_init_guest.c km_intr.c km_coredump.c \
		km_filesys.c km_hc_name.c km_trace.c km_musl_related.c km_decode.c km_proc.c \
		km_guest_asmcode.s km_snapshot.c km_exec.c km_fork.c km_management.c \
		km_kkm.c km_vmdriver.c km_exec_fd_save_recover.c
VERSION_SRC := km_main.c # it has branch/version info, so rebuild it if git info changes
INCLUDES := ${TOP}/include
COVERAGE := yes
EXEC := km
ifneq (${VALGRIND},)
LOCAL_COPTS := -Werror -D_GNU_SOURCE -DKM_GPA_AT_16T=1
LOCAL_LDOPTS := -Wl,--script=km_guest_ldcmd -pie
else
LOCAL_COPTS := -Werror -D_GNU_SOURCE
LOCAL_LDOPTS := -Wl,--script=km_guest_ldcmd -static-pie
endif

include ${TOP}/make/actions.mk

${BLDDIR}/km_guest_asmcode.o: CFLAGS += -fpic
CFLAGS += -fpic


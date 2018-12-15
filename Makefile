#
# Basic build for kontain machine (km) and related tests/examples.
# Subject to change - code is currently PoC
#
# dependencies:
#  elfutils-libelf-devel - for gelf.h and related libs
#
# ====
#  Copyright © 2018 Kontain Inc. All rights reserved.
#
#  Kontain Inc CONFIDENTIAL
#
#   This file includes unpublished proprietary source code of Kontain Inc. The
#   copyright notice above does not evidence any actual or intended publication of
#   such source code. Disclosure of this source code or any related proprietary
#   information is strictly prohibited without the express written permission of
#   Kontain Inc.

CFLAGS := -Wall -ggdb -O2

HDR := km.h km_hcalls.h x86_cpu.h
KM_SRC := load_elf.c km_cpu_init.c km_main.c km_vcpu_run.c km_hcalls.c
SRC := $(KM_SRC) runtime.c hello.c hello_html.c

KM_OBJ := $(KM_SRC:.c=.o)
OBJ := $(SRC:.c=.o) 

# colors for nice color in output
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
CYAN := \033[36m
NOCOLOR := \033[0m

#
# Default target (should be first, to support simply 'make' invoking 'make help')
#
# This target ("help") scans Makefile for '##' in targets and prints a summary
# Note - used awk to print (instead of echo) so escaping/coloring is platform independed
.PHONY: help
help:  ## Prints help on 'make' targets
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make $(CYAN)<target>$(NOCOLOR)\n" } \
    /^[.a-zA-Z0-9_-]+:.*?##/ { printf "  $(CYAN)%-15s$(NOCOLOR) %s\n", $$1, $$2 } \
	/^##@/ { printf "\n\033[1m%s$(NOCOLOR)\n", substr($$0, 5) } ' \
	$(MAKEFILE_LIST)
	@echo ""

.PHONY: all
all: km hello.km hello_html.km hello hello_html ## build everything

km: $(KM_OBJ)  ## build the 'km' VMM
	$(CC) $(KM_OBJ) -lelf -o km

$(KM_OBJ) $(OBJ): $(HDR)

.PHONY: clean
clean:  ## removes .o and other artifacts
	rm -f *.o km hello hello_html hello.km hello_html.km

runtime.o: runtime.c

hello.km:	hello.o runtime.o ## builds 'hello world' example (load a unikernel and print hello via hypercall)
	ld -T km.ld hello.o runtime.o -o hello.km

hello_html.km: hello_html.o runtime.o  ## builds 'hello world" HTML example - run with ``km hello_world'' in one window, then from browser go to http://127.0.0.1:8002.
	ld -T km.ld hello_html.o runtime.o -o hello_html.km

.PHONY: test
test: all ## Builds all and runs a simple test
	# TBD - make return from km compliant with "success" :-)
	-./km hello.km
	./km hello_html.km &
	curl -s http://127.0.0.1:8002 | grep -q "I'm here"
	@echo "Success"

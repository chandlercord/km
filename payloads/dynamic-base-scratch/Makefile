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

COMPONENT := dynamic-base-scratch

# name to be added to label(s)
PAYLOAD_NAME := Dynamic-base-scratch
PAYLOAD_FILES := tmp -C ${TOP}/build/opt/kontain/ --exclude '*.a' --exclude '*c++*' runtime lib
# Command to run tests in testenv-image container:
CONTAINER_TEST_CMD := km hello_test.kmd Hello 'The One'
CONTAINER_TEST_ALL_CMD := ${CONTAINER_TEST_CMD}
TESTENV_EXTRA_FILES := ${TOP}/tests/hello_test.kmd

all test test-krun test-all:
	@echo Skipping $@

clean clobber:
	@true

RUNENV_VALIDATE_CMD := ("./hello_test" "Hello, World!")
RUNENV_VALIDATE_EXPECTED := Hello, World

RUNENV_TAG := ${COMPONENT}

export define runenv_prep
	mkdir -p tmp && rm -rf tmp/* && chmod a+rwx tmp
# hello_test.kmd rpath for runenv-image and demo-runenv-image must point to /opt/kontain.
# This file is used by multiple payloads so we check wheteher it already has correct rpath and if not, we re-build it
# Note: it will be already correct when runenv/demo-runenv done from payloads level because we pre-buid it there to avoid
# race condition when using -j
	[ ! -f ${TOP}/tests/hello_test.kmd ] || ! (readelf -l ${TOP}/tests/hello_test.kmd | grep -q ": /opt/kontain/") && (rm -f ${TOP}/tests/hello_test.kmd && make -C ${TOP}/tests RPATH=/opt/kontain hello_test.kmd) || echo -n ""
	tar -czf ${RUNENV_PATH}/libs.tar ${PAYLOAD_FILES}
	cp ${TESTENV_EXTRA_FILES} ${RUNENV_DEMO_PATH}
	mkdir -p ${RUNENV_PATH}/.kontain
endef
include ${TOP}/make/images.mk

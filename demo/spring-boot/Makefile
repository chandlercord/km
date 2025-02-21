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

COMPONENT := spring-boot-demo
include ${TOP}/make/images.mk

CURRENT_DIR := $(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
SRC_REPO_DIR := ${CURRENT_DIR}/rest-service-11
SRC_DIR := ${SRC_REPO_DIR}/complete
APP_JAR := ${SRC_DIR}/build/libs/rest-service-0.0.1-SNAPSHOT.jar

DEFAULT_DEMO_PORT := 8080

.PHONY: all
all: | build build/demo

.PHONY: build
build: | ${SRC_REPO_DIR} ${APP_JAR}

REPO_COMMIT := f7f2acb31d27f57550d6a9d37bbe50e185d1ca4b
${SRC_REPO_DIR}:
	git clone https://github.com/spring-guides/gs-rest-service.git ${SRC_REPO_DIR}
	cd ${SRC_REPO_DIR}; git reset --hard ${REPO_COMMIT}

BUILDER_CONTAINER := spring-boot-demo-builder
${APP_JAR}:
	$(call clean_container,${BUILDER_CONTAINER})
	docker run --rm -it -d -v ${SRC_REPO_DIR}:/app:z --name ${BUILDER_CONTAINER} java
	docker exec -w /app/complete -u $(shell id -u):$(shell id -g) ${BUILDER_CONTAINER} ./gradlew build
	$(call clean_container,${BUILDER_CONTAINER})

DEMO_IMAGE ?= kontainapp/spring-boot-demo:${IMAGE_VERSION}
DEMO_CONTAINER ?= kontainapp_sprint_boot_demo

.PHONY: build/demo
# Docker requires relative path to the building context
build/demo: TARGET_JAR_PATH := $(shell realpath --canonicalize-missing --relative-to="${CURRENT_DIR}" "${APP_JAR}")
build/demo: clean/image
	${DOCKER_BUILD} -t ${DEMO_IMAGE} --build-arg TARGET_JAR_PATH=${TARGET_JAR_PATH} -f kontain.dockerfile ${CURRENT_DIR}

run_demo = ${DOCKER_RUN} ${DOCKER_KRUN_RUNTIME} -d -v $$(pwd):/mnt:z -p ${DEFAULT_DEMO_PORT}:8080 --name ${DEMO_CONTAINER} ${DEMO_IMAGE} /run.sh
.PHONY: run/demo
run/demo:
	$(call run_demo)

.PHONY: clean/build
clean/build:
	$(call clean_container,${BUILDER_CONTAINER})
	rm -rf ${SRC_REPO_DIR}

.PHONY: clean/demo
clean/demo:
	$(call clean_container,${DEMO_CONTAINER})

.PHONY: clean/image
clean/image:
	$(call clean_container_image,${DEMO_IMAGE})

.PHONY: clean
clean: clean/build clean/demo clean/image

.PHONY: test
test: clean/test
	$(call run_demo)
	${CURRENT_DIR}/test/test.sh
	$(call clean_container,${DEMO_CONTAINER})

.PHONY: clean/test
clean/test:
	$(call clean_container,${DEMO_CONTAINER})

DEMO_IMAGE_REG := $(subst kontainapp/,$(REGISTRY)/,$(DEMO_IMAGE))
# note: to login, `make -C cloud/docker login' - see docs/how-twos/login-dockerhub.md
publish-demo: ## publish to docker.io (requred login)
	$(MAKE) MAKEFLAGS="$(MAKEFLAGS)" .push-image \
		FROM=$(DEMO_IMAGE) TO=$(DEMO_IMAGE_REG)
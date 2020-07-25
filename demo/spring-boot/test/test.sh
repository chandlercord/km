#!/bin/bash
#
# Copyright © 2020 Kontain Inc. All rights reserved.
#
# Kontain Inc CONFIDENTIAL
#
#  This file includes unpublished proprietary source code of Kontain Inc. The
#  copyright notice above does not evidence any actual or intended publication of
#  such source code. Disclosure of this source code or any related proprietary
#  information is strictly prohibited without the express written permission of
#  Kontain Inc.
# 
# This is a minimal script used to get time to active readings.
#
set -e
[ "$TRACE" ] && set -x

until $(curl --output /dev/null --silent --fail http://localhost:8080/greeting); do
    sleep 1
done
echo $(date +%s%N)

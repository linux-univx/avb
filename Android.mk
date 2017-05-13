#
# Copyright 2016, The Android Open Source Project
#
# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation
# files (the "Software"), to deal in the Software without
# restriction, including without limitation the rights to use, copy,
# modify, merge, publish, distribute, sublicense, and/or sell copies
# of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#

LOCAL_PATH := $(my-dir)

avb_common_cflags := \
    -D_FILE_OFFSET_BITS=64 \
    -D_POSIX_C_SOURCE=199309L \
    -Wa,--noexecstack \
    -Werror \
    -Wall \
    -Wextra \
    -Wformat=2 \
    -Wno-psabi \
    -Wno-unused-parameter \
    -ffunction-sections \
    -fstack-protector-strong \
    -g
avb_common_ldflags := \
    -Wl,--gc-sections \
    -rdynamic

include $(CLEAR_VARS)
LOCAL_MODULE := bootctrl.avb
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := \
    boot_control/boot_control_avb.c
LOCAL_CLANG := true
LOCAL_CFLAGS := $(avb_common_cflags) -DAVB_COMPILATION
LOCAL_LDFLAGS := $(avb_common_ldflags)
LOCAL_SHARED_LIBRARIES := \
    libbase \
    libcutils
LOCAL_STATIC_LIBRARIES := \
    libavb_user \
    libfs_mgr
LOCAL_POST_INSTALL_CMD := \
	$(hide) mkdir -p $(TARGET_OUT_SHARED_LIBRARIES)/hw && \
	ln -sf bootctrl.avb.so $(TARGET_OUT_SHARED_LIBRARIES)/hw/bootctrl.default.so
include $(BUILD_SHARED_LIBRARY)

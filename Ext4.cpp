/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/wait.h>

#include <linux/kdev_t.h>

#define LOG_TAG "Vold"
#include <cutils/log.h>
#include <cutils/properties.h>

#include <logwrap/logwrap.h>

#include "Ext4.h"
#include "VoldUtil.h"

static char E2FSCK_PATH[] = HELPER_PATH "e2fsck";
static char MKEXT4FS_PATH[] = HELPER_PATH "make_ext4fs";
static char MKE2FS_PATH[] = HELPER_PATH "mke2fs";

int Ext4::doMount(const char *fsPath, const char *mountPoint, bool ro, bool remount,
        bool executable, bool sdcard, const char *mountOpts) {
    int rc;
    unsigned long flags;
    char data[1024];

    data[0] = '\0';
    if (mountOpts)
        strlcat(data, mountOpts, sizeof(data));

    flags = MS_NOATIME | MS_NODEV | MS_NOSUID | MS_DIRSYNC;

    flags |= (executable ? 0 : MS_NOEXEC);
    flags |= (ro ? MS_RDONLY : 0);
    flags |= (remount ? MS_REMOUNT : 0);

    if (sdcard) {
        // Mount external volumes with forced context
        if (data[0])
            strlcat(data, ",", sizeof(data));
        strlcat(data, "context=u:object_r:sdcard_external:s0", sizeof(data));
    }

    rc = mount(fsPath, mountPoint, "ext4", flags, data);

    if (rc && errno == EROFS) {
        SLOGE("%s appears to be a read only filesystem - retrying mount RO", fsPath);
        flags |= MS_RDONLY;
        rc = mount(fsPath, mountPoint, "ext4", flags, data);
    }

    return rc;
}

int Ext4::check(const char *fsPath) {
    if (access(E2FSCK_PATH, X_OK)) {
        SLOGW("Skipping fs checks.\n");
        return 0;
    }

    int rc = -1;
    int status;
    do {
        const char *args[4];
        args[0] = E2FSCK_PATH;
        args[1] = "-p";
        args[2] = fsPath;
        args[3] = NULL;

        rc = android_fork_execvp(ARRAY_SIZE(args), (char **)args, &status, false, true);
        SLOGI("E2FSCK returned %d", rc);
        if(rc == 0) {
            SLOGI("EXT4 Filesystem check completed OK.\n");
            return 0;
        }
        if(rc & 1) {
            SLOGI("EXT4 Filesystem check completed, errors corrected OK.\n");
        }
        if(rc & 2) {
            SLOGE("EXT4 Filesystem check completed, errors corrected, need reboot.\n");
        }
        if(rc & 4) {
            SLOGE("EXT4 Filesystem errors left uncorrected.\n");
        }
        if(rc & 8) {
            SLOGE("E2FSCK Operational error.\n");
            errno = EIO;
        }
        if(rc & 16) {
            SLOGE("E2FSCK Usage or syntax error.\n");
            errno = EIO;
        }
        if(rc & 32) {
            SLOGE("E2FSCK Canceled by user request.\n");
            errno = EIO;
        }
        if(rc & 128) {
            SLOGE("E2FSCK Shared library error.\n");
            errno = EIO;
        }
        if(errno == EIO) {
            return -1;
        }
    } while (0);

    return 0;
}

int Ext4::format(const char *fsPath, const char *mountpoint) {
    const char *args[5];
    int rc;
    int status;

    if (mountpoint == NULL) {
        args[0] = MKE2FS_PATH;
        args[1] = "-j";
        args[2] = "-T";
        args[3] = "ext4";
    } else {
        args[0] = MKEXT4FS_PATH;
        args[1] = "-J";
        args[2] = "-a";
        args[3] = mountpoint;
    }
    args[4] = fsPath;
    rc = android_fork_execvp(ARRAY_SIZE(args), (char **)args, &status, false,
            true);
    if (rc != 0) {
        SLOGE("Filesystem (ext4) format failed due to logwrap error");
        errno = EIO;
        return -1;
    }

    if (!WIFEXITED(status)) {
        SLOGE("Filesystem (ext4) format did not exit properly");
        errno = EIO;
        return -1;
    }

    status = WEXITSTATUS(status);

    if (status == 0) {
        SLOGI("Filesystem (ext4) formatted OK");
        return 0;
    } else {
        SLOGE("Format (ext4) failed (unknown exit code %d)", status);
        errno = EIO;
        return -1;
    }
    return 0;
}

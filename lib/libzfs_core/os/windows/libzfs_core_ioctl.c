/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/zfs_ioctl.h>
#include <os/windows/zfs/sys/zfs_ioctl_compat.h>
#include <libzfs_core.h>

static int
zcmd_ioctl_compat(int fd, int request, zfs_cmd_t *zc, const int cflag)
{
	int ret;
	void *zc_c;
	unsigned long ncmd;
	zfs_iocparm_t *zip;
	DWORD bytesReturned = 0;

	switch (cflag) {
	case ZFS_CMD_COMPAT_NONE:
		ncmd = CTL_CODE(ZFSIOCTL_TYPE, ZFSIOCTL_BASE + request,
		    METHOD_NEITHER, FILE_ANY_ACCESS);

		zip = malloc(sizeof (zfs_iocparm_t));
		zip->zfs_cmd = (uint64_t)zc;
		zip->zfs_cmd_size = sizeof (zfs_cmd_t);
		zip->zfs_ioctl_version = ZFS_IOCVER_ZOF;
		zip->zfs_ioc_error = 0;
		bytesReturned = sizeof (zfs_iocparm_t);

		// ret = ioctl(fd, ncmd, &zp);
		ret = DeviceIoControl(ITOH(fd),
		    (DWORD)ncmd,
		    zip,
		    (DWORD)sizeof (zfs_iocparm_t),
		    zc,
		    (DWORD)sizeof (zfs_cmd_t),
		    &bytesReturned,
		    NULL);

		if (ret == 0)
			ret = GetLastError();
		else
			ret = 0;


		/*
		 * If ioctl worked, get actual rc from kernel, which goes
		 * into errno, and return -1 if not-zero.
		 */
		if (ret == 0) {
			errno = zip->zfs_ioc_error;
			if (zip->zfs_ioc_error != 0)
				ret = -1;
		}
		free(zip);
		return (ret);

	default:
		abort();
		return (EINVAL);
	}

	/* Pass-through ioctl, rarely used if at all */

	ret = ioctl(fd, ncmd, zc_c);
	ASSERT0(ret);

	zfs_cmd_compat_get(zc, (caddr_t)zc_c, cflag);
	free(zc_c);

	return (ret);
}

/*
 * This is the Windows version of ioctl(). Because the XNU kernel
 * handles copyin() and copyout(), we must return success from the
 * ioctl() handler (or it will not copyout() for userland),
 * and instead embed the error return value in the zc structure.
 */
int
lzc_ioctl_fd(int fd, unsigned long request, zfs_cmd_t *zc)
{
	size_t oldsize;
	int ret, cflag = ZFS_CMD_COMPAT_NONE;

	oldsize = zc->zc_nvlist_dst_size;
	ret = zcmd_ioctl_compat(fd, request, zc, cflag);

	if (ret == 0 && oldsize < zc->zc_nvlist_dst_size) {
		ret = -1;
		errno = ENOMEM;
	}

	return (ret);
}

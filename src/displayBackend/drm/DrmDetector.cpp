/*
 *  DRM Detector helper module
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * Copyright (C) 2020 EPAM Systems Inc.
 *
 */
#include "DrmDetector.hpp"

#include <memory>
#include <string>

#include <libudev.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>

#include <xen/be/Log.hpp>

namespace Drm {

struct drmFdGuard {
	drmFdGuard(int fd) : mFd(fd) {}
	~drmFdGuard()
	{
		if (mFd > 0)
		{
			drmClose(mFd);
		}
	}
    operator int() const { return mFd; }

	drmFdGuard(const drmFdGuard&) = delete;
	drmFdGuard& operator=(drmFdGuard const&) = delete;

private:
	int mFd;
};

static bool isDrmDevice(udev_device* device, XenBackend::Log& log)
{
	auto filename = udev_device_get_devnode(device);
	auto sysnum = udev_device_get_sysnum(device);

	if (!filename || !sysnum || atoi(sysnum) < 0)
	{
		return false;
	}

	drmFdGuard fd(open(filename, O_RDWR | O_CLOEXEC));
	if (fd < 0)
	{
		return false;
	}

	std::unique_ptr<drmModeRes, decltype(&drmModeFreeResources)>
			res(drmModeGetResources(fd), drmModeFreeResources);
	if (!res)
	{
		return false;
	}

	if (res->count_crtcs <= 0 || res->count_connectors <= 0 ||
	    res->count_encoders <= 0)
	{
		return false;
	}

	return true;
}

std::string detectDrmDevice()
{
	XenBackend::Log log("DrmDetector");

	LOG(log, INFO) << "Auto detecting DRM KMS device";
	std::unique_ptr<struct udev, decltype(&udev_unref)>
			udev(udev_new(), &udev_unref);
	if (!udev)
	{
		LOG(log, ERROR) << "Cannot create udev context";
		return "";
	}

	std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)>
			e(udev_enumerate_new(udev.get()), udev_enumerate_unref);
	if (!e)
	{
		LOG(log, ERROR) << "Cannot create udev enumerator";
		return "";
	}

	if (udev_enumerate_add_match_subsystem(e.get(), "drm"))
	{
		LOG(log, ERROR) << "Error adding subsystem match";
		return "";
	}

	if (udev_enumerate_add_match_sysname(e.get(), "card[0-9]*"))
	{
		LOG(log, ERROR) << "Error adding sysname match";
		return "";
	}

	if (udev_enumerate_scan_devices(e.get()))
	{
		LOG(log, ERROR) << "Error scanning for udev devices";
		return "";
	}

	udev_list_entry *entry;
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e.get()))
	{
		auto path = udev_list_entry_get_name(entry);
		if (!path)
		{
			continue;
		}

		std::unique_ptr<struct udev_device, decltype(&udev_device_unref)>
				device(udev_device_new_from_syspath(udev.get(), path),
					   udev_device_unref);

		if (!device)
		{
			continue;
		}

		if (isDrmDevice(device.get(), log))
		{
			auto filename = udev_device_get_devnode(device.get());
			LOG(log, INFO) << "Using " << filename;

			return filename;
		}
	}

	LOG(log, WARNING) << "Could not auto detect DRM device";
	return "";
}

}

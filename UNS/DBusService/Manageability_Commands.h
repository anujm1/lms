/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2017-2018 Intel Corporation
 */
#include "DBusSkeleton.h"

namespace Intel {
namespace DBus {
namespace Manageability {
	gboolean on_bus_acquired(GDBusConnection *connection, LmsManageability **skeleton_manageability,
				gpointer user_data);
}}} //namespace

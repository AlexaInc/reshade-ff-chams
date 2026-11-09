/*
 * Copyright (C) 2014 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "runtime.hpp"
#include <Windows.h>
/* chams mod: no <WinInet.h> -- this build makes no outbound connections.
 * The only network user in upstream ReShade was this update check. */

unsigned int reshade::runtime::s_latest_version[3] = {};

void reshade::runtime::check_for_update()
{
	// chams mod: network access removed on purpose.
	// Upstream ReShade contacted https://api.github.com/repos/crosire/reshade/tags
	// on every start. This build never opens a socket or an HTTP session.
	return;
}

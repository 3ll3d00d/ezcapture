/*
 *      Copyright (C) 2025 Matt Khan
 *      https://github.com/3ll3d00d/ezcapture
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include "version_rev.h"

#define EZ_VERSION_MAJOR     0
#define EZ_VERSION_MINOR     7
#define EZ_VERSION_REVISION  0

/////////////////////////////////////////////////////////
#ifndef ISPP_INCLUDED

#define DO_MAKE_STR(x) #x
#define MAKE_STR(x) DO_MAKE_STR(x)

#if EZ_VERSION_BUILD > 0
#define EZ_VERSION EZ_VERSION_MAJOR.EZ_VERSION_MINOR.EZ_VERSION_REVISION.EZ_VERSION_BUILD-git
#define EZ_VERSION_TAG EZ_VERSION_MAJOR, EZ_VERSION_MINOR, EZ_VERSION_REVISION, EZ_VERSION_BUILD
#else
#define EZ_VERSION EZ_VERSION_MAJOR.EZ_VERSION_MINOR.EZ_VERSION_REVISION
#define EZ_VERSION_TAG EZ_VERSION_MAJOR, EZ_VERSION_MINOR, EZ_VERSION_REVISION
#endif

#define EZ_VERSION_STR MAKE_STR(EZ_VERSION)

#endif

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

#define DO_MAKE_STR(x) #x
#define MAKE_STR(x) DO_MAKE_STR(x)

#ifdef EZ_VERSION_MAJOR
#ifdef EZ_VERSION_MINOR
#ifdef EZ_VERSION_PATCH
#ifdef EZ_VERSION_SUFFIX
#define EZ_VERSION EZ_VERSION_MAJOR.EZ_VERSION_MINOR.EZ_VERSION_PATCH-EZ_VERSION_SUFFIX
#else
#define EZ_VERSION EZ_VERSION_MAJOR.EZ_VERSION_MINOR.EZ_VERSION_PATCH
#endif
#endif
#endif
#endif

#ifndef EZ_VERSION
#define EZ_VERSION 0
#endif

#define EZ_VERSION_STR MAKE_STR(EZ_VERSION)

#pragma message("EZ_VERSION_STR [" EZ_VERSION_STR "]")

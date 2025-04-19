/*
 *      Copyright (C) 2025 Matt Khan
 *      https://github.com/3ll3d00d/mwcapture
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
 */#pragma once
#include <bit>

#include "DeckLinkAPI_h.h"

inline bool is_aligned(const void* ptr, size_t alignment)
{
	return (std::bit_cast<std::uintptr_t>(ptr) & (alignment - 1)) == 0;
}

void convert_v210_p210(IDeckLinkVideoInputFrame* frame, uint16_t** yPlane, uint16_t** uPlane, uint16_t** vPlane);

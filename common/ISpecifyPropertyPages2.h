/*
 * (C) 2003-2006 Gabest
 * (C) 2006-2010 MPC-HC Authors
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#ifndef I_SPECIFY_PROPERTY_PAGES2_HEADER
#define I_SPECIFY_PROPERTY_PAGES2_HEADER

#define NOMINMAX // quill does not compile without this

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <OCIdl.h>

interface __declspec(uuid("03481710-D73E-4674-839F-03EDE2D60ED8")) ISpecifyPropertyPages2 : public ISpecifyPropertyPages
{
    STDMETHOD(CreatePage)(const GUID &guid, IPropertyPage **ppPage) = 0;
};
#endif

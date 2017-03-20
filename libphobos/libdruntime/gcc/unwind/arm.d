// Exception handling and frame unwind runtime interface routines.
// Copyright (C) 2011-2017 Free Software Foundation, Inc.

// GCC is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3, or (at your option) any later
// version.

// GCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
// for more details.

// You should have received a copy of the GNU General Public License
// along with GCC; see the file COPYING3.  If not see
// <http://www.gnu.org/licenses/>.

// extern(C) interface for the ARM EABI unwinder library.
// This corresponds to unwind-arm.h

module gcc.unwind.arm;

import gcc.config;

static if (GNU_ARM_EABI_Unwinder):

public import gcc.unwind.arm_common;

extern (C):

enum int UNWIND_STACK_REG = 13;
// Use IP as a scratch register within the personality routine.
enum int UNWIND_POINTER_REG = 12;

version (linux)
    enum _TTYPE_ENCODING = (DW_EH_PE_pcrel | DW_EH_PE_indirect);
else version (NetBSD)
    enum _TTYPE_ENCODING = (DW_EH_PE_pcrel | DW_EH_PE_indirect);
else version (FreeBSD)
    enum _TTYPE_ENCODING = (DW_EH_PE_pcrel | DW_EH_PE_indirect);
else version (symbian) // TODO: name
    enum _TTYPE_ENCODING = (DW_EH_PE_absptr);
else version (uclinux) // TODO: name
    enum _TTYPE_ENCODING = (DW_EH_PE_absptr);
else
    enum _TTYPE_ENCODING = (DW_EH_PE_pcrel);

// Decode an R_ARM_TARGET2 relocation.
_Unwind_Word _Unwind_decode_typeinfo_ptr(_Unwind_Word base, _Unwind_Word ptr)
{
    _Unwind_Word tmp;

    tmp = *cast(_Unwind_Word*) ptr;
    // Zero values are always NULL.
    if (!tmp)
        return 0;

    if (_TTYPE_ENCODING == (DW_EH_PE_pcrel | DW_EH_PE_indirect))
    {
        // Pc-relative indirect.
        tmp += ptr;
        tmp = *cast(_Unwind_Word*) tmp;
    }
    else if (_TTYPE_ENCODING == DW_EH_PE_absptr)
    {
        // Absolute pointer.  Nothing more to do.
    }
    else
    {
        // Pc-relative pointer.
        tmp += ptr;
    }
    return tmp;
}

_Unwind_Reason_Code __gnu_unwind_24bit(_Unwind_Context* context, _uw data, int compact)
{
    return _URC_FAILURE;
}

// Return the address of the instruction, not the actual IP value.
_Unwind_Word _Unwind_GetIP(_Unwind_Context* context)
{
    return _Unwind_GetGR(context, 15) & ~ cast(_Unwind_Word) 1;
}

// The dwarf unwinder doesn't understand arm/thumb state.  We assume the
// landing pad uses the same instruction set as the call site.
void _Unwind_SetIP(_Unwind_Context* context, _Unwind_Word val)
{
    return _Unwind_SetGR(context, 15, val | (_Unwind_GetGR(context, 15) & 1));
}

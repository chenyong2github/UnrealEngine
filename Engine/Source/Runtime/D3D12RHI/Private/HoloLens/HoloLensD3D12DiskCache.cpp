// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Implementation of D3D12 Disk caching functions to preserve state across runs

//-----------------------------------------------------------------------------
//	Include Files
//-----------------------------------------------------------------------------
#include "D3D12RHIPrivate.h"

// Borrow the Windows desktop version with unavailable APIs remapped
#define CreateFile(name, access, share, unused1, flag, attr, unused2)  CreateFile2((name), (access), (share), (flag), NULL)
#include "Windows/WindowsD3D12DiskCache.cpp"
#undef CreateFile
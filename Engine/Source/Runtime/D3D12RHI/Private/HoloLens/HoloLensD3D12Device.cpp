// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WindowsD3D12Device.cpp: Windows D3D device RHI implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"

// Borrow the Windows desktop version with unavailable APIs remapped
#define CreateDXGIFactory CreateDXGIFactory1
#include "Windows/WindowsD3D12Device.cpp"
#undef CreateDXGIFactory
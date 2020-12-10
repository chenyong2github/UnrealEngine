// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D11BaseRHIPrivate.h: Private D3D RHI definitions for Windows.
=============================================================================*/

#pragma once

#include "Windows/WindowsHWrapper.h"

// Disable macro redefinition warning for compatibility with Windows SDK 8+
#pragma warning(push)
#pragma warning(disable : 4005)	// macro redefinition

// D3D headers.
#if PLATFORM_64BITS
#pragma pack(push,16)
#else
#pragma pack(push,8)
#endif
#define D3D_OVERLOADS 1
#include "Windows/AllowWindowsPlatformTypes.h"
#include <D3D11.h>
#include "Windows/HideWindowsPlatformTypes.h"

#undef DrawText

#pragma pack(pop)
#pragma warning(pop)

typedef ID3D11DeviceContext FD3D11DeviceContext;
typedef ID3D11Device FD3D11Device;

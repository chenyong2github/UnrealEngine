// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12BaseRHIPrivate.h: Private D3D RHI definitions for HoloLens.
=============================================================================*/

#pragma once

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
#include "HoloLens/AllowWindowsPlatformTypes.h"
#include <d3d12.h>
#include <d3dx12.h>
#include <dxgidebug.h>
#include <d3d12sdklayers.h>
#include "HoloLens/HideWindowsPlatformTypes.h"

#undef DrawText

#pragma pack(pop)
#pragma warning(pop)

#define D3D12RHI_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER D3D12_RESOURCE_FLAG_NONE
#define D3D12RHI_HEAP_FLAG_ALLOW_INDIRECT_BUFFERS		D3D12_HEAP_FLAG_NONE

#define D3D12RHI_NEEDS_VENDOR_EXTENSIONS     0
#define D3D12RHI_NEEDS_SHADER_FEATURE_CHECKS 0

// Heap create not zeroed flag is not available on Hololens so use internal define to disable the feature
// but make code path shared when it becomes available
#define FD3D12_HEAP_FLAG_CREATE_NOT_ZEROED D3D12_HEAP_FLAG_NONE

#include "../Public/D3D12Util.h"

// Windows desktop version is used.
#include "Windows/WindowsD3D12DiskCache.h"
#include "Windows/WindowsD3D12PipelineState.h"

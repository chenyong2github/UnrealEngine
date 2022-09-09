// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12BaseRHIPrivate.h: Private D3D RHI definitions for Windows.
=============================================================================*/

#pragma once

#include "Windows/D3D12ThirdParty.h"

#define D3D12RHI_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER D3D12_RESOURCE_FLAG_NONE
#define D3D12RHI_HEAP_FLAG_ALLOW_INDIRECT_BUFFERS		D3D12_HEAP_FLAG_NONE

#define D3D12RHI_NEEDS_VENDOR_EXTENSIONS     1
#define D3D12RHI_NEEDS_SHADER_FEATURE_CHECKS 1

#include "D3D12Util.h"
#include "WindowsD3D12DiskCache.h"
#include "WindowsD3D12PipelineState.h"

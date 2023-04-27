// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	D3D12BaseRHIPrivate.h: Private D3D RHI definitions for HoloLens.
=============================================================================*/

#pragma once

// Hololens uses the graphics command list interface for the copy queue.
typedef ID3D12GraphicsCommandList ID3D12CopyCommandList;

#define D3D12RHI_PLATFORM_COPY_COMMAND_LIST_TYPE D3D12_COMMAND_LIST_TYPE_COPY

#define FD3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER

// Hololens uses standard D3D12 query heaps for timestamps
#define D3D12RHI_PLATFORM_USES_TIMESTAMP_QUERIES 1

#define D3D12RHI_RESOURCE_FLAG_ALLOW_INDIRECT_BUFFER D3D12_RESOURCE_FLAG_NONE
#define D3D12RHI_HEAP_FLAG_ALLOW_INDIRECT_BUFFERS		D3D12_HEAP_FLAG_NONE

#define D3D12RHI_NEEDS_VENDOR_EXTENSIONS     0
#define D3D12RHI_NEEDS_SHADER_FEATURE_CHECKS 0

// Heap create not zeroed flag is not available on Hololens so use internal define to disable the feature
// but make code path shared when it becomes available
#define FD3D12_HEAP_FLAG_CREATE_NOT_ZEROED D3D12_HEAP_FLAG_NONE

#define USE_STATIC_ROOT_SIGNATURE			0
#define D3D12_USE_DUMMY_BACKBUFFER			1

#include "D3D12Util.h"

// Windows desktop version is used.
#include "Windows/WindowsD3D12DiskCache.h"
#include "Windows/WindowsD3D12PipelineState.h"

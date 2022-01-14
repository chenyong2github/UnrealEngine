// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXRHIPrivate.h: Private AGX RHI definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "Misc/CommandLine.h"
#include "PixelFormat.h"

// Dependencies
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#define AGXRHI_TRUE  1
#define AGXRHI_FALSE 0

// Metal C++ wrapper
THIRD_PARTY_INCLUDES_START
#include "mtlpp.hpp"
THIRD_PARTY_INCLUDES_END

extern id<MTLDevice> AGXUtilGetDevice();

// The Metal device object.
extern id<MTLDevice> GMtlDevice;

// Placeholder: TODO: remove
extern mtlpp::Device GMtlppDevice;

// Whether the AGX RHI is initialized sufficiently to handle resources
extern bool GIsAGXInitialized;

// Requirement for vertex buffer offset field
#if PLATFORM_MAC
const uint32 BufferOffsetAlignment = 256;
const uint32 BufferBackedLinearTextureOffsetAlignment = 1024;
#else
const uint32 BufferOffsetAlignment = 16;
const uint32 BufferBackedLinearTextureOffsetAlignment = 64;
#endif

// The maximum buffer page size that can be uploaded in a set*Bytes call
const uint32 AGXBufferPageSize = 4096;

// The buffer size that is more efficiently uploaded in a set*Bytes call - defined in terms of BufferOffsetAlignment
#if PLATFORM_MAC
const uint32 AGXBufferBytesSize = BufferOffsetAlignment * 2;
#else
const uint32 AGXBufferBytesSize = BufferOffsetAlignment * 32;
#endif

#include "AGXRHI.h"
#include "AGXDynamicRHI.h"
#include "RHI.h"

/**
 * EAGXDebugLevel: Level of AGX RHI debug features to be enabled.
 */
enum EAGXDebugLevel
{
	EAGXDebugLevelOff,
	EAGXDebugLevelFastValidation,
	EAGXDebugLevelResetOnBind,
	EAGXDebugLevelConditionalSubmit,
	EAGXDebugLevelValidation,
	EAGXDebugLevelLogOperations,
	EAGXDebugLevelWaitForComplete,
};

/**
 * The sampler, buffer and texture resource limits as defined here:
 * https://developer.apple.com/library/ios/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Render-Ctx/Render-Ctx.html
 */
#if PLATFORM_IOS
#define METAL_MAX_BUFFERS 31
#define METAL_MAX_TEXTURES 31
typedef uint32 FAGXTextureMask;
#elif PLATFORM_MAC
#define METAL_MAX_BUFFERS 31
#define METAL_MAX_TEXTURES 128
typedef __uint128_t FAGXTextureMask;
#else
#error "Unsupported Platform!"
#endif
typedef uint32 FAGXBufferMask;
typedef uint16 FAGXSamplerMask;

enum EAGXLimits
{
	ML_MaxSamplers = 16, /** Maximum number of samplers */
	ML_MaxBuffers = METAL_MAX_BUFFERS, /** Maximum number of buffers */
	ML_MaxTextures = METAL_MAX_TEXTURES, /** Maximum number of textures - there are more textures available on Mac than iOS */
	ML_MaxViewports = 16 /** Technically this may be different at runtime, but this is the likely absolute upper-bound */
};

/** A structure for quick mask-testing of shader-stage resource bindings */
struct FAGXDebugShaderResourceMask
{
	FAGXTextureMask TextureMask;
	FAGXBufferMask BufferMask;
	FAGXSamplerMask SamplerMask;
};

#define BUFFER_CACHE_MODE mtlpp::ResourceOptions::CpuCacheModeDefaultCache

#if PLATFORM_MAC
#define BUFFER_MANAGED_MEM mtlpp::ResourceOptions::StorageModeManaged
#define BUFFER_STORAGE_MODE mtlpp::StorageMode::Managed
#define BUFFER_RESOURCE_STORAGE_MANAGED mtlpp::ResourceOptions::StorageModeManaged
#define BUFFER_DYNAMIC_REALLOC BUF_AnyDynamic
// How many possible vertex streams are allowed
const uint32 MaxMetalStreams = 31;
#else
#define BUFFER_MANAGED_MEM 0
#define BUFFER_STORAGE_MODE mtlpp::StorageMode::Shared
#define BUFFER_RESOURCE_STORAGE_MANAGED mtlpp::ResourceOptions::StorageModeShared
#define BUFFER_DYNAMIC_REALLOC BUF_AnyDynamic
// How many possible vertex streams are allowed
const uint32 MaxMetalStreams = 30;
#endif

// Unavailable on iOS, but dealing with this clutters the code.
enum EMTLTextureType
{
	EMTLTextureTypeCubeArray = 6
};

// This is the right VERSION check, see Availability.h in the SDK
#define METAL_SUPPORTS_INDIRECT_ARGUMENT_BUFFERS 1
#define METAL_SUPPORTS_CAPTURE_MANAGER 1
#define METAL_SUPPORTS_TILE_SHADERS 1
// In addition to compile-time SDK checks we also need a way to check if these are available on runtime
extern bool GAGXSupportsCaptureManager;

struct FAGXBufferFormat
{
	// Valid linear texture pixel formats - potentially different than the actual texture formats
	mtlpp::PixelFormat LinearTextureFormat;
	// Metal buffer data types for manual ALU format conversions
	uint8 DataFormat;
};

extern FAGXBufferFormat GAGXBufferFormats[PF_MAX];

#define METAL_DEBUG_OPTIONS !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#if METAL_DEBUG_OPTIONS
#define METAL_DEBUG_OPTION(Code) Code
#else
#define METAL_DEBUG_OPTION(Code)
#endif

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
#define METAL_DEBUG_ONLY(Code) Code
#define METAL_DEBUG_LAYER(Level, Code) if (AGXSafeGetRuntimeDebuggingLevel() >= Level) Code
#else
#define METAL_DEBUG_ONLY(Code)
#define METAL_DEBUG_LAYER(Level, Code)
#endif

/** Set to 1 to enable GPU events in Xcode frame debugger */
#ifndef ENABLE_METAL_GPUEVENTS_IN_TEST
	#define ENABLE_METAL_GPUEVENTS_IN_TEST 0
#endif
#define ENABLE_METAL_GPUEVENTS	(UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT || (UE_BUILD_TEST && ENABLE_METAL_GPUEVENTS_IN_TEST))
#define ENABLE_METAL_GPUPROFILE	(ENABLE_METAL_GPUEVENTS && 1)

#if ENABLE_METAL_GPUPROFILE
#define METAL_GPUPROFILE(Code) Code
#else
#define METAL_GPUPROFILE(Code) 
#endif

#define UNREAL_TO_METAL_BUFFER_INDEX(Index) ((MaxMetalStreams - 1) - Index)
#define METAL_TO_UNREAL_BUFFER_INDEX(Index) ((MaxMetalStreams - 1) - Index)

#define METAL_NEW_NONNULL_DECL (__clang_major__ >= 9)

#if PLATFORM_IOS
#define METAL_FATAL_ERROR(Format, ...)  { UE_LOG(LogAGX, Warning, Format, __VA_ARGS__); FIOSPlatformMisc::MetalAssert(); }
#else
#define METAL_FATAL_ERROR(Format, ...)	UE_LOG(LogAGX, Fatal, Format, __VA_ARGS__)
#endif
#define METAL_FATAL_ASSERT(Condition, Format, ...) if (!(Condition)) { METAL_FATAL_ERROR(Format, __VA_ARGS__); }

#if !defined(METAL_IGNORED)
	#define METAL_IGNORED(Func)
#endif

// Access the internal context for the device-owning DynamicRHI object
FAGXDeviceContext& GetAGXDeviceContext();

// Safely release a metal object, correctly handling the case where the RHI has been destructed first
void AGXRHI_API AGXSafeReleaseMetalObject(id Object);

// Safely release a metal texture, correctly handling the case where the RHI has been destructed first
void AGXSafeReleaseMetalTexture(FAGXTexture& Object);

// Safely release a metal buffer, correctly handling the case where the RHI has been destructed first
void AGXSafeReleaseMetalBuffer(FAGXBuffer& Buffer);

// Safely release a render pass descriptor so that it may be reused.
void AGXSafeReleaseMetalRenderPassDescriptor(mtlpp::RenderPassDescriptor& Desc);

// Access the underlying surface object from any kind of texture
FAGXSurface* AGXGetMetalSurfaceFromRHITexture(FRHITexture* Texture);

#define NOT_SUPPORTED(Func) UE_LOG(LogAGX, Fatal, TEXT("'%s' is not supported"), TEXT(Func));

// Verifies we are on the correct thread to mutate internal AGXRHI resources.
FORCEINLINE void CheckMetalThread()
{
    check((IsInRenderingThread() && (!IsRunningRHIInSeparateThread() || !FRHICommandListExecutor::IsRHIThreadActive())) || IsInRHIThread());
}

FORCEINLINE bool MetalIsSafeToUseRHIThreadResources()
{
	// we can use RHI thread resources if we are on the RHIThread or on RenderingThread when there's no RHI thread, or the RHI thread is stalled or inactive
	return (GIsAGXInitialized && !GIsRHIInitialized) ||
			IsInRHIThread() ||
			(IsInRenderingThread() && (!IsRunningRHIInSeparateThread() || !FRHICommandListExecutor::IsRHIThreadActive() || FRHICommandListImmediate::IsStalled() || FRHICommandListExecutor::IsRHIThreadCompletelyFlushed()));
}

FORCEINLINE int32 GetMetalCubeFace(ECubeFace Face)
{
	// According to Metal docs these should match now: https://developer.apple.com/library/prerelease/ios/documentation/Metal/Reference/MTLTexture_Ref/index.html#//apple_ref/c/tdef/MTLTextureType
	switch (Face)
	{
		case CubeFace_PosX:;
		default:			return 0;
		case CubeFace_NegX:	return 1;
		case CubeFace_PosY:	return 2;
		case CubeFace_NegY:	return 3;
		case CubeFace_PosZ:	return 4;
		case CubeFace_NegZ:	return 5;
	}
}

FORCEINLINE mtlpp::LoadAction GetMetalRTLoadAction(ERenderTargetLoadAction LoadAction)
{
	switch(LoadAction)
	{
		case ERenderTargetLoadAction::ENoAction: return mtlpp::LoadAction::DontCare;
		case ERenderTargetLoadAction::ELoad: return mtlpp::LoadAction::Load;
		case ERenderTargetLoadAction::EClear: return mtlpp::LoadAction::Clear;
		default: return mtlpp::LoadAction::DontCare;
	}
}

mtlpp::PrimitiveType AGXTranslatePrimitiveType(uint32 PrimitiveType);

#if PLATFORM_MAC
mtlpp::PrimitiveTopologyClass AGXTranslatePrimitiveTopology(uint32 PrimitiveType);
#endif

mtlpp::PixelFormat AGXToSRGBFormat(mtlpp::PixelFormat LinMTLFormat);

uint8 AGXGetMetalPixelFormatKey(mtlpp::PixelFormat Format);

template<typename TRHIType>
static FORCEINLINE typename TAGXResourceTraits<TRHIType>::TConcreteType* ResourceCast(TRHIType* Resource)
{
	return static_cast<typename TAGXResourceTraits<TRHIType>::TConcreteType*>(Resource);
}

extern uint32 AGXSafeGetRuntimeDebuggingLevel();

extern int32 GAGXBufferZeroFill;

mtlpp::LanguageVersion AGXValidateVersion(uint32 Version);

// Needs to be the same as EShaderFrequency when all stages are supported, but unlike EShaderFrequency you can compile out stages.
enum EAGXShaderStages
{
	Vertex,
	Pixel,
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	Geometry,
#endif
	Compute,
	
	Num,
};

FORCEINLINE EShaderFrequency GetRHIShaderFrequency(EAGXShaderStages Stage)
{
	switch (Stage)
	{
		case EAGXShaderStages::Vertex:
			return SF_Vertex;
		case EAGXShaderStages::Pixel:
			return SF_Pixel;
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		case EAGXShaderStages::Geometry:
			return SF_Geometry;
#endif
		case EAGXShaderStages::Compute:
			return SF_Compute;
		default:
			return SF_NumFrequencies;
	}
}

FORCEINLINE EAGXShaderStages GetMetalShaderFrequency(EShaderFrequency Stage)
{
	switch (Stage)
	{
		case SF_Vertex:
			return EAGXShaderStages::Vertex;
		case SF_Pixel:
			return EAGXShaderStages::Pixel;
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		case SF_Geometry:
			return EAGXShaderStages::Geometry;
#endif
		case SF_Compute:
			return EAGXShaderStages::Compute;
		default:
			return EAGXShaderStages::Num;
	}
}

#include "AGXStateCache.h"
#include "AGXContext.h"

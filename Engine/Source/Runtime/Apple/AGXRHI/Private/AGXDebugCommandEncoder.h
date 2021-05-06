// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AGXCommandBuffer.h"

// For some reason when including this file while building the editor Clang 9 ignores this pragma from MacPlatformCompilerPreSetup.h,
// resulting in errors in FAGXDebugBufferBindings, FAGXDebugTextureBindings and FAGXDebugSamplerBindings. Readding it here works around this problem.
#if (__clang_major__ >= 9)
#pragma clang diagnostic ignored "-Wnullability-inferred-on-nested-type"
#endif

NS_ASSUME_NONNULL_BEGIN

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

enum EAGXShaderFrequency
{
    EAGXShaderVertex = 0,
    EAGXShaderFragment = 1,
    EAGXShaderCompute = 2,
	EAGXShaderStream = 3,
    EAGXShaderRenderNum = 2,
	EAGXShaderStagesNum = 4
};

/** A structure for quick mask-testing of shader-stage resource bindings */
struct FAGXDebugShaderResourceMask
{
	FAGXTextureMask TextureMask;
	FAGXBufferMask BufferMask;
	FAGXSamplerMask SamplerMask;
};

/** A structure of arrays for the current buffer binding settings. */
struct FAGXDebugBufferBindings
{
    /** The bound buffers or nil. */
    id<MTLBuffer> _Nullable Buffers[ML_MaxBuffers];
    /** Optional bytes buffer used instead of an id<MTLBuffer> */
    void const* _Nullable Bytes[ML_MaxBuffers];
    /** The bound buffer offsets or 0. */
    NSUInteger Offsets[ML_MaxBuffers];
};

/** A structure of arrays for the current texture binding settings. */
struct FAGXDebugTextureBindings
{
    /** The bound textures or nil. */
    id<MTLTexture> _Nullable Textures[ML_MaxTextures];
};

/** A structure of arrays for the current sampler binding settings. */
struct FAGXDebugSamplerBindings
{
    /** The bound sampler states or nil. */
    id<MTLSamplerState> _Nullable Samplers[ML_MaxSamplers];
};

#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
class FAGXCommandBufferDebugging;
@class FAGXDebugCommandBuffer;
@class FAGXDebugFence;

@interface FAGXDebugCommandEncoder : FApplePlatformObject
{
@public
	NSHashTable<FAGXDebugFence*>* UpdatedFences;
	NSHashTable<FAGXDebugFence*>* WaitingFences;
}
-(instancetype)init;
@end

class FAGXCommandEncoderDebugging : public ns::Object<FAGXDebugCommandEncoder*>
{
public:
	FAGXCommandEncoderDebugging();
	FAGXCommandEncoderDebugging(FAGXDebugCommandEncoder* handle);
	
	void AddUpdateFence(id Fence);
	void AddWaitFence(id Fence);
};

#endif
NS_ASSUME_NONNULL_END

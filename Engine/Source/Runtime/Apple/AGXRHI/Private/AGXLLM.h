// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AGXRHIPrivate.h"
#include "HAL/LowLevelMemTracker.h"
#include "Apple/AppleLLM.h"

@interface FAGXDeallocHandler : FApplePlatformObject<NSObject>
{
	dispatch_block_t Block;
}
-(instancetype)initWithBlock:(dispatch_block_t)InBlock;
-(void)dealloc;
@end

#if ENABLE_LOW_LEVEL_MEM_TRACKER

#define LLM_SCOPE_METAL(Tag) LLM_SCOPE((ELLMTag)Tag)
#define LLM_PLATFORM_SCOPE_METAL(Tag) LLM_PLATFORM_SCOPE((ELLMTag)Tag)

enum class ELLMTagAGX : LLM_TAG_TYPE
{
	Buffers = (LLM_TAG_TYPE)ELLMTagApple::AppleMetalTagsStart,
	Textures,
	Heaps,
	RenderTargets,
	
	Count
};

static_assert((int32)ELLMTagAGX::Count <= (int32)ELLMTagApple::AppleMetalTagsEnd, "too many ELLMTagAGX tags. Need to increase LLM_TAG_APPLE_NUM_METAL_TAGS_RESERVED");

namespace AGXLLM
{
	void Initialise();
}

#else

#define LLM_SCOPE_METAL(...)
#define LLM_PLATFORM_SCOPE_METAL(...)

#endif		// #if ENABLE_LOW_LEVEL_MEM_TRACKER

// These work without the LLM module
namespace AGXLLM
{
	void LogAllocTexture(mtlpp::TextureDescriptor const& Desc, mtlpp::Texture const& Texture);
	void LogAllocBuffer(mtlpp::Buffer const& Buffer);
	
	void LogAliasTexture(mtlpp::Texture const& Texture);
	void LogAliasBuffer(mtlpp::Buffer const& Buffer);
}



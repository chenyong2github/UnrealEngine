// Copyright Epic Games, Inc. All Rights Reserved.

#include "AGXLLM.h"
#include "AGXProfiler.h"
#include "RenderUtils.h"
#include "HAL/LowLevelMemStats.h"

#include <objc/runtime.h>

#if ENABLE_LOW_LEVEL_MEM_TRACKER

struct FLLMTagInfoAGX
{
	const TCHAR* Name;
	FName StatName;				// shows in the LLMFULL stat group
	FName SummaryStatName;		// shows in the LLM summary stat group
};

DECLARE_LLM_MEMORY_STAT(TEXT("Metal Buffers"), STAT_AGXBuffersLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Metal Textures"), STAT_AGXTexturesLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Metal Heaps"), STAT_AGXHeapsLLM, STATGROUP_LLMPlatform);
DECLARE_LLM_MEMORY_STAT(TEXT("Metal RenderTargets"), STAT_AGXRenderTargetsLLM, STATGROUP_LLMPlatform);

// *** order must match ELLMTagAGX enum ***
const FLLMTagInfoAGX ELLMTagNamesAGX[] =
{
	// csv name									// stat name										// summary stat name						// enum value
	{ TEXT("Metal Buffers"),		GET_STATFNAME(STAT_AGXBuffersLLM),		GET_STATFNAME(STAT_EngineSummaryLLM) },		// ELLMTagAGX::Buffers
	{ TEXT("Metal Textures"),		GET_STATFNAME(STAT_AGXTexturesLLM),		GET_STATFNAME(STAT_EngineSummaryLLM) },		// ELLMTagAGX::Textures
	{ TEXT("Metal Heaps"),			GET_STATFNAME(STAT_AGXHeapsLLM),			GET_STATFNAME(STAT_EngineSummaryLLM) },		// ELLMTagAGX::Heaps
	{ TEXT("Metal Render Targets"),	GET_STATFNAME(STAT_AGXRenderTargetsLLM),	GET_STATFNAME(STAT_EngineSummaryLLM) },		// ELLMTagAGX::RenderTargets
};

/*
 * Register Metal tags with LLM
 */
void AGXLLM::Initialise()
{
	int32 TagCount = sizeof(ELLMTagNamesAGX) / sizeof(FLLMTagInfoAGX);

	for (int32 Index = 0; Index < TagCount; ++Index)
	{
		int32 Tag = (int32)ELLMTagApple::AppleMetalTagsStart + Index;
		const FLLMTagInfoAGX& TagInfo = ELLMTagNamesAGX[Index];

		FLowLevelMemTracker::Get().RegisterPlatformTag(Tag, TagInfo.Name, TagInfo.StatName, TagInfo.SummaryStatName);
	}
}

#endif		// #if ENABLE_LOW_LEVEL_MEM_TRACKER

@implementation FAGXDeallocHandler

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXDeallocHandler)

-(instancetype)initWithBlock:(dispatch_block_t)InBlock
{
	id Self = [super init];
	if (Self)
	{
		self->Block = Block_copy(InBlock);
	}
	return Self;
}
-(void)dealloc
{
	self->Block();
	Block_release(self->Block);
	[super dealloc];
}
@end

static MTLPixelFormat FromSRGBFormat(MTLPixelFormat Format)
{
	MTLPixelFormat MTLFormat = Format;
	
	switch (Format)
	{
		case MTLPixelFormatRGBA8Unorm_sRGB:
			MTLFormat = MTLPixelFormatRGBA8Unorm;
			break;
		case MTLPixelFormatBGRA8Unorm_sRGB:
			MTLFormat = MTLPixelFormatBGRA8Unorm;
			break;
#if PLATFORM_MAC
		case MTLPixelFormatBC1_RGBA_sRGB:
			MTLFormat = MTLPixelFormatBC1_RGBA;
			break;
		case MTLPixelFormatBC2_RGBA_sRGB:
			MTLFormat = MTLPixelFormatBC2_RGBA;
			break;
		case MTLPixelFormatBC3_RGBA_sRGB:
			MTLFormat = MTLPixelFormatBC3_RGBA;
			break;
		case MTLPixelFormatBC7_RGBAUnorm_sRGB:
			MTLFormat = MTLPixelFormatBC7_RGBAUnorm;
			break;
#endif //PLATFORM_MAC
#if PLATFORM_IOS
		case MTLPixelFormatR8Unorm_sRGB:
			MTLFormat = MTLPixelFormatR8Unorm;
			break;
		case MTLPixelFormatPVRTC_RGBA_2BPP_sRGB:
			MTLFormat = MTLPixelFormatPVRTC_RGBA_2BPP;
			break;
		case MTLPixelFormatPVRTC_RGBA_4BPP_sRGB:
			MTLFormat = MTLPixelFormatPVRTC_RGBA_4BPP;
			break;
		case MTLPixelFormatASTC_4x4_sRGB:
			MTLFormat = MTLPixelFormatASTC_4x4_LDR;
			break;
		case MTLPixelFormatASTC_6x6_sRGB:
			MTLFormat = MTLPixelFormatASTC_6x6_LDR;
			break;
		case MTLPixelFormatASTC_8x8_sRGB:
			MTLFormat = MTLPixelFormatASTC_8x8_LDR;
			break;
		case MTLPixelFormatASTC_10x10_sRGB:
			MTLFormat = MTLPixelFormatASTC_10x10_LDR;
			break;
		case MTLPixelFormatASTC_12x12_sRGB:
			MTLFormat = MTLPixelFormatASTC_12x12_LDR;
			break;
#endif //PLATFORM_IOS
		default:
			break;
	}
	
	return MTLFormat;
}

static EPixelFormat MetalToRHIPixelFormat(MTLPixelFormat Format)
{
	Format = FromSRGBFormat(Format);
	for (uint32 i = 0; i < PF_MAX; i++)
	{
		if((MTLPixelFormat)GPixelFormats[i].PlatformFormat == Format)
		{
			return (EPixelFormat)i;
		}
	}
	check(false);
	return PF_MAX;
}

void AGXLLM::LogAllocTexture(MTLTextureDescriptor* Desc, id<MTLTexture> Texture)
{
	MTLSizeAndAlign SizeAlign= [GMtlDevice heapTextureSizeAndAlignWithDescriptor:Desc];
	
	void* Ptr = (void*)Texture;
	uint64 Size = static_cast<uint64>(SizeAlign.size);
	
#if PLATFORM_IOS
	bool bMemoryless = ([Texture storageMode] == MTLStorageModeMemoryless);
	if (!bMemoryless)
#endif
	{
		INC_MEMORY_STAT_BY(STAT_AGXTextureMemory, Size);
	}
	INC_DWORD_STAT(STAT_AGXTextureCount);
	
	LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, Ptr, Size, ELLMTag::Untagged, ELLMAllocType::System));
	// Assign a dealloc handler to untrack the memory - but don't track the dispatch block!
	{
		LLM_SCOPED_PAUSE_TRACKING(ELLMAllocType::System);
		
		if ([Desc usage] & MTLTextureUsageRenderTarget)
		{
			objc_setAssociatedObject(Texture, (void*)&AGXLLM::LogAllocTexture,
			[[[FAGXDeallocHandler alloc] initWithBlock:^{
				LLM_PLATFORM_SCOPE_METAL(ELLMTagAGX::RenderTargets);
				
				LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, Ptr, ELLMAllocType::System));
				
#if PLATFORM_IOS
				if (!bMemoryless)
#endif
				{
					DEC_MEMORY_STAT_BY(STAT_AGXTextureMemory, Size);
				}
				DEC_DWORD_STAT(STAT_AGXTextureCount);
			}] autorelease],
			OBJC_ASSOCIATION_RETAIN);
		}
		else
		{
			objc_setAssociatedObject(Texture, (void*)&AGXLLM::LogAllocTexture,
			[[[FAGXDeallocHandler alloc] initWithBlock:^{
				LLM_PLATFORM_SCOPE_METAL(ELLMTagAGX::Textures);
			
				LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, Ptr, ELLMAllocType::System));
			
#if PLATFORM_IOS
				if (!bMemoryless)
#endif
				{
					DEC_MEMORY_STAT_BY(STAT_AGXTextureMemory, Size);
				}
				DEC_DWORD_STAT(STAT_AGXTextureCount);
			}] autorelease],
			OBJC_ASSOCIATION_RETAIN);
		}
	}
}

void AGXLLM::LogAllocBuffer(id<MTLBuffer> Buffer)
{
	void* Ptr = (void*)Buffer;
	uint64 Size = [Buffer length];
	
	INC_MEMORY_STAT_BY(STAT_AGXBufferMemory, Size);
	INC_DWORD_STAT(STAT_AGXBufferCount);
	
	LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, Ptr, Size, ELLMTag::Untagged, ELLMAllocType::System));
	// Assign a dealloc handler to untrack the memory - but don't track the dispatch block!
	{
		LLM_SCOPED_PAUSE_TRACKING(ELLMAllocType::System);
		
		objc_setAssociatedObject(Buffer, (void*)&AGXLLM::LogAllocBuffer,
		[[[FAGXDeallocHandler alloc] initWithBlock:^{
			LLM_PLATFORM_SCOPE_METAL(ELLMTagAGX::Buffers);
			
			LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, Ptr, ELLMAllocType::System));
			
			DEC_MEMORY_STAT_BY(STAT_AGXBufferMemory, Size);
			DEC_DWORD_STAT(STAT_AGXBufferCount);
		}] autorelease],
		OBJC_ASSOCIATION_RETAIN);
	}
}

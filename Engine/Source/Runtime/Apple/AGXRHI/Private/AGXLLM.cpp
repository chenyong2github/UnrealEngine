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

static mtlpp::PixelFormat FromSRGBFormat(mtlpp::PixelFormat Format)
{
	mtlpp::PixelFormat MTLFormat = Format;
	
	switch (Format)
	{
		case mtlpp::PixelFormat::RGBA8Unorm_sRGB:
			MTLFormat = mtlpp::PixelFormat::RGBA8Unorm;
			break;
		case mtlpp::PixelFormat::BGRA8Unorm_sRGB:
			MTLFormat = mtlpp::PixelFormat::BGRA8Unorm;
			break;
#if PLATFORM_MAC
		case mtlpp::PixelFormat::BC1_RGBA_sRGB:
			MTLFormat = mtlpp::PixelFormat::BC1_RGBA;
			break;
		case mtlpp::PixelFormat::BC2_RGBA_sRGB:
			MTLFormat = mtlpp::PixelFormat::BC2_RGBA;
			break;
		case mtlpp::PixelFormat::BC3_RGBA_sRGB:
			MTLFormat = mtlpp::PixelFormat::BC3_RGBA;
			break;
		case mtlpp::PixelFormat::BC7_RGBAUnorm_sRGB:
			MTLFormat = mtlpp::PixelFormat::BC7_RGBAUnorm;
			break;
#endif //PLATFORM_MAC
#if PLATFORM_IOS
		case mtlpp::PixelFormat::R8Unorm_sRGB:
			MTLFormat = mtlpp::PixelFormat::R8Unorm;
			break;
		case mtlpp::PixelFormat::PVRTC_RGBA_2BPP_sRGB:
			MTLFormat = mtlpp::PixelFormat::PVRTC_RGBA_2BPP;
			break;
		case mtlpp::PixelFormat::PVRTC_RGBA_4BPP_sRGB:
			MTLFormat = mtlpp::PixelFormat::PVRTC_RGBA_4BPP;
			break;
		case mtlpp::PixelFormat::ASTC_4x4_sRGB:
			MTLFormat = mtlpp::PixelFormat::ASTC_4x4_LDR;
			break;
		case mtlpp::PixelFormat::ASTC_6x6_sRGB:
			MTLFormat = mtlpp::PixelFormat::ASTC_6x6_LDR;
			break;
		case mtlpp::PixelFormat::ASTC_8x8_sRGB:
			MTLFormat = mtlpp::PixelFormat::ASTC_8x8_LDR;
			break;
		case mtlpp::PixelFormat::ASTC_10x10_sRGB:
			MTLFormat = mtlpp::PixelFormat::ASTC_10x10_LDR;
			break;
		case mtlpp::PixelFormat::ASTC_12x12_sRGB:
			MTLFormat = mtlpp::PixelFormat::ASTC_12x12_LDR;
			break;
#endif //PLATFORM_IOS
		default:
			break;
	}
	
	return MTLFormat;
}

static EPixelFormat MetalToRHIPixelFormat(mtlpp::PixelFormat Format)
{
	Format = FromSRGBFormat(Format);
	for (uint32 i = 0; i < PF_MAX; i++)
	{
		if((mtlpp::PixelFormat)GPixelFormats[i].PlatformFormat == Format)
		{
			return (EPixelFormat)i;
		}
	}
	check(false);
	return PF_MAX;
}

static mtlpp::SizeAndAlign TextureSizeAndAlign(mtlpp::TextureType TextureType, uint32 Width, uint32 Height, uint32 Depth, mtlpp::PixelFormat Format, uint32 MipCount, uint32 SampleCount, uint32 ArrayCount)
{
	mtlpp::SizeAndAlign SizeAlign;
	SizeAlign.Size = 0;
	SizeAlign.Align = 0;
	
	uint32 Align = 0;
	FRHIResourceCreateInfo CreateInfo(TEXT(""));
	switch (TextureType)
	{
		case mtlpp::TextureType::Texture2D:
		case mtlpp::TextureType::Texture2DMultisample:
			SizeAlign.Size = RHICalcTexture2DPlatformSize(Width, Height, MetalToRHIPixelFormat(Format), MipCount, SampleCount, TexCreate_None, CreateInfo, Align);
			SizeAlign.Align = Align;
			break;
		case mtlpp::TextureType::Texture2DArray:
			SizeAlign.Size = RHICalcTexture2DPlatformSize(Width, Height, MetalToRHIPixelFormat(Format), MipCount, SampleCount, TexCreate_None, CreateInfo, Align) * ArrayCount;
			SizeAlign.Align = Align;
			break;
		case mtlpp::TextureType::TextureCube:
			SizeAlign.Size = RHICalcTextureCubePlatformSize(Width, MetalToRHIPixelFormat(Format), MipCount, TexCreate_None, CreateInfo, Align);
			SizeAlign.Align = Align;
			break;
		case mtlpp::TextureType::TextureCubeArray:
			SizeAlign.Size = RHICalcTextureCubePlatformSize(Width, MetalToRHIPixelFormat(Format), MipCount, TexCreate_None, CreateInfo, Align) * ArrayCount;
			SizeAlign.Align = Align;
			break;
		case mtlpp::TextureType::Texture3D:
			SizeAlign.Size = RHICalcTexture3DPlatformSize(Width, Height, Depth, MetalToRHIPixelFormat(Format), MipCount, TexCreate_None, CreateInfo, Align);
			SizeAlign.Align = Align;
			break;
		case mtlpp::TextureType::Texture1D:
		case mtlpp::TextureType::Texture1DArray:
		default:
			check(false);
			break;
	}
	
	return SizeAlign;
}

void AGXLLM::LogAllocTexture(mtlpp::TextureDescriptor const& Desc, mtlpp::Texture const& Texture)
{
	MTLSizeAndAlign SizeAlign;
	if (FAGXCommandQueue::SupportsFeature(EAGXFeaturesGPUCaptureManager))
	{
		SizeAlign = [GMtlDevice heapTextureSizeAndAlignWithDescriptor:Desc.GetPtr()];
	}
	
	void* Ptr = (void*)Texture.GetPtr();
	uint64 Size = static_cast<uint64>(SizeAlign.size);
	
#if PLATFORM_IOS
	bool bMemoryless = (Texture.GetStorageMode() == mtlpp::StorageMode::Memoryless);
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
		
		if (Desc.GetUsage() & mtlpp::TextureUsage::RenderTarget)
		{
			objc_setAssociatedObject(Texture.GetPtr(), (void*)&AGXLLM::LogAllocTexture,
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
			objc_setAssociatedObject(Texture.GetPtr(), (void*)&AGXLLM::LogAllocTexture,
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

void AGXLLM::LogAllocBuffer(mtlpp::Buffer const& Buffer)
{
	void* Ptr = (void*)Buffer.GetPtr();
	uint64 Size = Buffer.GetLength();
	
	INC_MEMORY_STAT_BY(STAT_AGXBufferMemory, Size);
	INC_DWORD_STAT(STAT_AGXBufferCount);
	
	LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, Ptr, Size, ELLMTag::Untagged, ELLMAllocType::System));
	// Assign a dealloc handler to untrack the memory - but don't track the dispatch block!
	{
		LLM_SCOPED_PAUSE_TRACKING(ELLMAllocType::System);
		
		objc_setAssociatedObject(Buffer.GetPtr(), (void*)&AGXLLM::LogAllocBuffer,
		[[[FAGXDeallocHandler alloc] initWithBlock:^{
			LLM_PLATFORM_SCOPE_METAL(ELLMTagAGX::Buffers);
			
			LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, Ptr, ELLMAllocType::System));
			
			DEC_MEMORY_STAT_BY(STAT_AGXBufferMemory, Size);
			DEC_DWORD_STAT(STAT_AGXBufferCount);
		}] autorelease],
		OBJC_ASSOCIATION_RETAIN);
	}
}

void AGXLLM::LogAliasTexture(mtlpp::Texture const& Texture)
{
	objc_setAssociatedObject(Texture.GetPtr(), (void*)&AGXLLM::LogAllocTexture, nil, OBJC_ASSOCIATION_RETAIN);
}

void AGXLLM::LogAliasBuffer(mtlpp::Buffer const& Buffer)
{
	objc_setAssociatedObject(Buffer.GetPtr(), (void*)&AGXLLM::LogAllocBuffer, nil, OBJC_ASSOCIATION_RETAIN);
}



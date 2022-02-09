// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXPipeline.cpp: AGX RHI shader pipeline implementation.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXVertexDeclaration.h"
#include "AGXShaderTypes.h"
#include "AGXGraphicsPipelineState.h"
#include "AGXComputePipelineState.h"
#include "AGXPipeline.h"
#include "MetalShaderResources.h"
#include "AGXProfiler.h"
#include "AGXCommandQueue.h"
#include "RenderUtils.h"
#include "Misc/ScopeRWLock.h"
#include "HAL/PThreadEvent.h"
#include <objc/runtime.h>

static int32 GAGXCacheShaderPipelines = 1;
static FAutoConsoleVariableRef CVarAGXCacheShaderPipelines(
	TEXT("rhi.AGX.CacheShaderPipelines"),
	GAGXCacheShaderPipelines,
	TEXT("When enabled (1, default) cache all graphics pipeline state objects created in AGXRHI for the life of the program, this trades memory for performance as creating PSOs is expensive in Metal.\n")
	TEXT("Disable in the project configuration to allow PSOs to be released to save memory at the expense of reduced performance and increased hitching in-game\n. (On by default (1))"), ECVF_ReadOnly);

static int32 GAGXCacheMinSize = 32;
static FAutoConsoleVariableRef CVarAGXCacheMinSize(
	TEXT("r.ShaderPipelineCache.AGXCacheMinSizeInMB"),
	GAGXCacheMinSize,
	TEXT("Sets the minimum size that we expect the metal OS cache to be (in MB). This is used to determine if we need to cache PSOs again (Default: 32).\n"), ECVF_ReadOnly);

static int32 GAGXBinaryCacheDebugOutput = 0;
static FAutoConsoleVariableRef CVarAGXBinaryCacheDebugOutput(
    TEXT("rhi.AGX.BinaryCacheDebugOutput"),
    GAGXBinaryCacheDebugOutput,
    TEXT("Enable to output logging information for PSO Binary cache default(0) \n"), ECVF_ReadOnly);

static uint32 BlendBitOffsets[] = { Offset_BlendState0, Offset_BlendState1, Offset_BlendState2, Offset_BlendState3, Offset_BlendState4, Offset_BlendState5, Offset_BlendState6, Offset_BlendState7 };
static uint32 RTBitOffsets[] = { Offset_RenderTargetFormat0, Offset_RenderTargetFormat1, Offset_RenderTargetFormat2, Offset_RenderTargetFormat3, Offset_RenderTargetFormat4, Offset_RenderTargetFormat5, Offset_RenderTargetFormat6, Offset_RenderTargetFormat7 };
static_assert(Offset_RasterEnd < 64 && Offset_End < 128, "Offset_RasterEnd must be < 64 && Offset_End < 128");

struct FAGXGraphicsPipelineKey
{
	FAGXRenderPipelineHash RenderPipelineHash;
	FAGXHashedVertexDescriptor VertexDescriptorHash;
	FSHAHash VertexFunction;
	FSHAHash PixelFunction;

	template<typename Type>
	inline void SetHashValue(uint32 Offset, uint32 NumBits, Type Value)
	{
		if (Offset < Offset_RasterEnd)
		{
			uint64 BitMask = ((((uint64)1ULL) << NumBits) - 1) << Offset;
			RenderPipelineHash.RasterBits = (RenderPipelineHash.RasterBits & ~BitMask) | (((uint64)Value << Offset) & BitMask);
		}
		else
		{
			Offset -= Offset_RenderTargetFormat0;
			uint64 BitMask = ((((uint64)1ULL) << NumBits) - 1) << Offset;
			RenderPipelineHash.TargetBits = (RenderPipelineHash.TargetBits & ~BitMask) | (((uint64)Value << Offset) & BitMask);
		}
	}

	bool operator==(FAGXGraphicsPipelineKey const& Other) const
	{
		return (RenderPipelineHash == Other.RenderPipelineHash
		&& VertexDescriptorHash == Other.VertexDescriptorHash
		&& VertexFunction == Other.VertexFunction
		&& PixelFunction == Other.PixelFunction);
	}
	
	friend uint32 GetTypeHash(FAGXGraphicsPipelineKey const& Key)
	{
		uint32 H = FCrc::MemCrc32(&Key.RenderPipelineHash, sizeof(Key.RenderPipelineHash), GetTypeHash(Key.VertexDescriptorHash));
		H = FCrc::MemCrc32(Key.VertexFunction.Hash, sizeof(Key.VertexFunction.Hash), H);
		H = FCrc::MemCrc32(Key.PixelFunction.Hash, sizeof(Key.PixelFunction.Hash), H);
		return H;
	}
	
	friend void InitMetalGraphicsPipelineKey(FAGXGraphicsPipelineKey& Key, const FGraphicsPipelineStateInitializer& Init)
	{
		uint32 const NumActiveTargets = Init.ComputeNumValidRenderTargets();
		check(NumActiveTargets <= MaxSimultaneousRenderTargets);
	
		FAGXBlendState* BlendState = (FAGXBlendState*)Init.BlendState;
		
		FMemory::Memzero(Key.RenderPipelineHash);
		
		bool bHasActiveTargets = false;
		for (uint32 i = 0; i < NumActiveTargets; i++)
		{
			EPixelFormat TargetFormat = (EPixelFormat)Init.RenderTargetFormats[i];
			if (TargetFormat == PF_Unknown) { continue; }

			mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[TargetFormat].PlatformFormat;
			ETextureCreateFlags Flags = Init.RenderTargetFlags[i];
			if (EnumHasAnyFlags(Flags, TexCreate_SRGB))
			{
#if PLATFORM_MAC // Expand as R8_sRGB is iOS only.
				if (MetalFormat == mtlpp::PixelFormat::R8Unorm)
				{
					MetalFormat = mtlpp::PixelFormat::RGBA8Unorm;
				}
#endif
				MetalFormat = AGXToSRGBFormat(MetalFormat);
			}
			
			uint8 FormatKey = AGXGetMetalPixelFormatKey(MetalFormat);;
			Key.SetHashValue(RTBitOffsets[i], NumBits_RenderTargetFormat, FormatKey);
			Key.SetHashValue(BlendBitOffsets[i], NumBits_BlendState, BlendState->RenderTargetStates[i].BlendStateKey);
			
			bHasActiveTargets |= true;
		}
		
		uint8 DepthFormatKey = 0;
		uint8 StencilFormatKey = 0;
		switch(Init.DepthStencilTargetFormat)
		{
			case PF_DepthStencil:
			{
				mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat;
				if (Init.DepthTargetLoadAction != ERenderTargetLoadAction::ENoAction || Init.DepthTargetStoreAction != ERenderTargetStoreAction::ENoAction)
				{
					DepthFormatKey = AGXGetMetalPixelFormatKey(MetalFormat);
				}
				if (Init.StencilTargetLoadAction != ERenderTargetLoadAction::ENoAction || Init.StencilTargetStoreAction != ERenderTargetStoreAction::ENoAction)
				{
					StencilFormatKey = AGXGetMetalPixelFormatKey(mtlpp::PixelFormat::Stencil8);
				}
				bHasActiveTargets |= true;
				break;
			}
			case PF_ShadowDepth:
			{
				DepthFormatKey = AGXGetMetalPixelFormatKey((mtlpp::PixelFormat)GPixelFormats[PF_ShadowDepth].PlatformFormat);
				bHasActiveTargets |= true;
				break;
			}
			default:
			{
				break;
			}
		}
		
		// If the pixel shader writes depth then we must compile with depth access, so we may bind the dummy depth.
		// If the pixel shader writes to UAVs but not target is bound we must also bind the dummy depth.
		FAGXPixelShader* PixelShader = (FAGXPixelShader*)Init.BoundShaderState.PixelShaderRHI;
		if ( PixelShader && ( ( PixelShader->Bindings.InOutMask.IsFieldEnabled(CrossCompiler::FShaderBindingInOutMask::DepthStencilMaskIndex) && DepthFormatKey == 0 ) || (bHasActiveTargets == false && PixelShader->Bindings.NumUAVs > 0) ) )
		{
			mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat;
			DepthFormatKey = AGXGetMetalPixelFormatKey(MetalFormat);
		}
		
		Key.SetHashValue(Offset_DepthFormat, NumBits_DepthFormat, DepthFormatKey);
		Key.SetHashValue(Offset_StencilFormat, NumBits_StencilFormat, StencilFormatKey);

		Key.SetHashValue(Offset_SampleCount, NumBits_SampleCount, Init.NumSamples);

		Key.SetHashValue(Offset_AlphaToCoverage, NumBits_AlphaToCoverage, Init.NumSamples > 1 && BlendState->bUseAlphaToCoverage ? 1 : 0);
		
#if PLATFORM_MAC
		Key.SetHashValue(Offset_PrimitiveTopology, NumBits_PrimitiveTopology, AGXTranslatePrimitiveTopology(Init.PrimitiveType));
#endif

		FAGXVertexDeclaration* VertexDecl = (FAGXVertexDeclaration*)Init.BoundShaderState.VertexDeclarationRHI;
		Key.VertexDescriptorHash = VertexDecl->Layout;
		
		FAGXVertexShader* VertexShader = (FAGXVertexShader*)Init.BoundShaderState.VertexShaderRHI;
		Key.VertexFunction = VertexShader->GetHash();

		if (PixelShader)
		{
			Key.PixelFunction = PixelShader->GetHash();
		}
	}
};

static FAGXShaderPipeline* CreateMTLRenderPipeline(bool const bSync, FAGXGraphicsPipelineKey const& Key, const FGraphicsPipelineStateInitializer& Init);

class FAGXShaderPipelineCache
{
public:
	static FAGXShaderPipelineCache& Get()
	{
		static FAGXShaderPipelineCache sSelf;
		return sSelf;
	}
	
	FAGXShaderPipeline* GetRenderPipeline(bool const bSync, FAGXGraphicsPipelineState const* State, const FGraphicsPipelineStateInitializer& Init)
	{
		SCOPE_CYCLE_COUNTER(STAT_AGXPipelineStateTime);
		
		FAGXGraphicsPipelineKey Key;
		InitMetalGraphicsPipelineKey(Key, Init);
		
		// By default there'll be more threads trying to read this than to write it.
		PipelineMutex.ReadLock();

		// Try to find the entry in the cache.
		FAGXShaderPipeline* Desc = Pipelines.FindRef(Key);

		PipelineMutex.ReadUnlock();

		if (Desc == nil)
		{

			// By default there'll be more threads trying to read this than to write it.
			EventsMutex.ReadLock();

			// Try to find a pipeline creation event for this key. If it's found, we already have a thread creating this pipeline and we just have to wait.
			TSharedPtr<FPThreadEvent, ESPMode::ThreadSafe> Event = PipelineEvents.FindRef(Key);

			EventsMutex.ReadUnlock();

			bool bCompile = false;
			if (!Event.IsValid())
			{
				// Create an event other threads can use to wait if they request the same pipeline this thread is creating
				EventsMutex.WriteLock();

				Event = PipelineEvents.FindRef(Key);
				if (!Event.IsValid())
				{
					Event = PipelineEvents.Add(Key, MakeShareable(new FPThreadEvent()));
					Event->Create(true);
					bCompile = true;
				}
				check(Event.IsValid());

				EventsMutex.WriteUnlock();
			}

			if (bCompile)
			{
				Desc = CreateMTLRenderPipeline(bSync, Key, Init);

				if (Desc != nil)
				{
					PipelineMutex.WriteLock();

					Pipelines.Add(Key, Desc);
					ReverseLookup.Add(Desc, Key);

					PipelineMutex.WriteUnlock();

					if (GAGXCacheShaderPipelines == 0)
					{
						// When we aren't caching for program lifetime we autorelease so that the PSO is released to the OS once all RHI references are released.
						[Desc autorelease];
					}
				}

				EventsMutex.WriteLock();

				Event->Trigger();
				PipelineEvents.Remove(Key);

				EventsMutex.WriteUnlock();
			}
			else
			{
				check(Event.IsValid());
				Event->Wait();

				PipelineMutex.ReadLock();
				Desc = Pipelines.FindRef(Key);
				PipelineMutex.ReadUnlock();
				check(Desc);
			}
		}
		
		return Desc;
	}
	
	void ReleaseRenderPipeline(FAGXShaderPipeline* Pipeline)
	{
		if (GAGXCacheShaderPipelines)
		{
			[Pipeline release];
		}
		else
		{
			// We take a mutex here to prevent anyone from acquiring a reference to the state which might just be about to return memory to the OS.
			FRWScopeLock Lock(PipelineMutex, SLT_Write);
			[Pipeline release];
		}
	}
	
	void RemoveRenderPipeline(FAGXShaderPipeline* Pipeline)
	{
		check (GAGXCacheShaderPipelines == 0);
		{
			FAGXGraphicsPipelineKey* Desc = ReverseLookup.Find(Pipeline);
			if (Desc)
			{
				Pipelines.Remove(*Desc);
				ReverseLookup.Remove(Pipeline);
			}
		}
	}
	
	
private:
	FRWLock PipelineMutex;
	FRWLock EventsMutex;
	TMap<FAGXGraphicsPipelineKey, FAGXShaderPipeline*> Pipelines;
	TMap<FAGXShaderPipeline*, FAGXGraphicsPipelineKey> ReverseLookup;
	TMap<FAGXGraphicsPipelineKey, TSharedPtr<FPThreadEvent, ESPMode::ThreadSafe>> PipelineEvents;
};

@implementation FAGXShaderPipeline

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FAGXShaderPipeline)

- (void)dealloc
{
	// For render pipeline states we might need to remove the PSO from the cache when we aren't caching them for program lifetime
	if (GAGXCacheShaderPipelines == 0 && RenderPipelineState)
	{
		FAGXShaderPipelineCache::Get().RemoveRenderPipeline(self);
	}
	[super dealloc];
}

- (instancetype)init
{
	id Self = [super init];
	if (Self)
	{
		RenderPipelineReflection = mtlpp::RenderPipelineReflection(nil);
		ComputePipelineReflection = mtlpp::ComputePipelineReflection(nil);
#if METAL_DEBUG_OPTIONS
		RenderDesc = mtlpp::RenderPipelineDescriptor(nil);
		ComputeDesc = mtlpp::ComputePipelineDescriptor(nil);
#endif
	}
	return Self;
}

- (void)initResourceMask
{
	if (RenderPipelineReflection)
	{
		[self initResourceMask:SF_Vertex];
		[self initResourceMask:SF_Pixel];
		
		if (AGXSafeGetRuntimeDebuggingLevel() < EAGXDebugLevelValidation)
		{
			RenderPipelineReflection = mtlpp::RenderPipelineReflection(nil);
		}
	}
	if (ComputePipelineReflection)
	{
		[self initResourceMask:SF_Compute];
		
		if (AGXSafeGetRuntimeDebuggingLevel() < EAGXDebugLevelValidation)
		{
			ComputePipelineReflection = mtlpp::ComputePipelineReflection(nil);
		}
	}
}
- (void)initResourceMask:(EShaderFrequency)Frequency
{
	NSArray<MTLArgument*>* Arguments = nil;
	switch(Frequency)
	{
		case SF_Vertex:
		{
			MTLRenderPipelineReflection* Reflection = RenderPipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.vertexArguments;
			break;
		}
		case SF_Pixel:
		{
			MTLRenderPipelineReflection* Reflection = RenderPipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.fragmentArguments;
			break;
		}
		case SF_Compute:
		{
			MTLComputePipelineReflection* Reflection = ComputePipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.arguments;
			break;
		}
		default:
			check(false);
			break;
	}
	
	for (uint32 i = 0; i < Arguments.count; i++)
	{
		MTLArgument* Arg = [Arguments objectAtIndex:i];
		check(Arg);

		if (!Arg.active)
		{
			continue;
		}

		switch(Arg.type)
		{
			case MTLArgumentTypeBuffer:
			{
				checkf(Arg.index < ML_MaxBuffers, TEXT("Metal buffer index exceeded!"));
				if (FString(Arg.name) != TEXT("BufferSizes") && FString(Arg.name) != TEXT("spvBufferSizeConstants"))
				{
					ResourceMask[Frequency].BufferMask |= (1 << Arg.index);
				
					if(BufferDataSizes[Frequency].Num() < 31)
						BufferDataSizes[Frequency].SetNumZeroed(31);
				
					BufferDataSizes[Frequency][Arg.index] = Arg.bufferDataSize;
				}
				break;
			}
			case MTLArgumentTypeThreadgroupMemory:
			{
				break;
			}
			case MTLArgumentTypeTexture:
			{
				checkf(Arg.index < ML_MaxTextures, TEXT("Metal texture index exceeded!"));
				ResourceMask[Frequency].TextureMask |= (FAGXTextureMask(1) << Arg.index);
				TextureTypes[Frequency].Add(Arg.index, (uint8)Arg.textureType);
				break;
			}
			case MTLArgumentTypeSampler:
			{
				checkf(Arg.index < ML_MaxSamplers, TEXT("Metal sampler index exceeded!"));
				ResourceMask[Frequency].SamplerMask |= (1 << Arg.index);
				break;
			}
			default:
				check(false);
				break;
		}
	}
}
@end

static MTLVertexDescriptor* GetMaskedVertexDescriptor(MTLVertexDescriptor* InputDesc, const CrossCompiler::FShaderBindingInOutMask& InOutMask)
{
	for (uint32 Attr = 0; Attr < MaxMetalStreams; Attr++)
	{
		if (!InOutMask.IsFieldEnabled((int32)Attr) && [InputDesc.attributes objectAtIndexedSubscript:Attr] != nil)
		{
			MTLVertexDescriptor* Desc = [[InputDesc copy] autorelease];
			CrossCompiler::FShaderBindingInOutMask BuffersUsed;
			for (int32 MetalStreamIndex = 0; MetalStreamIndex < MaxMetalStreams; ++MetalStreamIndex)
			{
				if (!InOutMask.IsFieldEnabled(MetalStreamIndex))
				{
					[Desc.attributes setObject:nil atIndexedSubscript:MetalStreamIndex];
				}
				else
				{
					BuffersUsed.EnableField([Desc.attributes objectAtIndexedSubscript : MetalStreamIndex].bufferIndex);
				}
			}
			for (int32 BufferIndex = 0; BufferIndex < ML_MaxBuffers; ++BufferIndex)
			{
				if (!BuffersUsed.IsFieldEnabled(BufferIndex))
				{
					[Desc.layouts setObject:nil atIndexedSubscript:BufferIndex];
				}
			}
			return Desc;
		}
	}
	
	return InputDesc;
}

static bool ConfigureRenderPipelineDescriptor(mtlpp::RenderPipelineDescriptor& RenderPipelineDesc,
#if PLATFORM_MAC
											  mtlpp::RenderPipelineDescriptor& DebugPipelineDesc,
#elif !PLATFORM_TVOS
											  mtlpp::TileRenderPipelineDescriptor& DebugPipelineDesc,
#endif
											  FAGXGraphicsPipelineKey const& Key,
											  const FGraphicsPipelineStateInitializer& Init)
{
	FAGXPixelShader* PixelShader = (FAGXPixelShader*)Init.BoundShaderState.PixelShaderRHI;
	uint32 const NumActiveTargets = Init.ComputeNumValidRenderTargets();
	check(NumActiveTargets <= MaxSimultaneousRenderTargets);
	if (PixelShader)
	{
		if (PixelShader->Bindings.InOutMask.Bitmask == 0 && PixelShader->Bindings.NumUAVs == 0 && PixelShader->Bindings.bDiscards == false)
		{
			UE_LOG(LogAGX, Error, TEXT("Pixel shader has no outputs which is not permitted. No Discards, In-Out Mask: %x\nNumber UAVs: %d\nSource Code:\n%s"), PixelShader->Bindings.InOutMask.Bitmask, PixelShader->Bindings.NumUAVs, *FString(PixelShader->GetSourceCode()));
			return false;
		}
		
		const uint32 MaxNumActiveTargets = __builtin_popcount(PixelShader->Bindings.InOutMask.Bitmask & ((1u << CrossCompiler::FShaderBindingInOutMask::MaxIndex) - 1));
		UE_CLOG((NumActiveTargets < MaxNumActiveTargets), LogAGX, Verbose, TEXT("NumActiveTargets doesn't match pipeline's pixel shader output mask: %u, %hx"), NumActiveTargets, PixelShader->Bindings.InOutMask.Bitmask);
	}
	
	FAGXBlendState* BlendState = (FAGXBlendState*)Init.BlendState;
	
	ns::Array<mtlpp::RenderPipelineColorAttachmentDescriptor> ColorAttachments = RenderPipelineDesc.GetColorAttachments();
#if !PLATFORM_TVOS
	auto DebugColorAttachements = DebugPipelineDesc.GetColorAttachments();
#endif
	
	uint32 TargetWidth = 0;
	for (uint32 ActiveTargetIndex = 0; ActiveTargetIndex < NumActiveTargets; ActiveTargetIndex++)
	{
		EPixelFormat TargetFormat = (EPixelFormat)Init.RenderTargetFormats[ActiveTargetIndex];
		
		const bool bIsActiveTargetBound = (PixelShader && PixelShader->Bindings.InOutMask.IsFieldEnabled(ActiveTargetIndex));
		METAL_FATAL_ASSERT(!(TargetFormat == PF_Unknown && bIsActiveTargetBound), TEXT("Pipeline pixel shader expects target %u to be bound but it isn't: %s."), ActiveTargetIndex, *FString(PixelShader->GetSourceCode()));
		
		TargetWidth += GPixelFormats[TargetFormat].BlockBytes;
		
		mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[TargetFormat].PlatformFormat;
		ETextureCreateFlags Flags = (ETextureCreateFlags)Init.RenderTargetFlags[ActiveTargetIndex];
		if (EnumHasAnyFlags(Flags, TexCreate_SRGB))
		{
#if PLATFORM_MAC // Expand as R8_sRGB is iOS only.
			if (MetalFormat == mtlpp::PixelFormat::R8Unorm)
			{
				MetalFormat = mtlpp::PixelFormat::RGBA8Unorm;
			}
#endif
			MetalFormat = AGXToSRGBFormat(MetalFormat);
		}
		
		mtlpp::RenderPipelineColorAttachmentDescriptor Attachment = ColorAttachments[ActiveTargetIndex];
		Attachment.SetPixelFormat(MetalFormat);
		
#if !PLATFORM_TVOS
		auto DebugAttachment = DebugColorAttachements[ActiveTargetIndex];
		DebugAttachment.SetPixelFormat(MetalFormat);
#endif
		
		mtlpp::RenderPipelineColorAttachmentDescriptor Blend = BlendState->RenderTargetStates[ActiveTargetIndex].BlendState;
		if(TargetFormat != PF_Unknown)
		{
			// assign each property manually, would be nice if this was faster
			Attachment.SetBlendingEnabled(Blend.IsBlendingEnabled());
			Attachment.SetSourceRgbBlendFactor(Blend.GetSourceRgbBlendFactor());
			Attachment.SetDestinationRgbBlendFactor(Blend.GetDestinationRgbBlendFactor());
			Attachment.SetRgbBlendOperation(Blend.GetRgbBlendOperation());
			Attachment.SetSourceAlphaBlendFactor(Blend.GetSourceAlphaBlendFactor());
			Attachment.SetDestinationAlphaBlendFactor(Blend.GetDestinationAlphaBlendFactor());
			Attachment.SetAlphaBlendOperation(Blend.GetAlphaBlendOperation());
			Attachment.SetWriteMask(Blend.GetWriteMask());
			
#if PLATFORM_MAC
			DebugAttachment.SetBlendingEnabled(Blend.IsBlendingEnabled());
			DebugAttachment.SetSourceRgbBlendFactor(Blend.GetSourceRgbBlendFactor());
			DebugAttachment.SetDestinationRgbBlendFactor(Blend.GetDestinationRgbBlendFactor());
			DebugAttachment.SetRgbBlendOperation(Blend.GetRgbBlendOperation());
			DebugAttachment.SetSourceAlphaBlendFactor(Blend.GetSourceAlphaBlendFactor());
			DebugAttachment.SetDestinationAlphaBlendFactor(Blend.GetDestinationAlphaBlendFactor());
			DebugAttachment.SetAlphaBlendOperation(Blend.GetAlphaBlendOperation());
			DebugAttachment.SetWriteMask(Blend.GetWriteMask());
#endif
		}
		else
		{
			Attachment.SetBlendingEnabled(NO);
			Attachment.SetWriteMask(mtlpp::ColorWriteMask::None);
#if PLATFORM_MAC
			DebugAttachment.SetBlendingEnabled(NO);
			DebugAttachment.SetWriteMask(mtlpp::ColorWriteMask::None);
#endif
		}
	}
	
	// don't allow a PSO that is too wide
	if (!GSupportsWideMRT && TargetWidth > 16)
	{
		return false;
	}
	
	switch(Init.DepthStencilTargetFormat)
	{
		case PF_DepthStencil:
		{
			mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat;
			if(MetalFormat == mtlpp::PixelFormat::Depth32Float)
			{
				if (Init.DepthTargetLoadAction != ERenderTargetLoadAction::ENoAction || Init.DepthTargetStoreAction != ERenderTargetStoreAction::ENoAction)
				{
					RenderPipelineDesc.SetDepthAttachmentPixelFormat(MetalFormat);
#if PLATFORM_MAC
					DebugPipelineDesc.SetDepthAttachmentPixelFormat(MetalFormat);
#endif
				}
				if (Init.StencilTargetLoadAction != ERenderTargetLoadAction::ENoAction || Init.StencilTargetStoreAction != ERenderTargetStoreAction::ENoAction)
				{
					RenderPipelineDesc.SetStencilAttachmentPixelFormat(mtlpp::PixelFormat::Stencil8);
#if PLATFORM_MAC
					DebugPipelineDesc.SetStencilAttachmentPixelFormat(mtlpp::PixelFormat::Stencil8);
#endif
				}
			}
			else
			{
				RenderPipelineDesc.SetDepthAttachmentPixelFormat(MetalFormat);
				RenderPipelineDesc.SetStencilAttachmentPixelFormat(MetalFormat);
#if PLATFORM_MAC
				DebugPipelineDesc.SetDepthAttachmentPixelFormat(MetalFormat);
				DebugPipelineDesc.SetStencilAttachmentPixelFormat(MetalFormat);
#endif
			}
			break;
		}
		case PF_ShadowDepth:
		{
			RenderPipelineDesc.SetDepthAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_ShadowDepth].PlatformFormat);
#if PLATFORM_MAC
			DebugPipelineDesc.SetDepthAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_ShadowDepth].PlatformFormat);
#endif
			break;
		}
		default:
		{
			break;
		}
	}
	
	check(Init.BoundShaderState.VertexShaderRHI != nullptr);
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	check(Init.BoundShaderState.GetGeometryShader() == nullptr);
#endif
	
	if( RenderPipelineDesc.GetDepthAttachmentPixelFormat() == mtlpp::PixelFormat::Invalid &&
		PixelShader && ( PixelShader->Bindings.InOutMask.IsFieldEnabled(CrossCompiler::FShaderBindingInOutMask::DepthStencilMaskIndex) || ( NumActiveTargets == 0 && PixelShader->Bindings.NumUAVs > 0) ) )
	{
		RenderPipelineDesc.SetDepthAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
		RenderPipelineDesc.SetStencilAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
		
#if PLATFORM_MAC
		DebugPipelineDesc.SetDepthAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
		DebugPipelineDesc.SetStencilAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
#endif
	}
	
	static bool bNoMSAA = FParse::Param(FCommandLine::Get(), TEXT("nomsaa"));
	uint16 NumSamples = !bNoMSAA ? FMath::Max(Init.NumSamples, (uint16)1u) : (uint16)1u;
	RenderPipelineDesc.SetSampleCount(NumSamples);
	RenderPipelineDesc.SetAlphaToCoverageEnabled(NumSamples > 1 && BlendState->bUseAlphaToCoverage);
#if PLATFORM_MAC
	RenderPipelineDesc.SetInputPrimitiveTopology(AGXTranslatePrimitiveTopology(Init.PrimitiveType));
	DebugPipelineDesc.SetSampleCount(!bNoMSAA ? FMath::Max(Init.NumSamples, (uint16)1u) : (uint16)1u);
	DebugPipelineDesc.SetInputPrimitiveTopology(mtlpp::PrimitiveTopologyClass::Point);
#endif
	
	if (FAGXCommandQueue::SupportsFeature(EAGXFeaturesPipelineBufferMutability))
	{
		FAGXVertexShader* VertexShader = (FAGXVertexShader*)Init.BoundShaderState.VertexShaderRHI;
		
		ns::AutoReleased<ns::Array<mtlpp::PipelineBufferDescriptor>> VertexPipelineBuffers = RenderPipelineDesc.GetVertexBuffers();
		FMetalShaderBindings& VertexBindings = VertexShader->Bindings;
		int8 VertexSideTable = VertexShader->SideTableBinding;
		{
			uint32 ImmutableBuffers = VertexBindings.ConstantBuffers;
			while(ImmutableBuffers)
			{
				uint32 Index = __builtin_ctz(ImmutableBuffers);
				ImmutableBuffers &= ~(1 << Index);
				
				if (Index < ML_MaxBuffers)
				{
					ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = VertexPipelineBuffers[Index];
					PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
				}
			}
			if (VertexSideTable > 0)
			{
				ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = VertexPipelineBuffers[VertexSideTable];
				PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
			}
		}
		
		if (PixelShader)
		{
			ns::AutoReleased<ns::Array<mtlpp::PipelineBufferDescriptor>> FragmentPipelineBuffers = RenderPipelineDesc.GetFragmentBuffers();
			uint32 ImmutableBuffers = PixelShader->Bindings.ConstantBuffers;
			while(ImmutableBuffers)
			{
				uint32 Index = __builtin_ctz(ImmutableBuffers);
				ImmutableBuffers &= ~(1 << Index);
				
				if (Index < ML_MaxBuffers)
				{
					ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = FragmentPipelineBuffers[Index];
					PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
				}
			}
			if (PixelShader->SideTableBinding > 0)
			{
				ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = FragmentPipelineBuffers[PixelShader->SideTableBinding];
				PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
			}
		}
	}
	
	return true;
}



/*
* PSO Harvesting and Reuse
*
* Usage:
*
* To Harvest, run the game with -AGXPSOCache=recreate
* All Render and Compute PSOs created will be harvested into the MTLBinaryArchive
* Console command r.Agx.ShaderPipelineCache.Save will trigger the serialization to file.
* The binary archive's location will be printed to the log.
*
* To reuse, run the game with -AGXPSOCache=use
* The binary archive will be opened from the saved location.
* The binary archive can be moved to another device, as long as it's the same GPU
* and OS build.
*
*/

enum class CacheMode
{
	Uninitialized,
	Recreate,
	Append,
	Use,
	Ignore
};

static CacheMode GPSOCacheMode = CacheMode::Uninitialized;
static id< MTLBinaryArchive > GPSOBinaryArchive = nil;
static uint32_t GPSOHarvestCount = 0;

static NSURL * PipelineCacheSaveLocation()
{
	NSString * path = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES)[0];
	if (!path)
	{
		UE_LOG(LogAGX, Error, TEXT("AGX Pipeline Cache: Unable to find Documents folder"));
	}
	NSURL * url = [NSURL fileURLWithPath : [NSString stringWithFormat : @"%@/mtlarchive.bin", path] ];
	return url;
}

static void InitializeAGXPipelineCache()
{
	FString strCacheMode;
	FParse::Value(FCommandLine::Get(), TEXT("AGXPSOCache="), strCacheMode);

	if (strCacheMode.Compare(TEXT("recreate"), ESearchCase::IgnoreCase) == 0)
	{
		GPSOCacheMode = CacheMode::Recreate;
		UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: recreate PSO cache"));
	}
	else if (strCacheMode.Compare(TEXT("append"), ESearchCase::IgnoreCase) == 0)
	{
		GPSOCacheMode = CacheMode::Append;
		UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: append to PSO cache"));
	}
	else if (strCacheMode.Compare(TEXT("use"), ESearchCase::IgnoreCase) == 0)
	{
		GPSOCacheMode = CacheMode::Use;
		UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: use PSO cache"));
	}
	else
	{
		GPSOCacheMode = CacheMode::Ignore;
		UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: ignore PSO cache"));
	}

	if (GPSOCacheMode != CacheMode::Ignore)
	{
		NSURL* url = PipelineCacheSaveLocation();
		UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: pso cache save location will be: %s"), *FString(url.absoluteString.UTF8String));
		MTLBinaryArchiveDescriptor* archDesc = [MTLBinaryArchiveDescriptor new];
		archDesc.url = ((GPSOCacheMode == CacheMode::Append) || (GPSOCacheMode == CacheMode::Use)) ? PipelineCacheSaveLocation() : nil;
		id< MTLDevice > mtlDevice = GMtlDevice;
		__autoreleasing NSError* err = nil;
		GPSOBinaryArchive = [mtlDevice newBinaryArchiveWithDescriptor : archDesc error : &err];
		if (err)
		{
			UE_LOG(LogAGX, Error, TEXT("Error adding Pipeline Functions to Binary Archive: %s"), *FString(err.localizedDescription.UTF8String));
		}
	}
}

static void RelatePipelineStateToCache(const mtlpp::RenderPipelineDescriptor & PipelineDesc, NSUInteger * outPipelineOpts)
{
	if (GPSOBinaryArchive && (GPSOCacheMode != CacheMode::Ignore))
	{
		if (GPSOCacheMode == CacheMode::Recreate || GPSOCacheMode == CacheMode::Append)
		{
			__autoreleasing NSError * err = nil;
			bool bAddedBinaryPSO = [GPSOBinaryArchive addRenderPipelineFunctionsWithDescriptor : PipelineDesc.GetPtr() error : &err];
			if (err)
			{
				UE_LOG(LogAGX, Warning, TEXT("AGX Pipeline Cache: Error adding Pipeline Functions to Binary Archive: %s"), *FString(err.localizedDescription.UTF8String));
			}
            else if(bAddedBinaryPSO)
            {
                GPSOHarvestCount++;
                if(GAGXBinaryCacheDebugOutput && GPSOHarvestCount % 100)
                {
                    UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: Harvested PSO count: %d"), GPSOHarvestCount);
                }
            }
		}
	}
}

static void RelatePipelineStateToCache(const mtlpp::ComputePipelineDescriptor & PipelineDesc, NSUInteger * outPipelineOpts)
{
	if (GPSOBinaryArchive && (GPSOCacheMode != CacheMode::Ignore))
	{
		if (GPSOCacheMode == CacheMode::Recreate || GPSOCacheMode == CacheMode::Append)
		{
			__autoreleasing NSError* err = nil;
            bool bAddedBinaryPSO = [GPSOBinaryArchive addComputePipelineFunctionsWithDescriptor : PipelineDesc.GetPtr() error : &err] ;
			if (err)
			{
				UE_LOG(LogAGX, Warning, TEXT("AGX Pipeline Cache: Error adding Pipeline Functions to Binary Archive: %s"), *FString(err.localizedDescription.UTF8String));
			}
            else if(bAddedBinaryPSO)
            {
                GPSOHarvestCount++;
                if(GAGXBinaryCacheDebugOutput && GPSOHarvestCount % 100)
                {
                    UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: Harvested PSO count: %d"), GPSOHarvestCount);
                }
            }
		}
	}
}

void AGXConsoleCommandSavePipelineFileCache()
{
	UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: requesting PSO save..."));

	if (GPSOBinaryArchive && (GPSOCacheMode == CacheMode::Recreate || GPSOCacheMode == CacheMode::Append))
	{
		NSURL * url = PipelineCacheSaveLocation();
		UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: Serialize harvested PSOs to: %s"), *FString(url.absoluteString.UTF8String));
        UE_LOG(LogAGX, Log, TEXT("AGX Pipeline Cache: Serialized PSO Count: %d"), GPSOHarvestCount);
        
        __autoreleasing NSError * err = nil;
		[GPSOBinaryArchive serializeToURL : url error : &err];
				
		if (err)
		{
			UE_LOG(LogAGX, Error, TEXT("AGX Pipeline Cache: Error Serializing binary archive: %s"), *FString(err.localizedDescription.UTF8String));
		}
	}
}

static FAutoConsoleCommand SavePipelineCacheCmd(
        TEXT("rhi.Agx.ShaderPipelineCache.Save"),
        TEXT("Save the current pipeline file cache."),
        FConsoleCommandDelegate::CreateStatic(AGXConsoleCommandSavePipelineFileCache));

static FAGXShaderPipeline* CreateMTLRenderPipeline(bool const bSync, FAGXGraphicsPipelineKey const& Key, const FGraphicsPipelineStateInitializer& Init)
{
	if (GPSOCacheMode == CacheMode::Uninitialized)
	{
		InitializeAGXPipelineCache();
	}

    FAGXVertexShader* VertexShader = (FAGXVertexShader*)Init.BoundShaderState.VertexShaderRHI;
    FAGXPixelShader* PixelShader = (FAGXPixelShader*)Init.BoundShaderState.PixelShaderRHI;
    
    mtlpp::Function vertexFunction = VertexShader->GetFunction();
    mtlpp::Function fragmentFunction = PixelShader ? PixelShader->GetFunction() : nil;

    FAGXShaderPipeline* Pipeline = nil;
    if (vertexFunction && ((PixelShader != nullptr) == (fragmentFunction != nil)))
    {
		ns::Error Error;

		uint32 const NumActiveTargets = Init.ComputeNumValidRenderTargets();
        check(NumActiveTargets <= MaxSimultaneousRenderTargets);
		
		Pipeline = [FAGXShaderPipeline new];
		METAL_DEBUG_OPTION(FMemory::Memzero(Pipeline->ResourceMask, sizeof(Pipeline->ResourceMask)));

		mtlpp::RenderPipelineDescriptor RenderPipelineDesc;
        mtlpp::ComputePipelineDescriptor ComputePipelineDesc(nil);
#if PLATFORM_MAC
        mtlpp::RenderPipelineDescriptor DebugPipelineDesc;
#elif !PLATFORM_TVOS
        mtlpp::TileRenderPipelineDescriptor DebugPipelineDesc;
#endif
		
#if PLATFORM_TVOS
		if (!ConfigureRenderPipelineDescriptor(RenderPipelineDesc, Key, Init))
#else
		if (!ConfigureRenderPipelineDescriptor(RenderPipelineDesc, DebugPipelineDesc, Key, Init))
#endif
		{
			return nil;
		}
        
        FAGXVertexDeclaration* VertexDecl = (FAGXVertexDeclaration*)Init.BoundShaderState.VertexDeclarationRHI;
		
		RenderPipelineDesc.SetVertexDescriptor(GetMaskedVertexDescriptor(VertexDecl->Layout.VertexDesc, VertexShader->Bindings.InOutMask));
		RenderPipelineDesc.SetVertexFunction(vertexFunction);
		RenderPipelineDesc.SetFragmentFunction(fragmentFunction);
#if ENABLE_METAL_GPUPROFILE
		ns::String VertexName = vertexFunction.GetName();
		ns::String FragmentName = fragmentFunction ? fragmentFunction.GetName() : @"";
		RenderPipelineDesc.SetLabel([NSString stringWithFormat:@"%@+%@", VertexName.GetPtr(), FragmentName.GetPtr()]);
#endif

		NSUInteger RenderOption = mtlpp::PipelineOption::NoPipelineOption;
		mtlpp::AutoReleasedRenderPipelineReflection* Reflection = nullptr;
		mtlpp::AutoReleasedRenderPipelineReflection OutReflection;
		Reflection = &OutReflection;
		if (GetAGXDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelFastValidation)
		{
			RenderOption = mtlpp::PipelineOption::ArgumentInfo | mtlpp::PipelineOption::BufferTypeInfo;
		}

		{
			ns::AutoReleasedError RenderError;
			METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("NewRenderPipeline: %s"), TEXT("")/**FString([RenderPipelineDesc.GetPtr() description])*/)));
			RelatePipelineStateToCache(RenderPipelineDesc, &RenderOption);
			Pipeline->RenderPipelineState = GMtlppDevice.NewRenderPipelineState(RenderPipelineDesc, (mtlpp::PipelineOption)RenderOption, Reflection, &RenderError);
			if (Reflection)
			{
				Pipeline->RenderPipelineReflection = *Reflection;
#if METAL_DEBUG_OPTIONS
				Pipeline->RenderDesc = RenderPipelineDesc;
#endif
			}
			Error = RenderError;
		}
		
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogAGX, Error, TEXT("Failed to generate a pipeline state object: %s"), *FString(Error.GetPtr().description));
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogAGX, Error, TEXT("Vertex shader: %s"), *FString(VertexShader->GetSourceCode()));
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogAGX, Error, TEXT("Pixel shader: %s"), PixelShader ? *FString(PixelShader->GetSourceCode()) : TEXT("NULL"));
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogAGX, Error, TEXT("Descriptor: %s"), *FString(RenderPipelineDesc.GetPtr().description));
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogAGX, Error, TEXT("Failed to generate a render pipeline state object:\n\n %s\n\n"), *FString(Error.GetLocalizedDescription()));
		
		// We need to pass a failure up the chain, so we'll clean up here.
		if(Pipeline->RenderPipelineState == nil)
		{
			[Pipeline release];
			return nil;
		}
		
#if METAL_DEBUG_OPTIONS
		Pipeline->VertexSource = VertexShader->GetSourceCode();
        Pipeline->FragmentSource = PixelShader ? PixelShader->GetSourceCode() : nil;
#endif
		
#if METAL_DEBUG_OPTIONS
        if (GFrameCounter > 3)
        {
            UE_LOG(LogAGX, Verbose, TEXT("Created a hitchy pipeline state for hash %llx %llx %llx"), (uint64)Key.RenderPipelineHash.RasterBits, (uint64)(Key.RenderPipelineHash.TargetBits), (uint64)Key.VertexDescriptorHash.VertexDescHash);
        }
#endif
    }
	
	if (Pipeline && AGXSafeGetRuntimeDebuggingLevel() >= EAGXDebugLevelFastValidation)
	{
		[Pipeline initResourceMask];
	}
	
    return !bSync ? nil : Pipeline;
}

FAGXShaderPipeline* GetMTLRenderPipeline(bool const bSync, FAGXGraphicsPipelineState const* State, const FGraphicsPipelineStateInitializer& Init)
{
	return FAGXShaderPipelineCache::Get().GetRenderPipeline(bSync, State, Init);
}

void ReleaseMTLRenderPipeline(FAGXShaderPipeline* Pipeline)
{
	FAGXShaderPipelineCache::Get().ReleaseRenderPipeline(Pipeline);
}

FAGXPipelineStateCacheManager::FAGXPipelineStateCacheManager()
{
#if PLATFORM_IOS
	OnShaderPipelineCachePreOpenDelegate = FShaderPipelineCache::GetCachePreOpenDelegate().AddRaw(this, &FAGXPipelineStateCacheManager::OnShaderPipelineCachePreOpen);
	OnShaderPipelineCacheOpenedDelegate = FShaderPipelineCache::GetCacheOpenedDelegate().AddRaw(this, &FAGXPipelineStateCacheManager::OnShaderPipelineCacheOpened);
	OnShaderPipelineCachePrecompilationCompleteDelegate = FShaderPipelineCache::GetPrecompilationCompleteDelegate().AddRaw(this, &FAGXPipelineStateCacheManager::OnShaderPipelineCachePrecompilationComplete);
#endif
}

FAGXPipelineStateCacheManager::~FAGXPipelineStateCacheManager()
{
	if (OnShaderPipelineCacheOpenedDelegate.IsValid())
	{
		FShaderPipelineCache::GetCacheOpenedDelegate().Remove(OnShaderPipelineCacheOpenedDelegate);
	}
	
	if (OnShaderPipelineCachePrecompilationCompleteDelegate.IsValid())
	{
		FShaderPipelineCache::GetPrecompilationCompleteDelegate().Remove(OnShaderPipelineCachePrecompilationCompleteDelegate);
	}
}

void FAGXPipelineStateCacheManager::OnShaderPipelineCachePreOpen(FString const& Name, EShaderPlatform Platform, bool& bReady)
{
	// only do this when haven't gotten a full pso cache already
	struct stat FileInfo;
	static FString PrivateWritePathBase = FString([NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) objectAtIndex:0]) + TEXT("/");
	FString Result = PrivateWritePathBase + FString([NSString stringWithFormat:@"/Caches/%@/com.apple.metal/functions.data", [NSBundle mainBundle].bundleIdentifier]);
	FString Result2 = PrivateWritePathBase + FString([NSString stringWithFormat:@"/Caches/%@/com.apple.metal/usecache.txt", [NSBundle mainBundle].bundleIdentifier]);
	if (stat(TCHAR_TO_UTF8(*Result), &FileInfo) != -1 && ((FileInfo.st_size / 1024 / 1024) > GAGXCacheMinSize) && stat(TCHAR_TO_UTF8(*Result2), &FileInfo) != -1)
	{
		bReady = false;
		FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Background);
	}
	else
	{
		bReady = true;
		FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Precompile);
	}
}

void FAGXPipelineStateCacheManager::OnShaderPipelineCacheOpened(FString const& Name, EShaderPlatform Platform, uint32 Count, const FGuid& VersionGuid, FShaderPipelineCache::FShaderCachePrecompileContext& ShaderCachePrecompileContext)
{
	ShaderCachePrecompileContext.SetPrecompilationIsSlowTask();
}

void FAGXPipelineStateCacheManager::OnShaderPipelineCachePrecompilationComplete(uint32 Count, double Seconds, const FShaderPipelineCache::FShaderCachePrecompileContext& ShaderCachePrecompileContext)
{
	// Want to ignore any subsequent Shader Pipeline Cache opening/closing, eg when loading modules
	FShaderPipelineCache::GetCachePreOpenDelegate().Remove(OnShaderPipelineCachePreOpenDelegate);
	FShaderPipelineCache::GetCacheOpenedDelegate().Remove(OnShaderPipelineCacheOpenedDelegate);
	FShaderPipelineCache::GetPrecompilationCompleteDelegate().Remove(OnShaderPipelineCachePrecompilationCompleteDelegate);
	OnShaderPipelineCachePreOpenDelegate.Reset();
	OnShaderPipelineCacheOpenedDelegate.Reset();
	OnShaderPipelineCachePrecompilationCompleteDelegate.Reset();
}

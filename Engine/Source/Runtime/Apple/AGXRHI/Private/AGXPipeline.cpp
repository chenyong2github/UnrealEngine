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
#include "AGXCommandBuffer.h"
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

static uint32 BlendBitOffsets[] = { Offset_BlendState0, Offset_BlendState1, Offset_BlendState2, Offset_BlendState3, Offset_BlendState4, Offset_BlendState5, Offset_BlendState6, Offset_BlendState7 };
static uint32 RTBitOffsets[] = { Offset_RenderTargetFormat0, Offset_RenderTargetFormat1, Offset_RenderTargetFormat2, Offset_RenderTargetFormat3, Offset_RenderTargetFormat4, Offset_RenderTargetFormat5, Offset_RenderTargetFormat6, Offset_RenderTargetFormat7 };
static_assert(Offset_RasterEnd < 64 && Offset_End < 128, "Offset_RasterEnd must be < 64 && Offset_End < 128");

static float RoundUpNearestEven(const float f)
{
	const float ret = FMath::CeilToFloat(f);
	const float isOdd = (float)(((int)ret) & 1);
	return ret + isOdd;
}

// A tile-based or vertex-based debug shader for trying to emulate Aftermath style failure reporting
static NSString* GAGXDebugShader = @"#include <metal_stdlib>\n"
"#include <metal_compute>\n"
"\n"
"using namespace metal;\n"
"\n"
"struct DebugInfo\n"
"{\n"
"   uint CmdBuffIndex;\n"
"	uint EncoderIndex;\n"
"   uint ContextIndex;\n"
"   uint CommandIndex;\n"
"   uint CommandBuffer[2];\n"
"	uint PSOSignature[4];\n"
"};\n"
"\n"
#if !PLATFORM_MAC
"// Executes once per-tile\n"
"kernel void Main_Debug(constant DebugInfo *debugTable [[ buffer(0) ]], device DebugInfo* debugBuffer [[ buffer(1) ]], uint2 threadgroup_position_in_grid [[ threadgroup_position_in_grid ]], uint2 threadgroups_per_grid [[ threadgroups_per_grid ]])\n"
"{\n"
"	// Write Pass, Draw indices\n"
"	// Write Vertex+Fragment PSO sig (in form VertexLen, VertexCRC, FragLen, FragCRC)\n"
"   uint tile_index = threadgroup_position_in_grid.x + (threadgroup_position_in_grid.y * threadgroups_per_grid.x);"
"	debugBuffer[tile_index] = debugTable[0];\n"
"}";
#else
"// Executes once as a point draw call\n"
"vertex void Main_Debug(constant DebugInfo *debugTable [[ buffer(0) ]], device DebugInfo* debugBuffer [[ buffer(1) ]])\n"
"{\n"
"	// Write Pass, Draw indices\n"
"	// Write Vertex+Fragment PSO sig (in form VertexLen, VertexCRC, FragLen, FragCRC)\n"
"	debugBuffer[0] = debugTable[0];\n"
"}";
#endif

// A compute debug shader for trying to emulate Aftermath style failure reporting
static NSString* GAGXDebugMarkerComputeShader = @"#include <metal_stdlib>\n"
"#include <metal_compute>\n"
"\n"
"using namespace metal;\n"
"\n"
"struct DebugInfo\n"
"{\n"
"   uint CmdBuffIndex;\n"
"	uint EncoderIndex;\n"
"   uint ContextIndex;\n"
"   uint CommandIndex;\n"
"   uint CommandBuffer[2];\n"
"	uint PSOSignature[4];\n"
"};\n"
"\n"
"// Executes once\n"
"kernel void Main_Debug(constant DebugInfo *debugTable [[ buffer(0) ]], device DebugInfo* debugBuffer [[ buffer(1) ]])\n"
"{\n"
"	// Write Pass, Draw indices\n"
"	// Write Vertex+Fragment PSO sig (in form VertexLen, VertexCRC, FragLen, FragCRC)\n"
"	debugBuffer[0] = debugTable[0];\n"
"}";

struct FAGXHelperFunctions
{
    mtlpp::Library DebugShadersLib;
    mtlpp::Function DebugFunc;
	
	mtlpp::Library DebugComputeShadersLib;
	mtlpp::Function DebugComputeFunc;
	mtlpp::ComputePipelineState DebugComputeState;
	
    FAGXHelperFunctions()
    {
#if !PLATFORM_TVOS
        if (GAGXCommandBufferDebuggingEnabled)
        {
            mtlpp::CompileOptions CompileOptions;
            ns::AutoReleasedError Error;

			DebugShadersLib = GetAGXDeviceContext().GetDevice().NewLibrary(GAGXDebugShader, CompileOptions, &Error);
            DebugFunc = DebugShadersLib.NewFunction(@"Main_Debug");
			
			DebugComputeShadersLib = GetAGXDeviceContext().GetDevice().NewLibrary(GAGXDebugMarkerComputeShader, CompileOptions, &Error);
			DebugComputeFunc = DebugComputeShadersLib.NewFunction(@"Main_Debug");
			
			DebugComputeState = GetAGXDeviceContext().GetDevice().NewComputePipelineState(DebugComputeFunc, &Error);
        }
#endif
    }
    
    static FAGXHelperFunctions& Get()
    {
        static FAGXHelperFunctions sSelf;
        return sSelf;
    }
    
    mtlpp::Function GetDebugFunction()
    {
        return DebugFunc;
    }
	
	mtlpp::ComputePipelineState GetDebugComputeState()
	{
		return DebugComputeState;
	}
};

mtlpp::ComputePipelineState AGXGetMetalDebugComputeState()
{
	return FAGXHelperFunctions::Get().GetDebugComputeState();
}

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
			ETextureCreateFlags Flags = (ETextureCreateFlags)Init.RenderTargetFlags[i];
			if (Flags & TexCreate_SRGB)
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
		Key.SetHashValue(Offset_PrimitiveTopology, NumBits_PrimitiveTopology, TranslatePrimitiveTopology(Init.PrimitiveType));
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
		StreamPipelineReflection = mtlpp::RenderPipelineReflection(nil);
#if METAL_DEBUG_OPTIONS
		RenderDesc = mtlpp::RenderPipelineDescriptor(nil);
		StreamDesc = mtlpp::RenderPipelineDescriptor(nil);
		ComputeDesc = mtlpp::ComputePipelineDescriptor(nil);
#endif
	}
	return Self;
}

- (void)initResourceMask
{
	if (RenderPipelineReflection)
	{
		[self initResourceMask:EAGXShaderVertex];
		[self initResourceMask:EAGXShaderFragment];
		
		if (AGXSafeGetRuntimeDebuggingLevel() < EAGXDebugLevelValidation)
		{
			RenderPipelineReflection = mtlpp::RenderPipelineReflection(nil);
		}
	}
	if (ComputePipelineReflection)
	{
		[self initResourceMask:EAGXShaderCompute];
		
		if (AGXSafeGetRuntimeDebuggingLevel() < EAGXDebugLevelValidation)
		{
			ComputePipelineReflection = mtlpp::ComputePipelineReflection(nil);
		}
	}
	if (StreamPipelineReflection)
	{
		[self initResourceMask:EAGXShaderStream];
		
		if (AGXSafeGetRuntimeDebuggingLevel() < EAGXDebugLevelValidation)
		{
			StreamPipelineReflection = mtlpp::RenderPipelineReflection(nil);
		}
	}
}
- (void)initResourceMask:(EAGXShaderFrequency)Frequency
{
	NSArray<MTLArgument*>* Arguments = nil;
	switch(Frequency)
	{
		case EAGXShaderVertex:
		{
			MTLRenderPipelineReflection* Reflection = RenderPipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.vertexArguments;
			break;
		}
		case EAGXShaderFragment:
		{
			MTLRenderPipelineReflection* Reflection = RenderPipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.fragmentArguments;
			break;
		}
		case EAGXShaderCompute:
		{
			MTLComputePipelineReflection* Reflection = ComputePipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.arguments;
			break;
		}
		case EAGXShaderStream:
		{
			MTLRenderPipelineReflection* Reflection = StreamPipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.vertexArguments;
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
		if (Flags & TexCreate_SRGB)
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
	check(Init.BoundShaderState.GeometryShaderRHI == nullptr);
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
	RenderPipelineDesc.SetInputPrimitiveTopology(TranslatePrimitiveTopology(Init.PrimitiveType));
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
			uint32 ImmutableBuffers = VertexBindings.ConstantBuffers | VertexBindings.ArgumentBuffers;
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
			uint32 ImmutableBuffers = PixelShader->Bindings.ConstantBuffers | PixelShader->Bindings.ArgumentBuffers;
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

static FAGXShaderPipeline* CreateMTLRenderPipeline(bool const bSync, FAGXGraphicsPipelineKey const& Key, const FGraphicsPipelineStateInitializer& Init)
{
    FAGXVertexShader* VertexShader = (FAGXVertexShader*)Init.BoundShaderState.VertexShaderRHI;
    FAGXPixelShader* PixelShader = (FAGXPixelShader*)Init.BoundShaderState.PixelShaderRHI;
    
    mtlpp::Function vertexFunction = VertexShader->GetFunction();
    mtlpp::Function fragmentFunction = PixelShader ? PixelShader->GetFunction() : nil;

    FAGXShaderPipeline* Pipeline = nil;
    if (vertexFunction && ((PixelShader != nullptr) == (fragmentFunction != nil)))
    {
		ns::Error Error;
		mtlpp::Device Device = GetAGXDeviceContext().GetDevice();

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
			Pipeline->RenderPipelineState = Device.NewRenderPipelineState(RenderPipelineDesc, (mtlpp::PipelineOption)RenderOption, Reflection, &RenderError);
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
		
#if !PLATFORM_TVOS
		if (GAGXCommandBufferDebuggingEnabled)
		{
#if PLATFORM_MAC
			DebugPipelineDesc.SetVertexFunction(FAGXHelperFunctions::Get().GetDebugFunction());
			DebugPipelineDesc.SetRasterizationEnabled(false);
#else
			DebugPipelineDesc.SetTileFunction(FAGXHelperFunctions::Get().GetDebugFunction());
			DebugPipelineDesc.SetRasterSampleCount(RenderPipelineDesc.GetSampleCount());
			DebugPipelineDesc.SetThreadgroupSizeMatchesTileSize(false);
#endif
#if ENABLE_METAL_GPUPROFILE
			DebugPipelineDesc.SetLabel(@"Main_Debug");
#endif

			ns::AutoReleasedError RenderError;
			METAL_GPUPROFILE(FAGXScopedCPUStats CPUStat(FString::Printf(TEXT("NewDebugPipeline: %s"), TEXT("")/**FString([RenderPipelineDesc.GetPtr() description])*/)));
			Pipeline->DebugPipelineState = Device.NewRenderPipelineState(DebugPipelineDesc, mtlpp::PipelineOption::NoPipelineOption, Reflection, nullptr);
		}
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

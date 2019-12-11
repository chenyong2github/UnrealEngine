// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalPipeline.cpp: Metal shader pipeline RHI implementation.
=============================================================================*/

#include "MetalRHIPrivate.h"
#include "MetalPipeline.h"
#include "MetalShaderResources.h"
#include "MetalResources.h"
#include "MetalProfiler.h"
#include "MetalCommandQueue.h"
#include "MetalCommandBuffer.h"
#include "RenderUtils.h"
#include "Misc/ScopeRWLock.h"
#include "HAL/PThreadEvent.h"
#include <objc/runtime.h>

static int32 GMetalCacheShaderPipelines = 1;
static FAutoConsoleVariableRef CVarMetalCacheShaderPipelines(
	TEXT("rhi.Metal.CacheShaderPipelines"),
	GMetalCacheShaderPipelines,
	TEXT("When enabled (1, default) cache all graphics pipeline state objects created in MetalRHI for the life of the program, this trades memory for performance as creating PSOs is expensive in Metal.\n")
	TEXT("Disable in the project configuration to allow PSOs to be released to save memory at the expense of reduced performance and increased hitching in-game\n. (On by default (1))"), ECVF_ReadOnly);

static int32 GMetalTessellationForcePartitionMode = 0;
static FAutoConsoleVariableRef CVarMetalTessellationForcePartitionMode(
	TEXT("rhi.Metal.TessellationForcePartitionMode"),
	GMetalTessellationForcePartitionMode,
	TEXT("The partition mode (+1) to force Metal to use for debugging or off (0). (Default: 0)"));

static int32 GMetalCacheMinSize = 32;
static FAutoConsoleVariableRef CVarMetalCacheMinSize(
	TEXT("r.ShaderPipelineCache.MetalCacheMinSizeInMB"),
	GMetalCacheMinSize,
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

static float RoundTessLevel(float TessFactor, mtlpp::TessellationPartitionMode PartitionMode)
{
	switch(PartitionMode)
	{
		case mtlpp::TessellationPartitionMode::ModePow2:
			return FMath::RoundUpToPowerOfTwo((uint32)TessFactor);
		case mtlpp::TessellationPartitionMode::ModeInteger:
			return FMath::CeilToFloat(TessFactor);
		case mtlpp::TessellationPartitionMode::ModeFractionalEven:
		case mtlpp::TessellationPartitionMode::ModeFractionalOdd: // these are handled the same way
			return RoundUpNearestEven(TessFactor);
		default:
			check(false);
			return 0.0f;
	}
}

// A tile-based or vertex-based debug shader for trying to emulate Aftermath style failure reporting
static NSString* GMetalDebugShader = @"#include <metal_stdlib>\n"
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
static NSString* GMetalDebugMarkerComputeShader = @"#include <metal_stdlib>\n"
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

// A compute shader for copying indices for separate tessellation
static NSString* GMetalCopyIndexComputeShader = @"#include <metal_stdlib>\n"
"#include <metal_compute>\n"
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"\n"
"// Executes once\n"
"kernel void Main_CopyIndex32(const device uint* source [[ buffer(0) ]], device uint* dest [[ buffer(1) ]], constant uint2& controlPointCount [[ buffer(2) ]], constant MTLDrawIndexedPrimitivesIndirectArguments& Params [[ buffer(3) ]], uint2 threadgroup_position_in_grid [[ threadgroup_position_in_grid ]], uint2 thread_position_in_threadgroup [[ thread_position_in_threadgroup ]])\n"
"{\n"
"	uint i = thread_position_in_threadgroup.y;\n"
"	uint j = threadgroup_position_in_grid.x;\n"
"	uint k = thread_position_in_threadgroup.x;\n"
"	if (k < controlPointCount.x) {\n"
"		dest[i * Params.indexCount + j * controlPointCount.y + k] = source[Params.indexStart + j * controlPointCount.x + k] + i * Params.indexCount;\n"
"	} else {\n"
"		dest[i * Params.indexCount + j * controlPointCount.y + k] = 0;\n"
"	}\n"
"}\n"
"\n"
"// Executes once\n"
"kernel void Main_CopyIndex16(const device ushort* source [[ buffer(0) ]], device uint* dest [[ buffer(1) ]], constant uint2& controlPointCount [[ buffer(2) ]], constant MTLDrawIndexedPrimitivesIndirectArguments& Params [[ buffer(3) ]], uint2 threadgroup_position_in_grid [[ threadgroup_position_in_grid ]], uint2 thread_position_in_threadgroup [[ thread_position_in_threadgroup ]])\n"
"{\n"
"	uint i = thread_position_in_threadgroup.y;\n"
"	uint j = threadgroup_position_in_grid.x;\n"
"	uint k = thread_position_in_threadgroup.x;\n"
"	if (k < controlPointCount.x) {\n"
"		dest[i * Params.indexCount + j * controlPointCount.y + k] = source[Params.indexStart + j * controlPointCount.x + k] + i * Params.indexCount;\n"
"	} else {\n"
"		dest[i * Params.indexCount + j * controlPointCount.y + k] = 0;\n"
"	}\n"
"}\n"
"\n"
"// Executes once\n"
"kernel void Main_FlattenTess(device MTLTriangleTessellationFactorsHalf* dest [[ buffer(0) ]], constant MTLDrawIndexedPrimitivesIndirectArguments& Params [[ buffer(1) ]])\n"
"{\n"
"	for(uint i = 0; i < Params.indexCount; i++) {\n"
"		dest[i].edgeTessellationFactor[0] = half(1.0);\n"
"		dest[i].edgeTessellationFactor[1] = half(1.0);\n"
"		dest[i].edgeTessellationFactor[2] = half(1.0);\n"
"		dest[i].insideTessellationFactor = half(1.0);\n"
"	}\n"
"}";

struct FMetalHelperFunctions
{
    mtlpp::Library DebugShadersLib;
    mtlpp::Function DebugFunc;
	
	mtlpp::Library DebugComputeShadersLib;
	mtlpp::Function DebugComputeFunc;
	mtlpp::ComputePipelineState DebugComputeState;
	
	mtlpp::Library CopyIndexLib;
	mtlpp::Function CopyIndex32Func;
	mtlpp::Function CopyIndex16Func;
	mtlpp::ComputePipelineState CopyIndex32State;
	mtlpp::ComputePipelineState CopyIndex16State;
	
	mtlpp::Function FlattenTessFunc;
	mtlpp::ComputePipelineState FlattenTessState;
	
    FMetalHelperFunctions()
    {
#if !PLATFORM_TVOS
        if (GMetalCommandBufferDebuggingEnabled)
        {
            mtlpp::CompileOptions CompileOptions;
            ns::AutoReleasedError Error;

			DebugShadersLib = GetMetalDeviceContext().GetDevice().NewLibrary(GMetalDebugShader, CompileOptions, &Error);
            DebugFunc = DebugShadersLib.NewFunction(@"Main_Debug");
			
			DebugComputeShadersLib = GetMetalDeviceContext().GetDevice().NewLibrary(GMetalDebugMarkerComputeShader, CompileOptions, &Error);
			DebugComputeFunc = DebugComputeShadersLib.NewFunction(@"Main_Debug");
			
			DebugComputeState = GetMetalDeviceContext().GetDevice().NewComputePipelineState(DebugComputeFunc, &Error);
        }
#endif
		{
			mtlpp::CompileOptions CompileOptions;
			ns::AutoReleasedError Error;
			
			CopyIndexLib = GetMetalDeviceContext().GetDevice().NewLibrary(GMetalCopyIndexComputeShader, CompileOptions, &Error);
			CopyIndex32Func = CopyIndexLib.NewFunction(@"Main_CopyIndex32");
			CopyIndex16Func = CopyIndexLib.NewFunction(@"Main_CopyIndex16");
			CopyIndex32State = GetMetalDeviceContext().GetDevice().NewComputePipelineState(CopyIndex32Func, &Error);
			CopyIndex16State = GetMetalDeviceContext().GetDevice().NewComputePipelineState(CopyIndex16Func, &Error);
			
			FlattenTessFunc = CopyIndexLib.NewFunction(@"Main_FlattenTess");
			FlattenTessState = GetMetalDeviceContext().GetDevice().NewComputePipelineState(FlattenTessFunc, &Error);
		}
    }
    
    static FMetalHelperFunctions& Get()
    {
        static FMetalHelperFunctions sSelf;
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
	
	mtlpp::ComputePipelineState GetCopyIndex32Function()
	{
		return CopyIndex32State;
	}
	
	mtlpp::ComputePipelineState GetCopyIndex16Function()
	{
		return CopyIndex16State;
	}
	
	mtlpp::ComputePipelineState GetFlattenTessState()
	{
		return FlattenTessState;
	}
};

mtlpp::ComputePipelineState GetMetalDebugComputeState()
{
	return FMetalHelperFunctions::Get().GetDebugComputeState();
}

mtlpp::ComputePipelineState GetMetalCopyIndex32Function()
{
	return FMetalHelperFunctions::Get().GetCopyIndex32Function();
}

mtlpp::ComputePipelineState GetMetalCopyIndex16Function()
{
	return FMetalHelperFunctions::Get().GetCopyIndex16Function();
}

mtlpp::ComputePipelineState GetMetalFlattenTessState()
{
	return FMetalHelperFunctions::Get().GetFlattenTessState();
}

struct FMetalGraphicsPipelineKey
{
	FMetalRenderPipelineHash RenderPipelineHash;
	FMetalHashedVertexDescriptor VertexDescriptorHash;
	FSHAHash VertexFunction;
	FSHAHash DomainFunction;
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

	bool operator==(FMetalGraphicsPipelineKey const& Other) const
	{
		return (RenderPipelineHash == Other.RenderPipelineHash
		&& VertexDescriptorHash == Other.VertexDescriptorHash
		&& VertexFunction == Other.VertexFunction
		&& DomainFunction == Other.DomainFunction
		&& PixelFunction == Other.PixelFunction);
	}
	
	friend uint32 GetTypeHash(FMetalGraphicsPipelineKey const& Key)
	{
		uint32 H = FCrc::MemCrc32(&Key.RenderPipelineHash, sizeof(Key.RenderPipelineHash), GetTypeHash(Key.VertexDescriptorHash));
		H = FCrc::MemCrc32(Key.VertexFunction.Hash, sizeof(Key.VertexFunction.Hash), H);
		H = FCrc::MemCrc32(Key.DomainFunction.Hash, sizeof(Key.DomainFunction.Hash), H);
		H = FCrc::MemCrc32(Key.PixelFunction.Hash, sizeof(Key.PixelFunction.Hash), H);
		return H;
	}
	
	friend void InitMetalGraphicsPipelineKey(FMetalGraphicsPipelineKey& Key, const FGraphicsPipelineStateInitializer& Init, EMetalIndexType const IndexType)
	{
		uint32 const NumActiveTargets = Init.ComputeNumValidRenderTargets();
		check(NumActiveTargets <= MaxSimultaneousRenderTargets);
	
		FMetalBlendState* BlendState = (FMetalBlendState*)Init.BlendState;
		
		FMemory::Memzero(Key.RenderPipelineHash);
		
		bool bHasActiveTargets = false;
		for (uint32 i = 0; i < NumActiveTargets; i++)
		{
			EPixelFormat TargetFormat = (EPixelFormat)Init.RenderTargetFormats[i];
			if (TargetFormat == PF_Unknown) { continue; }

			mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[TargetFormat].PlatformFormat;
			uint32 Flags = Init.RenderTargetFlags[i];
			if (Flags & TexCreate_SRGB)
			{
#if PLATFORM_MAC // Expand as R8_sRGB is iOS only.
				if (MetalFormat == mtlpp::PixelFormat::R8Unorm)
				{
					MetalFormat = mtlpp::PixelFormat::RGBA8Unorm;
				}
#endif
				MetalFormat = ToSRGBFormat(MetalFormat);
			}
			
			uint8 FormatKey = GetMetalPixelFormatKey(MetalFormat);;
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
					DepthFormatKey = GetMetalPixelFormatKey(MetalFormat);
				}
				if (Init.StencilTargetLoadAction != ERenderTargetLoadAction::ENoAction || Init.StencilTargetStoreAction != ERenderTargetStoreAction::ENoAction)
				{
					StencilFormatKey = GetMetalPixelFormatKey(mtlpp::PixelFormat::Stencil8);
				}
				bHasActiveTargets |= true;
				break;
			}
			case PF_ShadowDepth:
			{
				DepthFormatKey = GetMetalPixelFormatKey((mtlpp::PixelFormat)GPixelFormats[PF_ShadowDepth].PlatformFormat);
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
		FMetalPixelShader* PixelShader = (FMetalPixelShader*)Init.BoundShaderState.PixelShaderRHI;
		if (PixelShader && (((PixelShader->Bindings.InOutMask & 0x8000) && (DepthFormatKey == 0)) || (bHasActiveTargets == false && PixelShader->Bindings.NumUAVs > 0)))
		{
			mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat;
			DepthFormatKey = GetMetalPixelFormatKey(MetalFormat);
		}
		
		Key.SetHashValue(Offset_DepthFormat, NumBits_DepthFormat, DepthFormatKey);
		Key.SetHashValue(Offset_StencilFormat, NumBits_StencilFormat, StencilFormatKey);

		Key.SetHashValue(Offset_SampleCount, NumBits_SampleCount, Init.NumSamples);
		
#if PLATFORM_MAC
		Key.SetHashValue(Offset_PrimitiveTopology, NumBits_PrimitiveTopology, TranslatePrimitiveTopology(Init.PrimitiveType));
#endif

		FMetalVertexDeclaration* VertexDecl = (FMetalVertexDeclaration*)Init.BoundShaderState.VertexDeclarationRHI;
		Key.VertexDescriptorHash = VertexDecl->Layout;
		
		FMetalVertexShader* VertexShader = (FMetalVertexShader*)Init.BoundShaderState.VertexShaderRHI;
		Key.VertexFunction = VertexShader->GetHash();

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		FMetalDomainShader* DomainShader = (FMetalDomainShader*)Init.BoundShaderState.DomainShaderRHI;
		if (DomainShader)
		{
			Key.DomainFunction = DomainShader->GetHash();
			Key.SetHashValue(Offset_IndexType, NumBits_IndexType, IndexType);
		}
		else
#endif
		{
			Key.SetHashValue(Offset_IndexType, NumBits_IndexType, EMetalIndexType_None);
		}
		if (PixelShader)
		{
			Key.PixelFunction = PixelShader->GetHash();
		}
	}
};

static FMetalShaderPipeline* CreateMTLRenderPipeline(bool const bSync, FMetalGraphicsPipelineKey const& Key, const FGraphicsPipelineStateInitializer& Init, EMetalIndexType const IndexType);

class FMetalShaderPipelineCache
{
public:
	static FMetalShaderPipelineCache& Get()
	{
		static FMetalShaderPipelineCache sSelf;
		return sSelf;
	}
	
	FMetalShaderPipeline* GetRenderPipeline(bool const bSync, FMetalGraphicsPipelineState const* State, const FGraphicsPipelineStateInitializer& Init, EMetalIndexType const IndexType)
	{
		SCOPE_CYCLE_COUNTER(STAT_MetalPipelineStateTime);
		
		FMetalGraphicsPipelineKey Key;
		InitMetalGraphicsPipelineKey(Key, Init, IndexType);
		
		// By default there'll be more threads trying to read this than to write it.
		PipelineMutex.ReadLock();

		// Try to find the entry in the cache.
		FMetalShaderPipeline* Desc = Pipelines.FindRef(Key);

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
				Desc = CreateMTLRenderPipeline(bSync, Key, Init, IndexType);

				if (Desc != nil)
				{
					PipelineMutex.WriteLock();

					Pipelines.Add(Key, Desc);
					ReverseLookup.Add(Desc, Key);

					PipelineMutex.WriteUnlock();

					if (GMetalCacheShaderPipelines == 0)
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
	
	void ReleaseRenderPipeline(FMetalShaderPipeline* Pipeline)
	{
		if (GMetalCacheShaderPipelines)
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
	
	void RemoveRenderPipeline(FMetalShaderPipeline* Pipeline)
	{
		check (GMetalCacheShaderPipelines == 0);
		{
			FMetalGraphicsPipelineKey* Desc = ReverseLookup.Find(Pipeline);
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
	TMap<FMetalGraphicsPipelineKey, FMetalShaderPipeline*> Pipelines;
	TMap<FMetalShaderPipeline*, FMetalGraphicsPipelineKey> ReverseLookup;
	TMap<FMetalGraphicsPipelineKey, TSharedPtr<FPThreadEvent, ESPMode::ThreadSafe>> PipelineEvents;
};

@implementation FMetalShaderPipeline

APPLE_PLATFORM_OBJECT_ALLOC_OVERRIDES(FMetalShaderPipeline)

- (void)dealloc
{
	// For render pipeline states we might need to remove the PSO from the cache when we aren't caching them for program lifetime
	if (GMetalCacheShaderPipelines == 0 && RenderPipelineState)
	{
		FMetalShaderPipelineCache::Get().RemoveRenderPipeline(self);
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
		[self initResourceMask:EMetalShaderVertex];
		[self initResourceMask:EMetalShaderFragment];
		
		if (SafeGetRuntimeDebuggingLevel() < EMetalDebugLevelValidation METAL_STATISTICS_ONLY(&& !GetMetalDeviceContext().GetCommandQueue().GetStatistics()))
		{
			RenderPipelineReflection = mtlpp::RenderPipelineReflection(nil);
		}
	}
	if (ComputePipelineReflection)
	{
		[self initResourceMask:EMetalShaderCompute];
		
		if (SafeGetRuntimeDebuggingLevel() < EMetalDebugLevelValidation METAL_STATISTICS_ONLY(&& !GetMetalDeviceContext().GetCommandQueue().GetStatistics()))
		{
			ComputePipelineReflection = mtlpp::ComputePipelineReflection(nil);
		}
	}
	if (StreamPipelineReflection)
	{
		[self initResourceMask:EMetalShaderStream];
		
		if (SafeGetRuntimeDebuggingLevel() < EMetalDebugLevelValidation METAL_STATISTICS_ONLY(&& !GetMetalDeviceContext().GetCommandQueue().GetStatistics()))
		{
			StreamPipelineReflection = mtlpp::RenderPipelineReflection(nil);
		}
	}
}
- (void)initResourceMask:(EMetalShaderFrequency)Frequency
{
	NSArray<MTLArgument*>* Arguments = nil;
	switch(Frequency)
	{
		case EMetalShaderVertex:
		{
			MTLRenderPipelineReflection* Reflection = RenderPipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.vertexArguments;
			break;
		}
		case EMetalShaderFragment:
		{
			MTLRenderPipelineReflection* Reflection = RenderPipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.fragmentArguments;
			break;
		}
		case EMetalShaderCompute:
		{
			MTLComputePipelineReflection* Reflection = ComputePipelineReflection;
			check(Reflection);
			
			Arguments = Reflection.arguments;
			break;
		}
		case EMetalShaderStream:
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
				ResourceMask[Frequency].TextureMask |= (FMetalTextureMask(1) << Arg.index);
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

static MTLVertexDescriptor* GetMaskedVertexDescriptor(MTLVertexDescriptor* InputDesc, uint32 InOutMask)
{
	for (uint32 Attr = 0; Attr < MaxMetalStreams; Attr++)
	{
		if (!(InOutMask & (1 << Attr)) && [InputDesc.attributes objectAtIndexedSubscript:Attr] != nil)
		{
			MTLVertexDescriptor* Desc = [[InputDesc copy] autorelease];
			uint32 BuffersUsed = 0;
			for (uint32 i = 0; i < MaxMetalStreams; i++)
			{
				if (!(InOutMask & (1 << i)))
				{
					[Desc.attributes setObject:nil atIndexedSubscript:i];
				}
				else
				{
					BuffersUsed |= (1 << [Desc.attributes objectAtIndexedSubscript:i].bufferIndex);
				}
			}
			for (uint32 i = 0; i < ML_MaxBuffers; i++)
			{
				if (!(BuffersUsed & (1 << i)))
				{
					[Desc.layouts setObject:nil atIndexedSubscript:i];
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
											  FMetalGraphicsPipelineKey const& Key,
											  const FGraphicsPipelineStateInitializer& Init,
											  EMetalIndexType const IndexType)
{
	FMetalPixelShader* PixelShader = (FMetalPixelShader*)Init.BoundShaderState.PixelShaderRHI;
	uint32 const NumActiveTargets = Init.ComputeNumValidRenderTargets();
	check(NumActiveTargets <= MaxSimultaneousRenderTargets);
	if (PixelShader)
	{
		if ((PixelShader->Bindings.InOutMask & 0x8000) == 0 && (PixelShader->Bindings.InOutMask & 0x7fff) == 0 && PixelShader->Bindings.NumUAVs == 0 && PixelShader->Bindings.bDiscards == false)
		{
			UE_LOG(LogMetal, Error, TEXT("Pixel shader has no outputs which is not permitted. No Discards, In-Out Mask: %x\nNumber UAVs: %d\nSource Code:\n%s"), PixelShader->Bindings.InOutMask, PixelShader->Bindings.NumUAVs, *FString(PixelShader->GetSourceCode()));
			return false;
		}
		
		UE_CLOG((NumActiveTargets < __builtin_popcount(PixelShader->Bindings.InOutMask & 0x7fff)), LogMetal, Verbose, TEXT("NumActiveTargets doesn't match pipeline's pixel shader output mask: %u, %hx"), NumActiveTargets, PixelShader->Bindings.InOutMask);
	}
	
	FMetalBlendState* BlendState = (FMetalBlendState*)Init.BlendState;
	
	ns::Array<mtlpp::RenderPipelineColorAttachmentDescriptor> ColorAttachments = RenderPipelineDesc.GetColorAttachments();
#if !PLATFORM_TVOS
	auto DebugColorAttachements = DebugPipelineDesc.GetColorAttachments();
#endif
	
	uint32 TargetWidth = 0;
	for (uint32 i = 0; i < NumActiveTargets; i++)
	{
		EPixelFormat TargetFormat = (EPixelFormat)Init.RenderTargetFormats[i];
		
		METAL_FATAL_ASSERT(!(TargetFormat == PF_Unknown && PixelShader && (((PixelShader->Bindings.InOutMask & 0x7fff) & (1 << i)))), TEXT("Pipeline pixel shader expects target %u to be bound but it isn't: %s."), i, *FString(PixelShader->GetSourceCode()));
		
		TargetWidth += GPixelFormats[TargetFormat].BlockBytes;
		
		mtlpp::PixelFormat MetalFormat = (mtlpp::PixelFormat)GPixelFormats[TargetFormat].PlatformFormat;
		uint32 Flags = Init.RenderTargetFlags[i];
		if (Flags & TexCreate_SRGB)
		{
#if PLATFORM_MAC // Expand as R8_sRGB is iOS only.
			if (MetalFormat == mtlpp::PixelFormat::R8Unorm)
			{
				MetalFormat = mtlpp::PixelFormat::RGBA8Unorm;
			}
#endif
			MetalFormat = ToSRGBFormat(MetalFormat);
		}
		
		mtlpp::RenderPipelineColorAttachmentDescriptor Attachment = ColorAttachments[i];
		Attachment.SetPixelFormat(MetalFormat);
		
#if !PLATFORM_TVOS
		auto DebugAttachment = DebugColorAttachements[i];;
		DebugAttachment.SetPixelFormat(MetalFormat);
#endif
		
		mtlpp::RenderPipelineColorAttachmentDescriptor Blend = BlendState->RenderTargetStates[i].BlendState;
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
	
	if(RenderPipelineDesc.GetDepthAttachmentPixelFormat() == mtlpp::PixelFormat::Invalid && PixelShader && ((PixelShader->Bindings.InOutMask & 0x8000) || (NumActiveTargets == 0 && (PixelShader->Bindings.NumUAVs > 0))))
	{
		RenderPipelineDesc.SetDepthAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
		RenderPipelineDesc.SetStencilAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
		
#if PLATFORM_MAC
		DebugPipelineDesc.SetDepthAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
		DebugPipelineDesc.SetStencilAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
#endif
	}
	
	static bool bNoMSAA = FParse::Param(FCommandLine::Get(), TEXT("nomsaa"));
	RenderPipelineDesc.SetSampleCount(!bNoMSAA ? FMath::Max(Init.NumSamples, (uint16)1u) : (uint16)1u);
#if PLATFORM_MAC
	RenderPipelineDesc.SetInputPrimitiveTopology(TranslatePrimitiveTopology(Init.PrimitiveType));
	DebugPipelineDesc.SetSampleCount(!bNoMSAA ? FMath::Max(Init.NumSamples, (uint16)1u) : (uint16)1u);
	DebugPipelineDesc.SetInputPrimitiveTopology(mtlpp::PrimitiveTopologyClass::Point);
#endif
	
	if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesPipelineBufferMutability))
	{
		FMetalVertexShader* VertexShader = (FMetalVertexShader*)Init.BoundShaderState.VertexShaderRHI;
		
		ns::AutoReleased<ns::Array<mtlpp::PipelineBufferDescriptor>> VertexPipelineBuffers = RenderPipelineDesc.GetVertexBuffers();
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		FMetalDomainShader* DomainShader = (FMetalDomainShader*)Init.BoundShaderState.DomainShaderRHI;
		FMetalShaderBindings& VertexBindings = DomainShader ? DomainShader->Bindings : VertexShader->Bindings;
		int8 VertexSideTable = DomainShader ? DomainShader->SideTableBinding : VertexShader->SideTableBinding;
#else
		FMetalShaderBindings& VertexBindings = VertexShader->Bindings;
		int8 VertexSideTable = VertexShader->SideTableBinding;
#endif
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

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS

static mtlpp::VertexFormat Formats[(uint8)EMetalComponentType::Max][4] = {
	{mtlpp::VertexFormat::UInt, mtlpp::VertexFormat::UInt2, mtlpp::VertexFormat::UInt3, mtlpp::VertexFormat::UInt4},
	{mtlpp::VertexFormat::Int, mtlpp::VertexFormat::Int2, mtlpp::VertexFormat::Int3, mtlpp::VertexFormat::Int4},
	{mtlpp::VertexFormat::Invalid, mtlpp::VertexFormat::Half2, mtlpp::VertexFormat::Half3, mtlpp::VertexFormat::Half4},
	{mtlpp::VertexFormat::Float, mtlpp::VertexFormat::Float2, mtlpp::VertexFormat::Float3, mtlpp::VertexFormat::Float4},
	{mtlpp::VertexFormat::Invalid, mtlpp::VertexFormat::UChar2, mtlpp::VertexFormat::UChar3, mtlpp::VertexFormat::UChar4},
};

static FMetalShaderPipeline* CreateSeparateMetalTessellationPipeline(bool const bSync, FMetalGraphicsPipelineKey const& Key, const FGraphicsPipelineStateInitializer& Init, EMetalIndexType const IndexType)
{
	FMetalShaderPipeline* Pipeline = [FMetalShaderPipeline new];
	METAL_DEBUG_OPTION(FMemory::Memzero(Pipeline->ResourceMask, sizeof(Pipeline->ResourceMask)));
	
	FMetalVertexShader* VertexShader = (FMetalVertexShader*)Init.BoundShaderState.VertexShaderRHI;
	mtlpp::Function vertexFunction = VertexShader->GetFunction();
	
	mtlpp::RenderPipelineDescriptor VertexRenderPipelineDesc;
	VertexRenderPipelineDesc.SetRasterizationEnabled(false);
	
#if PLATFORM_MAC
	VertexRenderPipelineDesc.SetInputPrimitiveTopology(TranslatePrimitiveTopology(Init.PrimitiveType));
#endif
	
	FMetalVertexDeclaration* VertexDecl = (FMetalVertexDeclaration*)Init.BoundShaderState.VertexDeclarationRHI;
	VertexRenderPipelineDesc.SetVertexDescriptor(GetMaskedVertexDescriptor(VertexDecl->Layout.VertexDesc, VertexShader->Bindings.InOutMask));
	VertexRenderPipelineDesc.SetVertexFunction(vertexFunction);
	
	VertexRenderPipelineDesc.SetDepthAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
	VertexRenderPipelineDesc.SetStencilAttachmentPixelFormat((mtlpp::PixelFormat)GPixelFormats[PF_DepthStencil].PlatformFormat);
	
#if ENABLE_METAL_GPUPROFILE
	ns::String VertexName = vertexFunction.GetName();
	VertexRenderPipelineDesc.SetLabel([NSString stringWithFormat:@"%@", VertexName.GetPtr()]);
#endif
	
	NSUInteger RenderOption = mtlpp::PipelineOption::NoPipelineOption;
	mtlpp::AutoReleasedRenderPipelineReflection* Reflection = nullptr;
	mtlpp::AutoReleasedRenderPipelineReflection OutReflection;
	Reflection = &OutReflection;
	if (GetMetalDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelFastValidation METAL_STATISTICS_ONLY(|| GetMetalDeviceContext().GetCommandQueue().GetStatistics()))
	{
		RenderOption = mtlpp::PipelineOption::ArgumentInfo|mtlpp::PipelineOption::BufferTypeInfo METAL_STATISTICS_ONLY(|NSUInteger(EMTLPipelineStats));
	}
	
	ns::Error Error;
	mtlpp::Device Device = GetMetalDeviceContext().GetDevice();
	mtlpp::RenderPipelineState& VertexRenderPipelineState = Pipeline->StreamPipelineState;
	{
		ns::AutoReleasedError RenderError;
		METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewRenderPipeline: %s"), TEXT("")/**FString([RenderPipelineDesc.GetPtr() description])*/)));
		VertexRenderPipelineState = Device.NewRenderPipelineState(VertexRenderPipelineDesc, (mtlpp::PipelineOption)RenderOption, Reflection, &RenderError);
		if (Reflection)
		{
			Pipeline->StreamPipelineReflection = *Reflection;
#if METAL_DEBUG_OPTIONS
			Pipeline->StreamDesc = VertexRenderPipelineDesc;
#endif
		}
		Error = RenderError;
	}
	
	UE_CLOG((VertexRenderPipelineState == nil), LogMetal, Error, TEXT("Failed to generate a pipeline state object: %s"), *FString(Error.GetPtr().description));
	UE_CLOG((VertexRenderPipelineState == nil), LogMetal, Error, TEXT("Vertex shader: %s"), *FString(VertexShader->GetSourceCode()));
	UE_CLOG((VertexRenderPipelineState == nil), LogMetal, Error, TEXT("Descriptor: %s"), *FString(VertexRenderPipelineDesc.GetPtr().description));
	UE_CLOG((VertexRenderPipelineState == nil), LogMetal, Error, TEXT("Failed to generate a render pipeline state object:\n\n %s\n\n"), *FString(Error.GetLocalizedDescription()));

	FMetalHullShader* HullShader = (FMetalHullShader*)Init.BoundShaderState.HullShaderRHI;
	mtlpp::ComputePipelineDescriptor HullShaderPipelineDesc;
	
	mtlpp::StageInputOutputDescriptor HullStageInOut;
	
	ns::Array<mtlpp::BufferLayoutDescriptor> HullVertexLayouts = HullStageInOut.GetLayouts();
	ns::Array<mtlpp::AttributeDescriptor> HullVertexAttribs = HullStageInOut.GetAttributes();
	
	bool bFoundAttrib = false;
	for (FMetalAttribute const& HSAttrib : HullShader->TessellationOutputAttribs.HSIn)
	{
		bFoundAttrib = false;
		for (FMetalAttribute const& VSAttrib : VertexShader->TessellationOutputAttribs.HSOut)
		{
			if (HSAttrib.Semantic == VSAttrib.Semantic)
			{
				uint32 attributeIndex = HSAttrib.Index;

				mtlpp::VertexFormat format = Formats[(uint8)HSAttrib.Type][HSAttrib.Components-1];
				check(format != mtlpp::VertexFormat::Invalid); // TODO support more cases
				HullVertexAttribs[attributeIndex].SetFormat((mtlpp::AttributeFormat)format);
				HullVertexAttribs[attributeIndex].SetOffset(VSAttrib.Offset);
				HullVertexAttribs[attributeIndex].SetBufferIndex(HullShader->TessellationControlPointIndexBuffer);
				bFoundAttrib = true;
				break;
			}
		}
		check(bFoundAttrib);
	}
	
	// if (IndexType == EMetalIndexType_UInt16 || IndexType == EMetalIndexType_UInt32)
	{
		HullStageInOut.SetIndexType(GetMetalIndexType(EMetalIndexType_UInt32));
		HullStageInOut.SetIndexBufferIndex(HullShader->TessellationIndexBuffer);
		HullVertexLayouts[HullShader->TessellationControlPointIndexBuffer].SetStepFunction(mtlpp::StepFunction::ThreadPositionInGridXIndexed);
	}
//	else
//	{
//		HullVertexLayouts[HullShader->TessellationControlPointIndexBuffer].SetStepFunction(mtlpp::StepFunction::ThreadPositionInGridX);
//	}
	HullVertexLayouts[HullShader->TessellationControlPointIndexBuffer].SetStride(VertexShader->TessellationOutputAttribs.HSOutSize);
	
	mtlpp::Function hullFunction = HullShader->GetFunction();
	HullShaderPipelineDesc.SetComputeFunction(hullFunction);
	check(HullShaderPipelineDesc.GetComputeFunction());
	HullShaderPipelineDesc.SetStageInputDescriptor(HullStageInOut);
	
	mtlpp::ComputePipelineState& HullPipelineState = Pipeline->ComputePipelineState;
	{
		METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewComputePipelineState: %s"), TEXT("")/**FString([ComputePipelineDesc.GetPtr() description])*/)));
		ns::AutoReleasedError AutoError;
		NSUInteger ComputeOption = mtlpp::PipelineOption::NoPipelineOption;
#if ENABLE_METAL_GPUPROFILE
		{
			ns::String HullName = hullFunction.GetName();
			HullShaderPipelineDesc.SetLabel([NSString stringWithFormat:@"%@", HullName.GetPtr()]);
		}
#endif
		if (GetMetalDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelFastValidation METAL_STATISTICS_ONLY(|| GetMetalDeviceContext().GetCommandQueue().GetStatistics()))
		{
			mtlpp::AutoReleasedComputePipelineReflection HullReflection;
			ComputeOption = mtlpp::PipelineOption::ArgumentInfo|mtlpp::PipelineOption::BufferTypeInfo METAL_STATISTICS_ONLY(|NSUInteger(EMTLPipelineStats));
			HullPipelineState = Device.NewComputePipelineState(HullShaderPipelineDesc, (mtlpp::PipelineOption)ComputeOption, &HullReflection, &AutoError);
			Pipeline->ComputePipelineReflection = HullReflection;
		}
		else
		{
			HullPipelineState = Device.NewComputePipelineState(HullShaderPipelineDesc, (mtlpp::PipelineOption)ComputeOption, nullptr, &AutoError);
		}
		Error = AutoError;
		
#if METAL_DEBUG_OPTIONS
		Pipeline->ComputeDesc = HullShaderPipelineDesc;
#endif
		
		UE_CLOG((HullPipelineState == nil), LogMetal, Error, TEXT("Failed to generate a pipeline state object: %s"), *FString([Error.GetPtr() description]));
		UE_CLOG((HullPipelineState == nil), LogMetal, Error, TEXT("Hull shader: %s"), *FString(HullShader->GetSourceCode()));
		UE_CLOG((HullPipelineState == nil), LogMetal, Error, TEXT("Descriptor: %s"), *FString(HullShaderPipelineDesc.GetPtr().description));
		UE_CLOG((HullPipelineState == nil), LogMetal, Error, TEXT("Failed to generate a hull pipeline state object:\n\n %s\n\n"), *FString(Error.GetLocalizedDescription()));
	}
	
#if PLATFORM_MAC
	mtlpp::RenderPipelineDescriptor DebugPipelineDesc;
#elif !PLATFORM_TVOS
	mtlpp::TileRenderPipelineDescriptor DebugPipelineDesc;
#endif

	FMetalDomainShader* DomainShader = (FMetalDomainShader*)Init.BoundShaderState.DomainShaderRHI;
	mtlpp::RenderPipelineDescriptor DomainRenderPipelineDesc;
	{
		mtlpp::VertexDescriptor DomainVertexDesc;
		ns::Array<mtlpp::VertexBufferLayoutDescriptor> DomainVertexLayouts = DomainVertexDesc.GetLayouts();
		ns::Array<mtlpp::VertexAttributeDescriptor> Attribs = DomainVertexDesc.GetAttributes();
		
		if (DomainShader->TessellationHSOutBuffer != UINT_MAX && HullShader->TessellationOutputAttribs.HSOutSize)
		{
			check(DomainShader->TessellationHSOutBuffer < ML_MaxBuffers);
			uint32 bufferIndex = DomainShader->TessellationHSOutBuffer;
			uint32 bufferSize = HullShader->TessellationOutputAttribs.HSOutSize;
			
			DomainVertexLayouts[bufferIndex].SetStride(bufferSize);
			DomainVertexLayouts[bufferIndex].SetStepFunction(mtlpp::VertexStepFunction::PerPatch);
			DomainVertexLayouts[bufferIndex].SetStepRate(1);
			
			bFoundAttrib = false;
			for (FMetalAttribute const& VSAttrib : DomainShader->TessellationOutputAttribs.HSOut)
			{
				bFoundAttrib = false;
				for (FMetalAttribute const& HSAttrib : HullShader->TessellationOutputAttribs.HSOut)
				{
					if (HSAttrib.Semantic == VSAttrib.Semantic)
					{
						uint32 attributeIndex = VSAttrib.Index;
						
						mtlpp::VertexFormat format = Formats[(uint8)VSAttrib.Type][VSAttrib.Components-1];
						check(format != mtlpp::VertexFormat::Invalid); // TODO support more cases
						Attribs[attributeIndex].SetFormat(format);
						Attribs[attributeIndex].SetOffset(HSAttrib.Offset);
						Attribs[attributeIndex].SetBufferIndex(DomainShader->TessellationHSOutBuffer);
						bFoundAttrib = true;
						break;
					}
				}
				check(bFoundAttrib);
			}
		}
		
		if (DomainShader->TessellationControlPointOutBuffer != UINT_MAX && HullShader->TessellationOutputAttribs.PatchControlPointOutSize)
		{
			uint32 bufferIndex = DomainShader->TessellationControlPointOutBuffer;
			uint32 bufferSize = HullShader->TessellationOutputAttribs.PatchControlPointOutSize;
			
			DomainVertexLayouts[bufferIndex].SetStride(bufferSize);
			DomainVertexLayouts[bufferIndex].SetStepFunction(mtlpp::VertexStepFunction::PerPatchControlPoint);
			DomainVertexLayouts[bufferIndex].SetStepRate(1);
			
			bFoundAttrib = false;
			for (FMetalAttribute const& VSAttrib : DomainShader->TessellationOutputAttribs.PatchControlPointOut)
			{
				bFoundAttrib = false;
				for (FMetalAttribute const& HSAttrib : HullShader->TessellationOutputAttribs.PatchControlPointOut)
				{
					if (HSAttrib.Semantic == VSAttrib.Semantic)
					{
						uint32 attributeIndex = VSAttrib.Index;
						
						mtlpp::VertexFormat format = Formats[(uint8)VSAttrib.Type][VSAttrib.Components-1];
						check(format != mtlpp::VertexFormat::Invalid); // TODO support more cases
						Attribs[attributeIndex].SetFormat(format);
						Attribs[attributeIndex].SetOffset(HSAttrib.Offset);
						Attribs[attributeIndex].SetBufferIndex(DomainShader->TessellationControlPointOutBuffer);
						bFoundAttrib = true;
						break;
					}
				}
				check(bFoundAttrib);
			}
		}
		
		DomainRenderPipelineDesc.SetTessellationPartitionMode(GMetalTessellationForcePartitionMode == 0 ? HullShader->TessellationPartitioning : (mtlpp::TessellationPartitionMode)(GMetalTessellationForcePartitionMode - 1));
		DomainRenderPipelineDesc.SetTessellationFactorStepFunction(mtlpp::TessellationFactorStepFunction::PerPatch);
		DomainRenderPipelineDesc.SetTessellationOutputWindingOrder(HullShader->TessellationOutputWinding);
		int FixedMaxTessFactor = (int)RoundTessLevel(HullShader->TessellationMaxTessFactor, DomainRenderPipelineDesc.GetTessellationPartitionMode());
		DomainRenderPipelineDesc.SetMaxTessellationFactor(FixedMaxTessFactor);
		DomainRenderPipelineDesc.SetTessellationFactorScaleEnabled(NO);
		DomainRenderPipelineDesc.SetTessellationFactorFormat(mtlpp::TessellationFactorFormat::Half);
		DomainRenderPipelineDesc.SetTessellationControlPointIndexType(mtlpp::TessellationControlPointIndexType::None);
		DomainRenderPipelineDesc.SetVertexDescriptor(DomainVertexDesc);
		
#if PLATFORM_TVOS
		if (!ConfigureRenderPipelineDescriptor(DomainRenderPipelineDesc, Key, Init, IndexType))
#else
		if (!ConfigureRenderPipelineDescriptor(DomainRenderPipelineDesc, DebugPipelineDesc, Key, Init, IndexType))
#endif
		{
			[Pipeline release];
			return nil;
		}
		
		mtlpp::Function domainFunction = DomainShader ? DomainShader->GetFunction() : nil;

		FMetalPixelShader* PixelShader = (FMetalPixelShader*)Init.BoundShaderState.PixelShaderRHI;
		mtlpp::Function fragmentFunction = PixelShader ? PixelShader->GetFunction() : nil;
		
		DomainRenderPipelineDesc.SetVertexFunction(domainFunction);
		DomainRenderPipelineDesc.SetFragmentFunction(fragmentFunction);
#if ENABLE_METAL_GPUPROFILE
		ns::String DomainName = domainFunction.GetName();
		ns::String FragmentName = fragmentFunction ? fragmentFunction.GetName() : @"";
		DomainRenderPipelineDesc.SetLabel([NSString stringWithFormat:@"%@+%@", DomainName.GetPtr(), FragmentName.GetPtr()]);
#endif
		
		mtlpp::RenderPipelineState& RenderPipelineState = Pipeline->RenderPipelineState;
		{
			ns::AutoReleasedError RenderError;
			METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewRenderPipeline: %s"), TEXT("")/**FString([RenderPipelineDesc.GetPtr() description])*/)));
			RenderPipelineState = Device.NewRenderPipelineState(DomainRenderPipelineDesc, (mtlpp::PipelineOption)RenderOption, Reflection, &RenderError);
			if (Reflection)
			{
				Pipeline->RenderPipelineReflection = *Reflection;
#if METAL_DEBUG_OPTIONS
				Pipeline->RenderDesc = DomainRenderPipelineDesc;
#endif
			}
			Error = RenderError;
		}
		
		UE_CLOG((RenderPipelineState == nil), LogMetal, Error, TEXT("Failed to generate a pipeline state object: %s"), *FString(Error.GetPtr().description));
		UE_CLOG((RenderPipelineState == nil), LogMetal, Error, TEXT("Domain shader: %s"), DomainShader ? *FString(DomainShader->GetSourceCode()) : TEXT("NULL"));
		UE_CLOG((RenderPipelineState == nil), LogMetal, Error, TEXT("Pixel shader: %s"), PixelShader ? *FString(PixelShader->GetSourceCode()) : TEXT("NULL"));
		UE_CLOG((RenderPipelineState == nil), LogMetal, Error, TEXT("Descriptor: %s"), *FString(DomainRenderPipelineDesc.GetPtr().description));
		UE_CLOG((RenderPipelineState == nil), LogMetal, Error, TEXT("Failed to generate a render pipeline state object:\n\n %s\n\n"), *FString(Error.GetLocalizedDescription()));
	}
	
	// We need to pass a failure up the chain, so we'll clean up here.
	if(Pipeline->StreamPipelineState == nil || Pipeline->ComputePipelineState == nil || Pipeline->RenderPipelineState == nil)
	{
		[Pipeline release];
		return nil;
	}
	
#if METAL_DEBUG_OPTIONS
	Pipeline->ComputeSource = HullShader->GetSourceCode();
	Pipeline->VertexSource = VertexShader->GetSourceCode();
	Pipeline->DomainSource = DomainShader->GetSourceCode();
	FMetalPixelShader* PixelShader = (FMetalPixelShader*)Init.BoundShaderState.PixelShaderRHI;
	Pipeline->FragmentSource = PixelShader ? PixelShader->GetSourceCode() : nil;
#endif
	
#if !PLATFORM_TVOS
	if (GMetalCommandBufferDebuggingEnabled)
	{
#if PLATFORM_MAC
		DebugPipelineDesc.SetVertexFunction(FMetalHelperFunctions::Get().GetDebugFunction());
		DebugPipelineDesc.SetRasterizationEnabled(false);
#else
		DebugPipelineDesc.SetTileFunction(FMetalHelperFunctions::Get().GetDebugFunction());
		DebugPipelineDesc.SetRasterSampleCount(RenderPipelineDesc.GetSampleCount());
		DebugPipelineDesc.SetThreadgroupSizeMatchesTileSize(false);
#endif
#if ENABLE_METAL_GPUPROFILE
		DebugPipelineDesc.SetLabel(@"Main_Debug");
#endif
		
		ns::AutoReleasedError RenderError;
		METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewDebugPipeline: %s"), TEXT("")/**FString([RenderPipelineDesc.GetPtr() description])*/)));
		Pipeline->DebugPipelineState = Device.NewRenderPipelineState(DebugPipelineDesc, mtlpp::PipelineOption::NoPipelineOption, Reflection, nullptr);
	}
#endif
	
#if METAL_DEBUG_OPTIONS
	if (GFrameCounter > 3)
	{
		UE_LOG(LogMetal, Verbose, TEXT("Created a hitchy pipeline state for hash %llx %llx %llx"), (uint64)Key.RenderPipelineHash.RasterBits, (uint64)(Key.RenderPipelineHash.TargetBits), (uint64)Key.VertexDescriptorHash.VertexDescHash);
	}
#endif

	if (Pipeline && SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelFastValidation)
	{
		[Pipeline initResourceMask];
	}
	
	return Pipeline;
}
#endif

static FMetalShaderPipeline* CreateMTLRenderPipeline(bool const bSync, FMetalGraphicsPipelineKey const& Key, const FGraphicsPipelineStateInitializer& Init, EMetalIndexType const IndexType)
{
    FMetalVertexShader* VertexShader = (FMetalVertexShader*)Init.BoundShaderState.VertexShaderRHI;
    FMetalPixelShader* PixelShader = (FMetalPixelShader*)Init.BoundShaderState.PixelShaderRHI;
    
    mtlpp::Function vertexFunction = VertexShader->GetFunction();
    mtlpp::Function fragmentFunction = PixelShader ? PixelShader->GetFunction() : nil;

#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	FMetalDomainShader* DomainShader = (FMetalDomainShader*)Init.BoundShaderState.DomainShaderRHI;
    mtlpp::Function domainFunction = DomainShader ? DomainShader->GetFunction() : nil;
	if (DomainShader && FMetalCommandQueue::SupportsFeature(EMetalFeaturesSeparateTessellation))
	{
		return CreateSeparateMetalTessellationPipeline(bSync, Key, Init, IndexType);
	}
#endif
    
    FMetalShaderPipeline* Pipeline = nil;
    if (vertexFunction && ((PixelShader != nullptr) == (fragmentFunction != nil))
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		&& ((DomainShader != nullptr) == (domainFunction != nil))
#endif
	)
    {
		ns::Error Error;
		mtlpp::Device Device = GetMetalDeviceContext().GetDevice();

		uint32 const NumActiveTargets = Init.ComputeNumValidRenderTargets();
        check(NumActiveTargets <= MaxSimultaneousRenderTargets);
		
		Pipeline = [FMetalShaderPipeline new];
		METAL_DEBUG_OPTION(FMemory::Memzero(Pipeline->ResourceMask, sizeof(Pipeline->ResourceMask)));

		mtlpp::RenderPipelineDescriptor RenderPipelineDesc;
        mtlpp::ComputePipelineDescriptor ComputePipelineDesc(nil);
#if PLATFORM_MAC
        mtlpp::RenderPipelineDescriptor DebugPipelineDesc;
#elif !PLATFORM_TVOS
        mtlpp::TileRenderPipelineDescriptor DebugPipelineDesc;
#endif
		
#if PLATFORM_TVOS
		if (!ConfigureRenderPipelineDescriptor(RenderPipelineDesc, Key, Init, IndexType))
#else
		if (!ConfigureRenderPipelineDescriptor(RenderPipelineDesc, DebugPipelineDesc, Key, Init, IndexType))
#endif
		{
			return nil;
		}
        
        FMetalVertexDeclaration* VertexDecl = (FMetalVertexDeclaration*)Init.BoundShaderState.VertexDeclarationRHI;
		
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		FMetalHullShader* HullShader = (FMetalHullShader*)Init.BoundShaderState.HullShaderRHI;
		if (Init.BoundShaderState.HullShaderRHI == nullptr)
#endif
        {
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
            check(Init.BoundShaderState.DomainShaderRHI == nullptr);
#endif
            RenderPipelineDesc.SetVertexDescriptor(GetMaskedVertexDescriptor(VertexDecl->Layout.VertexDesc, VertexShader->Bindings.InOutMask));
            RenderPipelineDesc.SetVertexFunction(vertexFunction);
            RenderPipelineDesc.SetFragmentFunction(fragmentFunction);
#if ENABLE_METAL_GPUPROFILE
			ns::String VertexName = vertexFunction.GetName();
			ns::String FragmentName = fragmentFunction ? fragmentFunction.GetName() : @"";
			RenderPipelineDesc.SetLabel([NSString stringWithFormat:@"%@+%@", VertexName.GetPtr(), FragmentName.GetPtr()]);
#endif
        }
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
        else
        {
            check(Init.BoundShaderState.DomainShaderRHI != nullptr);
            
            RenderPipelineDesc.SetTessellationPartitionMode(GMetalTessellationForcePartitionMode == 0 ? DomainShader->TessellationPartitioning : (mtlpp::TessellationPartitionMode)(GMetalTessellationForcePartitionMode - 1));
			RenderPipelineDesc.SetTessellationFactorStepFunction(mtlpp::TessellationFactorStepFunction::PerPatch);
            RenderPipelineDesc.SetTessellationOutputWindingOrder(DomainShader->TessellationOutputWinding);
            int FixedMaxTessFactor = (int)RoundTessLevel(VertexShader->TessellationMaxTessFactor, RenderPipelineDesc.GetTessellationPartitionMode());
            RenderPipelineDesc.SetMaxTessellationFactor(FixedMaxTessFactor);
			RenderPipelineDesc.SetTessellationFactorScaleEnabled(NO);
			RenderPipelineDesc.SetTessellationFactorFormat(mtlpp::TessellationFactorFormat::Half);
			RenderPipelineDesc.SetTessellationControlPointIndexType(mtlpp::TessellationControlPointIndexType::None);
            
            RenderPipelineDesc.SetVertexFunction(domainFunction);
            RenderPipelineDesc.SetFragmentFunction(fragmentFunction);
#if ENABLE_METAL_GPUPROFILE
			{
				ns::String VertexName = domainFunction.GetName();
				ns::String FragmentName = fragmentFunction ? fragmentFunction.GetName() : @"";
				RenderPipelineDesc.SetLabel([NSString stringWithFormat:@"%@+%@", VertexName.GetPtr(), FragmentName.GetPtr()]);
			}
#endif
            
			ComputePipelineDesc = mtlpp::ComputePipelineDescriptor();
            check(ComputePipelineDesc);
			
			if (FMetalCommandQueue::SupportsFeature(EMetalFeaturesPipelineBufferMutability))
			{
				ns::AutoReleased<ns::Array<mtlpp::PipelineBufferDescriptor>> PipelineBuffers = ComputePipelineDesc.GetBuffers();
				
				uint32 ImmutableBuffers = VertexShader->Bindings.ConstantBuffers | VertexShader->Bindings.ArgumentBuffers;
				while(ImmutableBuffers)
				{
					uint32 Index = __builtin_ctz(ImmutableBuffers);
					ImmutableBuffers &= ~(1 << Index);
					
					if (Index < ML_MaxBuffers)
					{
						ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = PipelineBuffers[Index];
						PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
					}
				}
				if (VertexShader->SideTableBinding > 0)
				{
					ns::AutoReleased<mtlpp::PipelineBufferDescriptor> PipelineBuffer = PipelineBuffers[VertexShader->SideTableBinding];
					PipelineBuffer.SetMutability(mtlpp::Mutability::Immutable);
				}
			}
			
			mtlpp::VertexDescriptor DomainVertexDesc;
			mtlpp::StageInputOutputDescriptor ComputeStageInOut;
			ComputeStageInOut.SetIndexBufferIndex(VertexShader->TessellationControlPointIndexBuffer);
			
            FMetalTessellationPipelineDesc& TessellationDesc = Pipeline->TessellationPipelineDesc;
            TessellationDesc.TessellationInputControlPointBufferIndex = DomainShader->TessellationControlPointOutBuffer;
            TessellationDesc.TessellationOutputControlPointBufferIndex = VertexShader->TessellationControlPointOutBuffer;
            TessellationDesc.TessellationInputPatchConstBufferIndex = DomainShader->TessellationHSOutBuffer;
            TessellationDesc.TessellationPatchConstBufferIndex = VertexShader->TessellationHSOutBuffer;
            TessellationDesc.TessellationFactorBufferIndex = VertexShader->TessellationHSTFOutBuffer;
            TessellationDesc.TessellationPatchCountBufferIndex = VertexShader->TessellationPatchCountBuffer;
            TessellationDesc.TessellationIndexBufferIndex = VertexShader->TessellationIndexBuffer;
            TessellationDesc.TessellationPatchConstOutSize = VertexShader->TessellationOutputAttribs.HSOutSize;
			TessellationDesc.TessellationControlPointIndexBufferIndex = VertexShader->TessellationControlPointIndexBuffer;
            TessellationDesc.TessellationPatchControlPointOutSize = VertexShader->TessellationOutputAttribs.PatchControlPointOutSize;
            TessellationDesc.TessellationTessFactorOutSize = VertexShader->TessellationOutputAttribs.HSTFOutSize;
            
            check(TessellationDesc.TessellationOutputControlPointBufferIndex < ML_MaxBuffers);
            check(TessellationDesc.TessellationFactorBufferIndex < ML_MaxBuffers);
            check(TessellationDesc.TessellationPatchCountBufferIndex < ML_MaxBuffers);
            check(TessellationDesc.TessellationTessFactorOutSize == 2*4 || TessellationDesc.TessellationTessFactorOutSize == 2*6);
            
            mtlpp::VertexStepFunction stepFunction = mtlpp::VertexStepFunction::PerPatch;
			
			ns::Array<mtlpp::VertexBufferLayoutDescriptor> DomainVertexLayouts = DomainVertexDesc.GetLayouts();
			
            if (DomainShader->TessellationHSOutBuffer != UINT_MAX)
            {
                check(DomainShader->TessellationHSOutBuffer < ML_MaxBuffers);
                uint32 bufferIndex = DomainShader->TessellationHSOutBuffer;
                uint32 bufferSize = VertexShader->TessellationOutputAttribs.HSOutSize;
               
                DomainVertexLayouts[bufferIndex].SetStride(bufferSize);
                DomainVertexLayouts[bufferIndex].SetStepFunction(stepFunction);
                DomainVertexLayouts[bufferIndex].SetStepRate(1);
				
				ns::Array<mtlpp::VertexAttributeDescriptor> Attribs = DomainVertexDesc.GetAttributes();
				
                for (FMetalAttribute const& Attrib : VertexShader->TessellationOutputAttribs.HSOut)
                {
                    int attributeIndex = Attrib.Index;
                    check(attributeIndex >= 0 && attributeIndex <= 31);
                    check(Attrib.Components > 0 && Attrib.Components <= 4);
                    mtlpp::VertexFormat format = Formats[(uint8)Attrib.Type][Attrib.Components-1];
                    check(format != mtlpp::VertexFormat::Invalid); // TODO support more cases
                    Attribs[attributeIndex].SetFormat(format);
                    Attribs[attributeIndex].SetOffset(Attrib.Offset);
                    Attribs[attributeIndex].SetBufferIndex(bufferIndex);
                }
            }
			
            stepFunction = mtlpp::VertexStepFunction::PerPatchControlPoint;
            uint32 bufferIndex = DomainShader->TessellationControlPointOutBuffer;
            uint32 bufferSize = VertexShader->TessellationOutputAttribs.PatchControlPointOutSize;
            
            DomainVertexLayouts[bufferIndex].SetStride(bufferSize);
            DomainVertexLayouts[bufferIndex].SetStepFunction(stepFunction);
            DomainVertexLayouts[bufferIndex].SetStepRate(1);
			
			ns::Array<mtlpp::VertexAttributeDescriptor> DomainVertexAttribs = DomainVertexDesc.GetAttributes();
            for (FMetalAttribute const& Attrib : VertexShader->TessellationOutputAttribs.PatchControlPointOut)
            {
                int attributeIndex = Attrib.Index;
                check(attributeIndex >= 0 && attributeIndex <= 31);
                check(Attrib.Components > 0 && Attrib.Components <= 4);
                mtlpp::VertexFormat format = Formats[(uint8)Attrib.Type][Attrib.Components-1];
                check(format != mtlpp::VertexFormat::Invalid); // TODO support more cases
				DomainVertexAttribs[attributeIndex].SetFormat(format);
				DomainVertexAttribs[attributeIndex].SetOffset(Attrib.Offset);
				DomainVertexAttribs[attributeIndex].SetBufferIndex(bufferIndex);
            }
			
			RenderPipelineDesc.SetVertexDescriptor(DomainVertexDesc);
            
            bool const bIsIndexed = (IndexType == EMetalIndexType_UInt16 || IndexType == EMetalIndexType_UInt32);
            
			mtlpp::VertexDescriptor VertexDesc = GetMaskedVertexDescriptor(VertexDecl->Layout.VertexDesc, VertexShader->Bindings.InOutMask);
			ns::Array<mtlpp::VertexBufferLayoutDescriptor> VertexLayouts = VertexDesc.GetLayouts();
			ns::Array<mtlpp::VertexAttributeDescriptor> VertexAttribs = VertexDesc.GetAttributes();
			ns::Array<mtlpp::BufferLayoutDescriptor> ComputeLayouts = ComputeStageInOut.GetLayouts();
			ns::Array<mtlpp::AttributeDescriptor> ComputeAttribs = ComputeStageInOut.GetAttributes();
			for(int onIndex = 0; onIndex < MaxMetalStreams; onIndex++)
            {
                // NOTE: accessing the VertexDesc like this will end up allocating layouts/attributes
                auto stride = VertexLayouts[onIndex].GetStride();
                if(stride)
                {
                    ComputeLayouts[onIndex].SetStride(stride);
                    auto InnerStepFunction = VertexLayouts[onIndex].GetStepFunction();
                    switch(InnerStepFunction)
                    {
                        case mtlpp::VertexStepFunction::Constant:
							ComputeLayouts[onIndex].SetStepFunction(mtlpp::StepFunction::Constant);
                            break;
                        case mtlpp::VertexStepFunction::PerVertex:
                            ComputeLayouts[onIndex].SetStepFunction(bIsIndexed ? mtlpp::StepFunction::ThreadPositionInGridXIndexed : mtlpp::StepFunction::ThreadPositionInGridX);
                            break;
                        case mtlpp::VertexStepFunction::PerInstance:
                            ComputeLayouts[onIndex].SetStepFunction(mtlpp::StepFunction::ThreadPositionInGridY);
                            break;
                        default:
                            check(0);
                    }
                    ComputeLayouts[onIndex].SetStepRate(VertexLayouts[onIndex].GetStepRate());
                }
                auto format = VertexAttribs[onIndex].GetFormat();
                if(format == mtlpp::VertexFormat::Invalid) continue;
                {
					ComputeAttribs[onIndex].SetFormat((mtlpp::AttributeFormat)format); // TODO FIXME currently these align perfectly (at least assert that is the case)
                    ComputeAttribs[onIndex].SetOffset(VertexAttribs[onIndex].GetOffset());
                    ComputeAttribs[onIndex].SetBufferIndex(VertexAttribs[onIndex].GetBufferIndex());
                }
            }
            
            // Disambiguated function name.
            ComputePipelineDesc.SetComputeFunction(vertexFunction);
            check(ComputePipelineDesc.GetComputeFunction());

            // Don't set the index type if there isn't an index buffer.
            if (IndexType != EMetalIndexType_None)
            {
                ComputeStageInOut.SetIndexType(GetMetalIndexType(IndexType));
            }
			ComputePipelineDesc.SetStageInputDescriptor(ComputeStageInOut);
			
            {
				METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewComputePipelineState: %s"), TEXT("")/**FString([ComputePipelineDesc.GetPtr() description])*/)));
				ns::AutoReleasedError AutoError;
				NSUInteger ComputeOption = mtlpp::PipelineOption::NoPipelineOption;
#if ENABLE_METAL_GPUPROFILE
				{
					ns::String VertexName = vertexFunction.GetName();
					RenderPipelineDesc.SetLabel([NSString stringWithFormat:@"%@", VertexName.GetPtr()]);
				}
#endif
				if (GetMetalDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelFastValidation METAL_STATISTICS_ONLY(|| GetMetalDeviceContext().GetCommandQueue().GetStatistics()))
				{
					mtlpp::AutoReleasedComputePipelineReflection Reflection;
					ComputeOption = mtlpp::PipelineOption::ArgumentInfo|mtlpp::PipelineOption::BufferTypeInfo METAL_STATISTICS_ONLY(|NSUInteger(EMTLPipelineStats));
					Pipeline->ComputePipelineState = Device.NewComputePipelineState(ComputePipelineDesc, (mtlpp::PipelineOption)ComputeOption, &Reflection, &AutoError);
#if METAL_DEBUG_OPTIONS
					Pipeline->ComputePipelineReflection = Reflection;
#endif
				}
				else
				{
					Pipeline->ComputePipelineState = Device.NewComputePipelineState(ComputePipelineDesc, (mtlpp::PipelineOption)ComputeOption, nullptr, &AutoError);
				}
				Error = AutoError;
                
				UE_CLOG((Pipeline->ComputePipelineState == nil), LogMetal, Error, TEXT("Failed to generate a pipeline state object: %s"), *FString([Error.GetPtr() description]));
				UE_CLOG((Pipeline->ComputePipelineState == nil), LogMetal, Error, TEXT("Vertex shader: %s"), *FString(VertexShader->GetSourceCode()));
				UE_CLOG((Pipeline->ComputePipelineState == nil), LogMetal, Error, TEXT("Pixel shader: %s"), PixelShader ? *FString(PixelShader->GetSourceCode()) : TEXT("NULL"));
				UE_CLOG((Pipeline->ComputePipelineState == nil), LogMetal, Error, TEXT("Hull shader: %s"), *FString(HullShader->GetSourceCode()));
				UE_CLOG((Pipeline->ComputePipelineState == nil), LogMetal, Error, TEXT("Domain shader: %s"), *FString(DomainShader->GetSourceCode()));
				UE_CLOG((Pipeline->ComputePipelineState == nil), LogMetal, Error, TEXT("Descriptor: %s"), *FString(ComputePipelineDesc.GetPtr().description));
				UE_CLOG((Pipeline->ComputePipelineState == nil), LogMetal, Error, TEXT("Failed to generate a hull pipeline state object:\n\n %s\n\n"), *FString(Error.GetLocalizedDescription()));
				
#if METAL_DEBUG_OPTIONS
				if (Pipeline->ComputePipelineReflection)
				{
					Pipeline->ComputeDesc = ComputePipelineDesc;
					
					bool found__HSTFOut = false;
					for(mtlpp::Argument arg : Pipeline->ComputePipelineReflection.GetArguments())
					{
						bool addAttributes = false;
						mtlpp::VertexStepFunction StepFunction = (mtlpp::VertexStepFunction)-1; // invalid
						
						uint32 BufferIndex = UINT_MAX;
						
						if([arg.GetName() isEqualToString: @"PatchControlPointOutBuffer"])
						{
							check((arg.GetBufferAlignment() & (arg.GetBufferAlignment() - 1)) == 0); // must be pow2
							check((arg.GetBufferDataSize() & (arg.GetBufferAlignment() - 1)) == 0); // must be aligned
							
							check(arg.GetBufferDataSize() == VertexShader->TessellationOutputAttribs.PatchControlPointOutSize);
							
							addAttributes = true;
							BufferIndex = DomainShader->TessellationControlPointOutBuffer;
							StepFunction = mtlpp::VertexStepFunction::PerPatchControlPoint;
							check(arg.GetIndex() == VertexShader->TessellationControlPointOutBuffer);
						}
						else if([arg.GetName() isEqualToString: @"__HSOut"])
						{
							check((arg.GetBufferAlignment() & (arg.GetBufferAlignment() - 1)) == 0); // must be pow2
							check((arg.GetBufferDataSize() & (arg.GetBufferAlignment() - 1)) == 0); // must be aligned
							
							check(arg.GetBufferDataSize() == VertexShader->TessellationOutputAttribs.HSOutSize);
							
							addAttributes = true;
							BufferIndex = DomainShader->TessellationHSOutBuffer;
							StepFunction = mtlpp::VertexStepFunction::PerPatch;
							check(arg.GetIndex() == VertexShader->TessellationHSOutBuffer);
						}
						else if([arg.GetName() isEqualToString: @"__HSTFOut"])
						{
							found__HSTFOut = true;
							check((arg.GetBufferAlignment() & (arg.GetBufferAlignment() - 1)) == 0); // must be pow2
							check((arg.GetBufferDataSize() & (arg.GetBufferAlignment() - 1)) == 0); // must be aligned
							
							check(arg.GetBufferDataSize() == VertexShader->TessellationOutputAttribs.HSTFOutSize);
							
							check(arg.GetIndex() == VertexShader->TessellationHSTFOutBuffer);
						}
						else if([arg.GetName() isEqualToString:@"patchCount"])
						{
							check(arg.GetIndex() == VertexShader->TessellationPatchCountBuffer);
						}
						else if([arg.GetName() isEqualToString:@"indexBuffer"])
						{
							check(arg.GetIndex() == VertexShader->TessellationIndexBuffer);
						}
						
						// build the vertexDescriptor
						if(addAttributes)
						{
							check(DomainVertexLayouts[BufferIndex].GetStride() == arg.GetBufferDataSize());
							check(DomainVertexLayouts[BufferIndex].GetStepFunction() == StepFunction);
							check(DomainVertexLayouts[BufferIndex].GetStepRate() == 1);
							for(mtlpp::StructMember attribute : arg.GetBufferStructType().GetMembers())
							{
								int attributeIndex = -1;
								sscanf([attribute.GetName() UTF8String], "OUT_ATTRIBUTE%d_", &attributeIndex);
								check(attributeIndex >= 0 && attributeIndex <= 31);
								mtlpp::VertexFormat format = mtlpp::VertexFormat::Invalid;
								switch(attribute.GetDataType())
								{
									case mtlpp::DataType::Float:  format = mtlpp::VertexFormat::Float; break;
									case mtlpp::DataType::Float2: format = mtlpp::VertexFormat::Float2; break;
									case mtlpp::DataType::Float3: format = mtlpp::VertexFormat::Float3; break;
									case mtlpp::DataType::Float4: format = mtlpp::VertexFormat::Float4; break;
										
									case mtlpp::DataType::Int:  format = mtlpp::VertexFormat::Int; break;
									case mtlpp::DataType::Int2: format = mtlpp::VertexFormat::Int2; break;
									case mtlpp::DataType::Int3: format = mtlpp::VertexFormat::Int3; break;
									case mtlpp::DataType::Int4: format = mtlpp::VertexFormat::Int4; break;
										
									case mtlpp::DataType::UInt:  format = mtlpp::VertexFormat::UInt; break;
									case mtlpp::DataType::UInt2: format = mtlpp::VertexFormat::UInt2; break;
									case mtlpp::DataType::UInt3: format = mtlpp::VertexFormat::UInt3; break;
									case mtlpp::DataType::UInt4: format = mtlpp::VertexFormat::UInt4; break;
										
									default: check(0); // TODO support more cases
								}
								check(DomainVertexAttribs[attributeIndex].GetFormat() == format);
								check(DomainVertexAttribs[attributeIndex].GetOffset() == attribute.GetOffset());
								check(DomainVertexAttribs[attributeIndex].GetBufferIndex() == BufferIndex);
							}
						}
					}
					check(found__HSTFOut);
				}
#endif
            }
        }
#endif
        
        NSUInteger RenderOption = mtlpp::PipelineOption::NoPipelineOption;
		mtlpp::AutoReleasedRenderPipelineReflection* Reflection = nullptr;
		mtlpp::AutoReleasedRenderPipelineReflection OutReflection;
		Reflection = &OutReflection;
        if (GetMetalDeviceContext().GetCommandQueue().GetRuntimeDebuggingLevel() >= EMetalDebugLevelFastValidation METAL_STATISTICS_ONLY(|| GetMetalDeviceContext().GetCommandQueue().GetStatistics()))
        {
        	RenderOption = mtlpp::PipelineOption::ArgumentInfo|mtlpp::PipelineOption::BufferTypeInfo METAL_STATISTICS_ONLY(|NSUInteger(EMTLPipelineStats));
        }

		{
			ns::AutoReleasedError RenderError;
			METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewRenderPipeline: %s"), TEXT("")/**FString([RenderPipelineDesc.GetPtr() description])*/)));
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
		
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogMetal, Error, TEXT("Failed to generate a pipeline state object: %s"), *FString(Error.GetPtr().description));
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogMetal, Error, TEXT("Vertex shader: %s"), *FString(VertexShader->GetSourceCode()));
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogMetal, Error, TEXT("Pixel shader: %s"), PixelShader ? *FString(PixelShader->GetSourceCode()) : TEXT("NULL"));
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogMetal, Error, TEXT("Hull shader: %s"), HullShader ? *FString(HullShader->GetSourceCode()) : TEXT("NULL"));
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogMetal, Error, TEXT("Domain shader: %s"), DomainShader ? *FString(DomainShader->GetSourceCode()) : TEXT("NULL"));
#endif
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogMetal, Error, TEXT("Descriptor: %s"), *FString(RenderPipelineDesc.GetPtr().description));
		UE_CLOG((Pipeline->RenderPipelineState == nil), LogMetal, Error, TEXT("Failed to generate a render pipeline state object:\n\n %s\n\n"), *FString(Error.GetLocalizedDescription()));
		
		// We need to pass a failure up the chain, so we'll clean up here.
		if(Pipeline->RenderPipelineState == nil)
		{
			[Pipeline release];
			return nil;
		}
		
    #if METAL_DEBUG_OPTIONS
	#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
		Pipeline->ComputeSource = DomainShader ? VertexShader->GetSourceCode() : nil;
        Pipeline->VertexSource = DomainShader ? DomainShader->GetSourceCode() : VertexShader->GetSourceCode();
	#else
		Pipeline->VertexSource = VertexShader->GetSourceCode();
	#endif
        Pipeline->FragmentSource = PixelShader ? PixelShader->GetSourceCode() : nil;
    #endif
		
#if !PLATFORM_TVOS
		if (GMetalCommandBufferDebuggingEnabled)
		{
#if PLATFORM_MAC
			DebugPipelineDesc.SetVertexFunction(FMetalHelperFunctions::Get().GetDebugFunction());
			DebugPipelineDesc.SetRasterizationEnabled(false);
#else
			DebugPipelineDesc.SetTileFunction(FMetalHelperFunctions::Get().GetDebugFunction());
			DebugPipelineDesc.SetRasterSampleCount(RenderPipelineDesc.GetSampleCount());
			DebugPipelineDesc.SetThreadgroupSizeMatchesTileSize(false);
#endif
#if ENABLE_METAL_GPUPROFILE
			DebugPipelineDesc.SetLabel(@"Main_Debug");
#endif

			ns::AutoReleasedError RenderError;
			METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewDebugPipeline: %s"), TEXT("")/**FString([RenderPipelineDesc.GetPtr() description])*/)));
			Pipeline->DebugPipelineState = Device.NewRenderPipelineState(DebugPipelineDesc, mtlpp::PipelineOption::NoPipelineOption, Reflection, nullptr);
		}
#endif
        
#if METAL_DEBUG_OPTIONS
        if (GFrameCounter > 3)
        {
            UE_LOG(LogMetal, Verbose, TEXT("Created a hitchy pipeline state for hash %llx %llx %llx"), (uint64)Key.RenderPipelineHash.RasterBits, (uint64)(Key.RenderPipelineHash.TargetBits), (uint64)Key.VertexDescriptorHash.VertexDescHash);
        }
#endif
    }
	
	if (Pipeline && SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelFastValidation)
	{
		[Pipeline initResourceMask];
	}
	
    return !bSync ? nil : Pipeline;
}

static FMetalShaderPipeline* GetMTLRenderPipeline(bool const bSync, FMetalGraphicsPipelineState const* State, const FGraphicsPipelineStateInitializer& Init, EMetalIndexType const IndexType)
{
	return FMetalShaderPipelineCache::Get().GetRenderPipeline(bSync, State, Init, IndexType);
}

static void ReleaseMTLRenderPipeline(FMetalShaderPipeline* Pipeline)
{
	FMetalShaderPipelineCache::Get().ReleaseRenderPipeline(Pipeline);
}

bool FMetalGraphicsPipelineState::Compile()
{
	FMemory::Memzero(PipelineStates);
		for (uint32 i = 0; i < EMetalIndexType_Num; i++)
		{
			PipelineStates[i] = [GetMTLRenderPipeline(true, this, Initializer, (EMetalIndexType)i) retain];
			if(!PipelineStates[i])
			{
				return false;
			}
		}
	
	return true;
}

FMetalGraphicsPipelineState::~FMetalGraphicsPipelineState()
{
	for (uint32 i = 0; i < EMetalIndexType_Num; i++)
	{
		ReleaseMTLRenderPipeline(PipelineStates[i]);
		PipelineStates[i] = nil;
	}
}

FMetalShaderPipeline* FMetalGraphicsPipelineState::GetPipeline(EMetalIndexType IndexType)
{
	check(IndexType < EMetalIndexType_Num);

		if(!PipelineStates[IndexType])
		{
			PipelineStates[IndexType] = [GetMTLRenderPipeline(true, this, Initializer, IndexType) retain];
		}
	FMetalShaderPipeline* Pipe = PipelineStates[IndexType];

		check(Pipe);
    return Pipe;
}


FGraphicsPipelineStateRHIRef FMetalDynamicRHI::RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer)
{
	@autoreleasepool {
	FMetalGraphicsPipelineState* State = new FMetalGraphicsPipelineState(Initializer);
		
	if(!State->Compile())
	{
		// Compilation failures are propagated up to the caller.
		State->DoNoDeferDelete();
		delete State;
		return nullptr;
	}
	State->VertexDeclaration = ResourceCast(Initializer.BoundShaderState.VertexDeclarationRHI);
	State->VertexShader = ResourceCast(Initializer.BoundShaderState.VertexShaderRHI);
	State->PixelShader = ResourceCast(Initializer.BoundShaderState.PixelShaderRHI);
#if PLATFORM_SUPPORTS_TESSELLATION_SHADERS
	State->HullShader = ResourceCast(Initializer.BoundShaderState.HullShaderRHI);
	State->DomainShader = ResourceCast(Initializer.BoundShaderState.DomainShaderRHI);
#endif
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
	State->GeometryShader = ResourceCast(Initializer.BoundShaderState.GeometryShaderRHI);
#endif
	State->DepthStencilState = ResourceCast(Initializer.DepthStencilState);
	State->RasterizerState = ResourceCast(Initializer.RasterizerState);
	return State;
	}
}

TRefCountPtr<FRHIComputePipelineState> FMetalDynamicRHI::RHICreateComputePipelineState(FRHIComputeShader* ComputeShader)
{
	@autoreleasepool {
	return new FMetalComputePipelineState(ResourceCast(ComputeShader));
	}
}

FMetalPipelineStateCacheManager::FMetalPipelineStateCacheManager()
{
#if PLATFORM_IOS
	OnShaderPipelineCachePreOpenDelegate = FShaderPipelineCache::GetCachePreOpenDelegate().AddRaw(this, &FMetalPipelineStateCacheManager::OnShaderPipelineCachePreOpen);
	OnShaderPipelineCacheOpenedDelegate = FShaderPipelineCache::GetCacheOpenedDelegate().AddRaw(this, &FMetalPipelineStateCacheManager::OnShaderPipelineCacheOpened);
	OnShaderPipelineCachePrecompilationCompleteDelegate = FShaderPipelineCache::GetPrecompilationCompleteDelegate().AddRaw(this, &FMetalPipelineStateCacheManager::OnShaderPipelineCachePrecompilationComplete);
#endif
}

FMetalPipelineStateCacheManager::~FMetalPipelineStateCacheManager()
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

void FMetalPipelineStateCacheManager::OnShaderPipelineCachePreOpen(FString const& Name, EShaderPlatform Platform, bool& bReady)
{
	// only do this when haven't gotten a full pso cache already
	struct stat FileInfo;
	static FString PrivateWritePathBase = FString([NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) objectAtIndex:0]) + TEXT("/");
	FString Result = PrivateWritePathBase + FString([NSString stringWithFormat:@"/Caches/%@/com.apple.metal/functions.data", [NSBundle mainBundle].bundleIdentifier]);
	FString Result2 = PrivateWritePathBase + FString([NSString stringWithFormat:@"/Caches/%@/com.apple.metal/usecache.txt", [NSBundle mainBundle].bundleIdentifier]);
	if (stat(TCHAR_TO_UTF8(*Result), &FileInfo) != -1 && ((FileInfo.st_size / 1024 / 1024) > GMetalCacheMinSize) && stat(TCHAR_TO_UTF8(*Result2), &FileInfo) != -1)
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

void FMetalPipelineStateCacheManager::OnShaderPipelineCacheOpened(FString const& Name, EShaderPlatform Platform, uint32 Count, const FGuid& VersionGuid, FShaderPipelineCache::FShaderCachePrecompileContext& ShaderCachePrecompileContext)
{
	ShaderCachePrecompileContext.SetPrecompilationIsSlowTask();
}

void FMetalPipelineStateCacheManager::OnShaderPipelineCachePrecompilationComplete(uint32 Count, double Seconds, const FShaderPipelineCache::FShaderCachePrecompileContext& ShaderCachePrecompileContext)
{
	// Want to ignore any subsequent Shader Pipeline Cache opening/closing, eg when loading modules
	FShaderPipelineCache::GetCachePreOpenDelegate().Remove(OnShaderPipelineCachePreOpenDelegate);
	FShaderPipelineCache::GetCacheOpenedDelegate().Remove(OnShaderPipelineCacheOpenedDelegate);
	FShaderPipelineCache::GetPrecompilationCompleteDelegate().Remove(OnShaderPipelineCachePrecompilationCompleteDelegate);
	OnShaderPipelineCachePreOpenDelegate.Reset();
	OnShaderPipelineCacheOpenedDelegate.Reset();
	OnShaderPipelineCachePrecompilationCompleteDelegate.Reset();
}

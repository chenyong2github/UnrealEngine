// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalDynamicRHI.cpp: Metal Dynamic RHI Class Implementation.
=============================================================================*/


#include "MetalRHIPrivate.h"
#include "MetalRHIRenderQuery.h"
#include "MetalRHIStagingBuffer.h"
#include "MetalShaderTypes.h"
#include "MetalVertexDeclaration.h"
#include "MetalGraphicsPipelineState.h"
#include "MetalComputePipelineState.h"


//------------------------------------------------------------------------------

#pragma mark - Metal Dynamic RHI Vertex Declaration Methods -


FVertexDeclarationRHIRef FMetalDynamicRHI::RHICreateVertexDeclaration(const FVertexDeclarationElementList& Elements)
{
	@autoreleasepool {
		uint32 Key = FCrc::MemCrc32(Elements.GetData(), Elements.Num() * sizeof(FVertexElement));

		// look up an existing declaration
		FVertexDeclarationRHIRef* VertexDeclarationRefPtr = VertexDeclarationCache.Find(Key);
		if (VertexDeclarationRefPtr == NULL)
		{
			// create and add to the cache if it doesn't exist.
			VertexDeclarationRefPtr = &VertexDeclarationCache.Add(Key, new FMetalVertexDeclaration(Elements));
		}

		return *VertexDeclarationRefPtr;
	} // autoreleasepool
}


//------------------------------------------------------------------------------

#pragma mark - Metal Dynamic RHI Pipeline State Methods -


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
#endif // PLATFORM_SUPPORTS_TESSELLATION_SHADERS
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
		State->GeometryShader = ResourceCast(Initializer.BoundShaderState.GeometryShaderRHI);
#endif // PLATFORM_SUPPORTS_GEOMETRY_SHADERS

		State->DepthStencilState = ResourceCast(Initializer.DepthStencilState);
		State->RasterizerState = ResourceCast(Initializer.RasterizerState);

		return State;
	} // autoreleasepool
}

TRefCountPtr<FRHIComputePipelineState> FMetalDynamicRHI::RHICreateComputePipelineState(FRHIComputeShader* ComputeShader)
{
	@autoreleasepool {
		return new FMetalComputePipelineState(ResourceCast(ComputeShader));
	} // autoreleasepool
}


//------------------------------------------------------------------------------

#pragma mark - Metal Dynamic RHI Staging Buffer Methods -


FStagingBufferRHIRef FMetalDynamicRHI::RHICreateStagingBuffer()
{
	return new FMetalRHIStagingBuffer();
}

void* FMetalDynamicRHI::RHILockStagingBuffer(FRHIStagingBuffer* StagingBuffer, FRHIGPUFence* Fence, uint32 Offset, uint32 SizeRHI)
{
	FMetalRHIStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	return Buffer->Lock(Offset, SizeRHI);
}

void FMetalDynamicRHI::RHIUnlockStagingBuffer(FRHIStagingBuffer* StagingBuffer)
{
	FMetalRHIStagingBuffer* Buffer = ResourceCast(StagingBuffer);
	Buffer->Unlock();
}


//------------------------------------------------------------------------------

#pragma mark - Metal Dynamic RHI Render Query Methods -


FRenderQueryRHIRef FMetalDynamicRHI::RHICreateRenderQuery(ERenderQueryType QueryType)
{
	@autoreleasepool {
		FRenderQueryRHIRef Query = new FMetalRHIRenderQuery(QueryType);
		return Query;
	}
}

FRenderQueryRHIRef FMetalDynamicRHI::RHICreateRenderQuery_RenderThread(class FRHICommandListImmediate& RHICmdList, ERenderQueryType QueryType)
{
	@autoreleasepool {
		return GDynamicRHI->RHICreateRenderQuery(QueryType);
	}
}

bool FMetalDynamicRHI::RHIGetRenderQueryResult(FRHIRenderQuery* QueryRHI, uint64& OutNumPixels, bool bWait, uint32 GPUIndex)
{
	@autoreleasepool {
		check(IsInRenderingThread());
		FMetalRHIRenderQuery* Query = ResourceCast(QueryRHI);
		return Query->GetResult(OutNumPixels, bWait, GPUIndex);
	}
}

void FMetalDynamicRHI::RHICalibrateTimers()
{
#if defined(UE_MTL_RHI_SUPPORTS_CALIBRATE_TIMERS)
	check(IsInRenderingThread());
#if METAL_STATISTICS
	FMetalContext& Context = ImmediateContext.GetInternalContext();
	if (Context.GetCommandQueue().GetStatistics())
	{
		FScopedRHIThreadStaller StallRHIThread(FRHICommandListExecutor::GetImmediateCommandList());
		mtlpp::CommandBuffer Buffer = Context.GetCommandQueue().CreateCommandBuffer();

		id<IMetalStatisticsSamples> Samples = Context.GetCommandQueue().GetStatistics()->RegisterEncoderStatistics(Buffer.GetPtr(), EMetalSampleComputeEncoderStart);
		mtlpp::ComputeCommandEncoder Encoder = Buffer.ComputeCommandEncoder();
#if MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS
		FMetalComputeCommandEncoderDebugging Debugging;
		if (SafeGetRuntimeDebuggingLevel() >= EMetalDebugLevelFastValidation)
		{
			FMetalCommandBufferDebugging CmdDebug = FMetalCommandBufferDebugging::Get(Buffer);
			Debugging = FMetalComputeCommandEncoderDebugging(Encoder, CmdDebug);
		}
#endif // MTLPP_CONFIG_VALIDATE && METAL_DEBUG_OPTIONS

		Context.GetCommandQueue().GetStatistics()->RegisterEncoderStatistics(Buffer.GetPtr(), EMetalSampleComputeEncoderEnd);
		check(Samples);
		[Samples retain];
		Encoder.EndEncoding();
		METAL_DEBUG_LAYER(EMetalDebugLevelFastValidation, Debugging.EndEncoder());

		FMetalProfiler* Profiler = ImmediateContext.GetProfiler();
		Buffer.AddCompletedHandler(^(const mtlpp::CommandBuffer & theBuffer) {
			double GpuTimeSeconds = theBuffer.GetGpuStartTime();
			const double CyclesPerSecond = 1.0 / FPlatformTime::GetSecondsPerCycle();
			NSUInteger EndTime = GpuTimeSeconds * CyclesPerSecond;
			NSUInteger StatsTime = Samples.Array[0];
			Profiler->TimingSupport.SetCalibrationTimestamp(StatsTime / 1000, EndTime / 1000);
			[Samples release];
		});

		Context.GetCommandQueue().CommitCommandBuffer(Buffer);
		Buffer.WaitUntilCompleted();
	}
#endif // METAL_STATISTICS
#endif // UE_MTL_RHI_SUPPORTS_CALIBRATE_TIMERS
}

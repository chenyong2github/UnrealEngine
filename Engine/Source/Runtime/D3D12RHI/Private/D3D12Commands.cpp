// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
D3D12Commands.cpp: D3D RHI commands implementation.
=============================================================================*/

#include "D3D12RHIPrivate.h"
#include "StaticBoundShaderState.h"
#include "GlobalShader.h"
#include "OneColorShader.h"
#include "RHICommandList.h"
#include "RHIStaticStates.h"
#include "ShaderParameterUtils.h"
#include "ShaderCompiler.h"
#include "ScreenRendering.h"
#include "ResolveShader.h"
#include "SceneUtils.h"

int32 AFRSyncTemporalResources = 1;
static FAutoConsoleVariableRef CVarSyncTemporalResources(
	TEXT("D3D12.AFRSyncTemporalResources"),
	AFRSyncTemporalResources,
	TEXT("Synchronize inter-frame dependencies between GPUs"),
	ECVF_RenderThreadSafe
	);

using namespace D3D12RHI;

#define DECLARE_ISBOUNDSHADER(ShaderType) inline void ValidateBoundShader(FD3D12StateCache& InStateCache, FRHI##ShaderType* ShaderType##RHI) \
{ \
	FD3D12##ShaderType* CachedShader; \
	InStateCache.Get##ShaderType(&CachedShader); \
	FD3D12##ShaderType* ShaderType = FD3D12DynamicRHI::ResourceCast(ShaderType##RHI); \
	ensureMsgf(CachedShader == ShaderType, TEXT("Parameters are being set for a %s which is not currently bound"), TEXT( #ShaderType )); \
}

DECLARE_ISBOUNDSHADER(VertexShader)
DECLARE_ISBOUNDSHADER(PixelShader)
DECLARE_ISBOUNDSHADER(GeometryShader)
DECLARE_ISBOUNDSHADER(HullShader)
DECLARE_ISBOUNDSHADER(DomainShader)
DECLARE_ISBOUNDSHADER(ComputeShader)

#if EXECUTE_DEBUG_COMMAND_LISTS
bool GIsDoingQuery = false;
#endif

#if DO_CHECK
#define VALIDATE_BOUND_SHADER(s) ValidateBoundShader(StateCache, s)
#else
#define VALIDATE_BOUND_SHADER(s)
#endif

void FD3D12DynamicRHI::SetupRecursiveResources()
{
	auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	{
		TShaderMapRef<FLongGPUTaskPS> PixelShader(ShaderMap);
		PixelShader->GetPixelShader();
	}

	{
		TShaderMapRef<FLongGPUTaskPS> PixelShader(ShaderMap);
		PixelShader->GetPixelShader();
	}

	// TODO: Waiting to integrate MSAA fix for ResolveShader.h
	if (GMaxRHIShaderPlatform == SP_XBOXONE_D3D12)
		return;

	TShaderMapRef<FResolveVS> ResolveVertexShader(ShaderMap);
	if (GMaxRHIShaderPlatform == SP_PCD3D_SM5 || GMaxRHIShaderPlatform == SP_XBOXONE_D3D12)
	{
		TShaderMapRef<FResolveDepthPS> ResolvePixelShader_Depth(ShaderMap);
		ResolvePixelShader_Depth->GetPixelShader();

		TShaderMapRef<FResolveDepthPS> ResolvePixelShader_SingleSample(ShaderMap);
		ResolvePixelShader_SingleSample->GetPixelShader();
	}
	else
	{
		TShaderMapRef<FResolveDepthNonMSPS> ResolvePixelShader_DepthNonMS(ShaderMap);
		ResolvePixelShader_DepthNonMS->GetPixelShader();
	}
}

// Vertex state.
void FD3D12CommandContext::RHISetStreamSource(uint32 StreamIndex, FRHIVertexBuffer* VertexBufferRHI, uint32 Offset)
{
	FD3D12VertexBuffer* VertexBuffer = RetrieveObject<FD3D12VertexBuffer>(VertexBufferRHI);

	StateCache.SetStreamSource(VertexBuffer ? &VertexBuffer->ResourceLocation : nullptr, StreamIndex, Offset);
}

void FD3D12CommandContext::RHISetComputeShader(FRHIComputeShader* ComputeShaderRHI)
{
	// TODO: Eventually the high-level should just use RHISetComputePipelineState() directly similar to how graphics PSOs are handled.
	FD3D12ComputePipelineState* const ComputePipelineState = FD3D12DynamicRHI::ResourceCast(RHICreateComputePipelineState(ComputeShaderRHI).GetReference());
	RHISetComputePipelineState(ComputePipelineState);
}

void FD3D12CommandContextBase::RHIWaitComputeFence(FRHIComputeFence* InFenceRHI)
{
	FD3D12Fence* Fence = FD3D12DynamicRHI::ResourceCast(InFenceRHI);

	if (Fence)
	{
		check(IsDefaultContext());
		RHISubmitCommandsHint();

		checkf(Fence->GetWriteEnqueued(), TEXT("ComputeFence: %s waited on before being written. This will hang the GPU."), *Fence->GetName().ToString());

		Fence->GpuWait(bIsAsyncComputeContext ? ED3D12CommandQueueType::Async : ED3D12CommandQueueType::Default , Fence->GetLastSignaledFence());
	}
}

void FD3D12CommandContext::RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
{
	FD3D12ComputeShader* ComputeShader = nullptr;
	StateCache.GetComputeShader(&ComputeShader);

	if (IsDefaultContext())
	{
		GetParentDevice()->RegisterGPUDispatch(FIntVector(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ));	
	}

	if (ComputeShader->ResourceCounts.bGlobalUniformBufferUsed)
	{
		CommitComputeShaderConstants();
	}
	CommitComputeResourceTables(ComputeShader);
	StateCache.ApplyState<D3D12PT_Compute>();

	numDispatches++;
	CommandListHandle->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);

	DEBUG_EXECUTE_COMMAND_LIST(this);
}

void FD3D12CommandContext::RHIDispatchIndirectComputeShader(FRHIVertexBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{
	FD3D12VertexBuffer* ArgumentBuffer = FD3D12DynamicRHI::ResourceCast(ArgumentBufferRHI);

	if (IsDefaultContext())
	{
		GetParentDevice()->RegisterGPUDispatch(FIntVector(1, 1, 1));	
	}

	FD3D12ComputeShader* ComputeShader = nullptr;
	StateCache.GetComputeShader(&ComputeShader);

	if (ComputeShader->ResourceCounts.bGlobalUniformBufferUsed)
	{
		CommitComputeShaderConstants();
	}
	CommitComputeResourceTables(ComputeShader);

	FD3D12ResourceLocation& Location = ArgumentBuffer->ResourceLocation;
	FD3D12DynamicRHI::TransitionResource(CommandListHandle, Location.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	StateCache.ApplyState<D3D12PT_Compute>();

	numDispatches++;
	CommandListHandle->ExecuteIndirect(
		GetParentDevice()->GetParentAdapter()->GetDispatchIndirectCommandSignature(),
		1,
		Location.GetResource()->GetResource(),
		Location.GetOffsetFromBaseOfResource() + ArgumentOffset,
		NULL,
		0
		);
	CommandListHandle.UpdateResidency(Location.GetResource());

	DEBUG_EXECUTE_COMMAND_LIST(this);
}


void FD3D12CommandContext::RHITransitionResources(EResourceTransitionAccess TransitionType, FRHITexture** InTextures, int32 NumTextures)
{
#if !USE_D3D12RHI_RESOURCE_STATE_TRACKING
	// TODO: Make sure that EMetaData is supported with an aliasing barrier, otherwise the CMask decal optimisation will break.
	check(TransitionType != EResourceTransitionAccess::EMetaData && (TransitionType == EResourceTransitionAccess::EReadable || TransitionType == EResourceTransitionAccess::EWritable || TransitionType == EResourceTransitionAccess::ERWSubResBarrier));
	// TODO: Remove this skip.
	// Skip for now because we don't have enough info about what mip to transition yet.
	// Note: This causes visual corruption.
	if (TransitionType == EResourceTransitionAccess::ERWSubResBarrier)
	{
		return;
	}

	static IConsoleVariable* CVarShowTransitions = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.ShowTransitions"));
	const bool bShowTransitionEvents = CVarShowTransitions->GetInt() != 0;

	SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(*this, RHITransitionResources, bShowTransitionEvents, TEXT("TransitionTo: %s: %i Textures"), *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)TransitionType], NumTextures);

	// Determine the direction of the transitions.
	const D3D12_RESOURCE_STATES* pBefore = nullptr;
	const D3D12_RESOURCE_STATES* pAfter = nullptr;
	D3D12_RESOURCE_STATES WritableState;
	D3D12_RESOURCE_STATES ReadableState;
	switch (TransitionType)
	{
	case EResourceTransitionAccess::EReadable:
		// Write -> Read
		pBefore = &WritableState;
		pAfter = &ReadableState;
		break;

	case EResourceTransitionAccess::EWritable:
		// Read -> Write
		pBefore = &ReadableState;
		pAfter = &WritableState;
		break;

	default:
		check(false);
		break;
	}

	// Create the resource barrier descs for each texture to transition.
	for (int32 i = 0; i < NumTextures; ++i)
	{
		if (InTextures[i])
		{
			FD3D12Resource* Resource = RetrieveTextureBase(InTextures[i])->GetResource();
			check(Resource->RequiresResourceStateTracking());

			SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(*this, RHITransitionResourcesLoop, bShowTransitionEvents, TEXT("To:%i - %s"), i, *Resource->GetName().ToString());

			WritableState = Resource->GetWritableState();
			ReadableState = Resource->GetReadableState();

			CommandListHandle.AddTransitionBarrier(Resource, *pBefore, *pAfter, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

			DUMP_TRANSITION(Resource->GetName(), TransitionType);
		}
	}
#else
	if (TransitionType == EResourceTransitionAccess::EMetaData)
	{
		FlushMetadata(InTextures, NumTextures);
	}
#endif // !USE_D3D12RHI_RESOURCE_STATE_TRACKING
}


void FD3D12CommandContext::RHITransitionResources(EResourceTransitionAccess TransitionType, EResourceTransitionPipeline TransitionPipeline, FRHIUnorderedAccessView** InUAVs, int32 InNumUAVs, FRHIComputeFence* WriteComputeFenceRHI)
{
	static IConsoleVariable* CVarShowTransitions = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ProfileGPU.ShowTransitions"));
	const bool bShowTransitionEvents = CVarShowTransitions->GetInt() != 0;

	SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(*this, RHITransitionResources, bShowTransitionEvents, TEXT("TransitionTo: %s: %i UAVs"), *FResourceTransitionUtility::ResourceTransitionAccessStrings[(int32)TransitionType], InNumUAVs);
	const bool bTransitionBetweenShaderStages = (TransitionPipeline == EResourceTransitionPipeline::EGfxToCompute) || (TransitionPipeline == EResourceTransitionPipeline::EComputeToGfx);
	const bool bUAVTransition = (TransitionType == EResourceTransitionAccess::EReadable) || (TransitionType == EResourceTransitionAccess::EWritable || TransitionType == EResourceTransitionAccess::ERWBarrier);
	
	// When transitioning between shader stage usage, we can avoid a UAV barrier as an optimization if the resource will be transitioned to a different resource state anyway (E.g RT -> UAV).
	// That being said, there is a danger when going from UAV usage on one stage (E.g. Pixel Shader UAV) to UAV usage on another stage (E.g. Compute Shader UAV), 
	// IFF the 2nd UAV usage relies on the output of the 1st. That would require a UAV barrier since the D3D12 RHI state tracking system would optimize that transition out.
	// The safest option is to always do a UAV barrier when ERWBarrier is passed in. However there is currently no usage like this so we're ok for now. 
	const bool bUAVBarrier = (TransitionType == EResourceTransitionAccess::ERWBarrier && !bTransitionBetweenShaderStages);

	if (bUAVBarrier)
	{
		// UAV barrier between Dispatch() calls to ensure all R/W accesses are complete.
		StateCache.FlushComputeShaderCache(true);
	}
	else if (bUAVTransition)
	{
		// We do a special transition now when called with a particular set of parameters (ERWBarrier && EGfxToCompute) as an optimization when the engine wants to use uavs on the async compute queue.
		// This will transition all specifed UAVs to the UAV state on the 3D queue to avoid stalling the compute queue with pending resource state transitions later.
		if ((TransitionType == EResourceTransitionAccess::ERWBarrier) && (TransitionPipeline == EResourceTransitionPipeline::EGfxToCompute))
		{
			// The 3D queue can safely transition resources to the UAV state, regardless of their current state (RT, SRV, etc.). However the compute queue is limited in what states 
			// it can transition to/from, so we limit this transition logic to only happen when going from Gfx -> Compute. (E.g. The compute queue cannot transition to/from RT, Pixel Shader SRV, etc.).
			for (int32 i = 0; i < InNumUAVs; ++i)
			{
				if (InUAVs[i])
				{
					FD3D12UnorderedAccessView* const UnorderedAccessView = RetrieveObject<FD3D12UnorderedAccessView>(InUAVs[i]);
					FD3D12DynamicRHI::TransitionResource(CommandListHandle, UnorderedAccessView, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				}
			}
		}
		else
		{
#if USE_D3D12RHI_RESOURCE_STATE_TRACKING
			if (TransitionType == EResourceTransitionAccess::EReadable)
			{
				const D3D12_COMMAND_LIST_TYPE CmdListType = CommandListHandle.GetCommandListType();

				// Compute pipeline can't transition to graphics states such as PIXEL_SHADER_RESOURCE. Best bet is to transition to COMMON given that we're going to consume it on a different queue anyway.
				// Technically we should be able to transition NON_PIXEL_SHADER_RESOURCE on ComputeToCompute cases, but it appears an AMD driver issue is causing that to hang the GPU, so we're using COMMON
				// in that case also, which is not ideal, but avoids the hang.
				D3D12_RESOURCE_STATES AfterState = (CmdListType == D3D12_COMMAND_LIST_TYPE_DIRECT && TransitionPipeline == EResourceTransitionPipeline::EComputeToGfx)?
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_COMMON;

				for (int32 i = 0; i < InNumUAVs; ++i)
				{
					if (InUAVs[i])
					{
						FD3D12UnorderedAccessView* const UnorderedAccessView = RetrieveObject<FD3D12UnorderedAccessView>(InUAVs[i]);
						FD3D12DynamicRHI::TransitionResource(CommandListHandle, UnorderedAccessView, AfterState);
					}
				}
			}
#else
			// Determine the direction of the transitions.
			// Note in this method, the writeable state is always UAV, regardless of the FD3D12Resource's Writeable state.
			const D3D12_RESOURCE_STATES* pBefore = nullptr;
			const D3D12_RESOURCE_STATES* pAfter = nullptr;
			const D3D12_RESOURCE_STATES WritableComputeState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
			D3D12_RESOURCE_STATES WritableGraphicsState;
			D3D12_RESOURCE_STATES ReadableState;
			switch (TransitionType)
			{
			case EResourceTransitionAccess::EReadable:
				// Write -> Read
				pBefore = &WritableComputeState;
				pAfter = &ReadableState;
				break;

			case EResourceTransitionAccess::EWritable:
				// Read -> Write
				pBefore = &ReadableState;
				pAfter = &WritableComputeState;
				break;

			case EResourceTransitionAccess::ERWBarrier:
				// Write -> Write, but switching from Grfx to Compute.
				check(TransitionPipeline == EResourceTransitionPipeline::EGfxToCompute);
				pBefore = &WritableGraphicsState;
				pAfter = &WritableComputeState;
				break;

			default:
				check(false);
				break;
			}

			// Create the resource barrier descs for each texture to transition.
			for (int32 i = 0; i < InNumUAVs; ++i)
			{
				if (InUAVs[i])
				{
					FD3D12UnorderedAccessView* UnorderedAccessView = RetrieveObject<FD3D12UnorderedAccessView>(InUAVs[i]);
					FD3D12Resource* Resource = UnorderedAccessView->GetResource();
					check(Resource->RequiresResourceStateTracking());

					SCOPED_RHI_CONDITIONAL_DRAW_EVENTF(*this, RHITransitionResourcesLoop, bShowTransitionEvents, TEXT("To:%i - %s"), i, *Resource->GetName().ToString());

					// The writable compute state is always UAV.
					WritableGraphicsState = Resource->GetWritableState();
					ReadableState = Resource->GetReadableState();

					// Some ERWBarriers might have the same before and after states.
					if (*pBefore != *pAfter)
					{
						CommandListHandle.AddTransitionBarrier(Resource, *pBefore, *pAfter, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

						DUMP_TRANSITION(Resource->GetName(), TransitionType);
					}
				}
			}
#endif // USE_D3D12RHI_RESOURCE_STATE_TRACKING
		}
	}

	if (WriteComputeFenceRHI)
	{
		RHISubmitCommandsHint();

		FD3D12Fence* Fence = FD3D12DynamicRHI::ResourceCast(WriteComputeFenceRHI);
		Fence->WriteFence();

		Fence->Signal(bIsAsyncComputeContext ? ED3D12CommandQueueType::Async : ED3D12CommandQueueType::Default);
	}
}

void FD3D12CommandContext::RHISetGlobalUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)
{
	FMemory::Memzero(GlobalUniformBuffers.GetData(), GlobalUniformBuffers.Num() * sizeof(FRHIUniformBuffer*));

	for (int32 Index = 0; Index < InUniformBuffers.GetUniformBufferCount(); ++Index)
	{
		GlobalUniformBuffers[InUniformBuffers.GetSlot(Index)] = InUniformBuffers.GetUniformBuffer(Index);
	}
}

void FD3D12CommandContext::RHICopyToStagingBuffer(FRHIVertexBuffer* SourceBufferRHI, FRHIStagingBuffer* StagingBufferRHI, uint32 Offset, uint32 NumBytes)
{
	FD3D12StagingBuffer* StagingBuffer = FD3D12DynamicRHI::ResourceCast(StagingBufferRHI);
	check(StagingBuffer);
	ensureMsgf(!StagingBuffer->bIsLocked, TEXT("Attempting to Copy to a locked staging buffer. This may have undefined behavior"));

	FD3D12VertexBuffer* VertexBuffer = FD3D12DynamicRHI::ResourceCast(SourceBufferRHI);
	check(VertexBuffer);

	// Only get data from the first gpu for now.
	FD3D12Device* StagingDevice = VertexBuffer->GetParentDevice();

	// Ensure our shadow buffer is large enough to hold the readback.
	if (!StagingBuffer->StagedRead || StagingBuffer->ShadowBufferSize < NumBytes)
	{
		// @todo-mattc I feel like we should allocate more than NumBytes to handle small reads without blowing tons of space. Need to pool this.
		// Hopefully d3d12 will do smart pooling out of an internal heap.
		StagingBuffer->SafeRelease();

		VERIFYD3D12RESULT(GetParentDevice()->GetParentAdapter()->CreateBuffer(D3D12_HEAP_TYPE_READBACK, GetGPUMask(), GetGPUMask(), NumBytes, &StagingBuffer->StagedRead, TEXT("StagedRead")));
		StagingBuffer->ShadowBufferSize = NumBytes;
	}

	{
		FD3D12Resource* pSourceResource = VertexBuffer->ResourceLocation.GetResource();
		D3D12_RESOURCE_DESC const& SourceBufferDesc = pSourceResource->GetDesc();

		FD3D12Resource* pDestResource = StagingBuffer->StagedRead;
		D3D12_RESOURCE_DESC const& DestBufferDesc = pDestResource->GetDesc();

		FD3D12DynamicRHI::TransitionResource(CommandListHandle, pSourceResource, D3D12_RESOURCE_STATE_COPY_SOURCE, 0);
		CommandListHandle.FlushResourceBarriers();	// Must flush so the desired state is actually set.

		numCopies++;

		CommandListHandle->CopyBufferRegion(pDestResource->GetResource(), 0, pSourceResource->GetResource(), Offset, NumBytes);
		CommandListHandle.UpdateResidency(pDestResource);
		CommandListHandle.UpdateResidency(pSourceResource);
	}
}

void FD3D12CommandContext::RHIWriteGPUFence(FRHIGPUFence* FenceRHI)
{
	check(FenceRHI)

	// @todo-mattc we don't want to flush here. That should be the caller's responsibility.
	RHISubmitCommandsHint();
	FD3D12GPUFence* Fence = FD3D12DynamicRHI::ResourceCast(FenceRHI);
	Fence->WriteInternal(ED3D12CommandQueueType::Default);
}

void FD3D12CommandContext::RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ)
{
	// These are the maximum viewport extents for D3D12. Exceeding them leads to badness.
	check(MinX <= (uint32)D3D12_VIEWPORT_BOUNDS_MAX);
	check(MinY <= (uint32)D3D12_VIEWPORT_BOUNDS_MAX);
	check(MaxX <= (uint32)D3D12_VIEWPORT_BOUNDS_MAX);
	check(MaxY <= (uint32)D3D12_VIEWPORT_BOUNDS_MAX);

	D3D12_VIEWPORT Viewport = { MinX, MinY, (MaxX - MinX), (MaxY - MinY), MinZ, MaxZ };
	//avoid setting a 0 extent viewport, which the debug runtime doesn't like
	if (Viewport.Width > 0 && Viewport.Height > 0)
	{
		// Setting a viewport will also set the scissor rect appropriately.
		StateCache.SetViewport(Viewport);
		RHISetScissorRect(true, MinX, MinY, MaxX, MaxY);
	}
}

void FD3D12CommandContext::RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY)
{
	if (bEnable)
	{
		const CD3DX12_RECT ScissorRect(MinX, MinY, MaxX, MaxY);
		StateCache.SetScissorRect(ScissorRect);
	}
	else
	{
		const D3D12_VIEWPORT& Viewport = StateCache.GetViewport();
		const CD3DX12_RECT ScissorRect((LONG) Viewport.TopLeftX, (LONG) Viewport.TopLeftY, (LONG) Viewport.TopLeftX + (LONG) Viewport.Width, (LONG) Viewport.TopLeftY + (LONG) Viewport.Height);
		StateCache.SetScissorRect(ScissorRect);
	}
}

void FD3D12CommandContext::RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState)
{
	FD3D12GraphicsPipelineState* GraphicsPipelineState = FD3D12DynamicRHI::ResourceCast(GraphicsState);

	// TODO: [PSO API] Every thing inside this scope is only necessary to keep the PSO shadow in sync while we convert the high level to only use PSOs
	const bool bWasUsingTessellation = bUsingTessellation;
	bUsingTessellation = GraphicsPipelineState->GetHullShader() && GraphicsPipelineState->GetDomainShader();
	// Ensure the command buffers are reset to reduce the amount of data that needs to be versioned.
	VSConstantBuffer.Reset();
	PSConstantBuffer.Reset();
	HSConstantBuffer.Reset();
	DSConstantBuffer.Reset();
	GSConstantBuffer.Reset();
	// Should this be here or in RHISetComputeShader? Might need a new bDiscardSharedConstants for CS.
	CSConstantBuffer.Reset();
	// @TODO : really should only discard the constants if the shader state has actually changed.
	bDiscardSharedConstants = true;

	if (!GraphicsPipelineState->PipelineStateInitializer.bDepthBounds)
	{
		StateCache.SetDepthBounds(0.0f, 1.0f);
	}

	StateCache.SetGraphicsPipelineState(GraphicsPipelineState, bUsingTessellation != bWasUsingTessellation);
	StateCache.SetStencilRef(0);

	ApplyGlobalUniformBuffers(GraphicsPipelineState->GetVertexShader());
	ApplyGlobalUniformBuffers(GraphicsPipelineState->GetHullShader());
	ApplyGlobalUniformBuffers(GraphicsPipelineState->GetDomainShader());
	ApplyGlobalUniformBuffers(GraphicsPipelineState->GetGeometryShader());
	ApplyGlobalUniformBuffers(GraphicsPipelineState->GetPixelShader());
}

void FD3D12CommandContext::RHISetComputePipelineState(FRHIComputePipelineState* ComputeState)
{
#if D3D12_RHI_RAYTRACING
	StateCache.TransitionComputeState(D3D12PT_Compute);
#endif

	FD3D12ComputePipelineState* ComputePipelineState = FD3D12DynamicRHI::ResourceCast(ComputeState);

	// TODO: [PSO API] Every thing inside this scope is only necessary to keep the PSO shadow in sync while we convert the high level to only use PSOs
	{
		StateCache.SetComputeShader(ComputePipelineState->ComputeShader);
	}

	StateCache.SetComputePipelineState(ComputePipelineState);

	ApplyGlobalUniformBuffers(ComputePipelineState->ComputeShader.GetReference());
}

void FD3D12CommandContext::RHISetShaderTexture(FRHIGraphicsShader* ShaderRHI, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	FD3D12TextureBase* const NewTexture = RetrieveTextureBase(NewTextureRHI);
	switch (ShaderRHI->GetType())
	{
	case FRHIShader::EType::Vertex:
	{
		FRHIVertexShader* VertexShaderRHI = static_cast<FRHIVertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShaderRHI);
		StateCache.SetShaderResourceView<SF_Vertex>(NewTexture ? NewTexture->GetShaderResourceView() : nullptr, TextureIndex);
	}
	break;
	case FRHIShader::EType::Hull:
	{
		FRHIHullShader* HullShaderRHI = static_cast<FRHIHullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShaderRHI);
		StateCache.SetShaderResourceView<SF_Hull>(NewTexture ? NewTexture->GetShaderResourceView() : nullptr, TextureIndex);
	}
	break;
	case FRHIShader::EType::Domain:
	{
		FRHIDomainShader* DomainShaderRHI = static_cast<FRHIDomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShaderRHI);
		StateCache.SetShaderResourceView<SF_Domain>(NewTexture ? NewTexture->GetShaderResourceView() : nullptr, TextureIndex);
	}
	break;
	case FRHIShader::EType::Geometry:
	{
		FRHIGeometryShader* GeometryShaderRHI = static_cast<FRHIGeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShaderRHI);
		StateCache.SetShaderResourceView<SF_Geometry>(NewTexture ? NewTexture->GetShaderResourceView() : nullptr, TextureIndex);
	}
	break;
	case FRHIShader::EType::Pixel:
	{
		FRHIPixelShader* PixelShaderRHI = static_cast<FRHIPixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShaderRHI);
		StateCache.SetShaderResourceView<SF_Pixel>(NewTexture ? NewTexture->GetShaderResourceView() : nullptr, TextureIndex);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetType());
	}
}

void FD3D12CommandContext::RHISetShaderTexture(FRHIComputeShader* ComputeShaderRHI, uint32 TextureIndex, FRHITexture* NewTextureRHI)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);
	FD3D12TextureBase* const NewTexture = RetrieveTextureBase(NewTextureRHI);
	StateCache.SetShaderResourceView<SF_Compute>(NewTexture ? NewTexture->GetShaderResourceView() : nullptr, TextureIndex);
}

void FD3D12CommandContext::RHISetUAVParameter(FRHIPixelShader* PixelShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI)
{
	FD3D12UnorderedAccessView* UAV = RetrieveObject<FD3D12UnorderedAccessView>(UAVRHI);

	if (UAV)
	{
		ConditionalClearShaderResource(UAV->GetResourceLocation());
	}

	uint32 InitialCount = -1;

	// Actually set the UAV
	StateCache.SetUAVs<SF_Pixel>(UAVIndex, 1, &UAV, &InitialCount);
}


void FD3D12CommandContext::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);

	FD3D12UnorderedAccessView* UAV = RetrieveObject<FD3D12UnorderedAccessView>(UAVRHI);

	if (UAV)
	{
		ConditionalClearShaderResource(UAV->GetResourceLocation());
	}

	uint32 InitialCount = -1;

	// Actually set the UAV
	StateCache.SetUAVs<SF_Compute>(UAVIndex, 1, &UAV, &InitialCount);
}

void FD3D12CommandContext::RHISetUAVParameter(FRHIComputeShader* ComputeShaderRHI, uint32 UAVIndex, FRHIUnorderedAccessView* UAVRHI, uint32 InitialCount)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);

	FD3D12UnorderedAccessView* UAV = RetrieveObject<FD3D12UnorderedAccessView>(UAVRHI);

	if (UAV)
	{
		ConditionalClearShaderResource(UAV->GetResourceLocation());
	}

	StateCache.SetUAVs<SF_Compute>(UAVIndex, 1, &UAV, &InitialCount);
}

void FD3D12CommandContext::RHISetShaderResourceViewParameter(FRHIGraphicsShader* ShaderRHI, uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	FD3D12ShaderResourceView* const SRV = RetrieveObject<FD3D12ShaderResourceView>(SRVRHI);
	switch (ShaderRHI->GetType())
	{
	case FRHIShader::EType::Vertex:
	{
		FRHIVertexShader* VertexShaderRHI = static_cast<FRHIVertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShaderRHI);
		StateCache.SetShaderResourceView<SF_Vertex>(SRV, TextureIndex);
	}
	break;
	case FRHIShader::EType::Hull:
	{
		FRHIHullShader* HullShaderRHI = static_cast<FRHIHullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShaderRHI);
		StateCache.SetShaderResourceView<SF_Hull>(SRV, TextureIndex);
	}
	break;
	case FRHIShader::EType::Domain:
	{
		FRHIDomainShader* DomainShaderRHI = static_cast<FRHIDomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShaderRHI);
		StateCache.SetShaderResourceView<SF_Domain>(SRV, TextureIndex);
	}
	break;
	case FRHIShader::EType::Geometry:
	{
		FRHIGeometryShader* GeometryShaderRHI = static_cast<FRHIGeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShaderRHI);
		StateCache.SetShaderResourceView<SF_Geometry>(SRV, TextureIndex);
	}
	break;
	case FRHIShader::EType::Pixel:
	{
		FRHIPixelShader* PixelShaderRHI = static_cast<FRHIPixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShaderRHI);
		StateCache.SetShaderResourceView<SF_Pixel>(SRV, TextureIndex);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetType());
	}

}

void FD3D12CommandContext::RHISetShaderResourceViewParameter(FRHIComputeShader* ComputeShaderRHI, uint32 TextureIndex, FRHIShaderResourceView* SRVRHI)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);
	FD3D12ShaderResourceView* const SRV = RetrieveObject<FD3D12ShaderResourceView>(SRVRHI);
	StateCache.SetShaderResourceView<SF_Compute>(SRV, TextureIndex);
}

void FD3D12CommandContext::RHISetShaderSampler(FRHIGraphicsShader* ShaderRHI, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	FD3D12SamplerState* NewState = RetrieveObject<FD3D12SamplerState>(NewStateRHI);
	switch (ShaderRHI->GetType())
	{
	case FRHIShader::EType::Vertex:
	{
		FRHIVertexShader* VertexShaderRHI = static_cast<FRHIVertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShaderRHI);
		StateCache.SetSamplerState<SF_Vertex>(NewState, SamplerIndex);
	}
	break;
	case FRHIShader::EType::Hull:
	{
		FRHIHullShader* HullShaderRHI = static_cast<FRHIHullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShaderRHI);
		StateCache.SetSamplerState<SF_Hull>(NewState, SamplerIndex);
	}
	break;
	case FRHIShader::EType::Domain:
	{
		FRHIDomainShader* DomainShaderRHI = static_cast<FRHIDomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShaderRHI);
		StateCache.SetSamplerState<SF_Domain>(NewState, SamplerIndex);
	}
	break;
	case FRHIShader::EType::Geometry:
	{
		FRHIGeometryShader* GeometryShaderRHI = static_cast<FRHIGeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShaderRHI);
		StateCache.SetSamplerState<SF_Geometry>(NewState, SamplerIndex);
	}
	break;
	case FRHIShader::EType::Pixel:
	{
		FRHIPixelShader* PixelShaderRHI = static_cast<FRHIPixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShaderRHI);
		StateCache.SetSamplerState<SF_Pixel>(NewState, SamplerIndex);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetType());
	}
}

void FD3D12CommandContext::RHISetShaderSampler(FRHIComputeShader* ComputeShaderRHI, uint32 SamplerIndex, FRHISamplerState* NewStateRHI)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);
	FD3D12SamplerState* NewState = RetrieveObject<FD3D12SamplerState>(NewStateRHI);
	StateCache.SetSamplerState<SF_Compute>(NewState, SamplerIndex);
}

void FD3D12CommandContext::RHISetShaderUniformBuffer(FRHIGraphicsShader* ShaderRHI, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	//SCOPE_CYCLE_COUNTER(STAT_D3D12SetShaderUniformBuffer);
	FD3D12UniformBuffer* Buffer = RetrieveObject<FD3D12UniformBuffer>(BufferRHI);
	EShaderFrequency Stage = SF_NumFrequencies;
	switch (ShaderRHI->GetType())
	{
	case FRHIShader::EType::Vertex:
	{
		FRHIVertexShader* VertexShaderRHI = static_cast<FRHIVertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShaderRHI);
		StateCache.SetConstantsFromUniformBuffer<SF_Vertex>(BufferIndex, Buffer);
		Stage = SF_Vertex;
	}
	break;
	case FRHIShader::EType::Hull:
	{
		FRHIHullShader* HullShaderRHI = static_cast<FRHIHullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShaderRHI);
		StateCache.SetConstantsFromUniformBuffer<SF_Hull>(BufferIndex, Buffer);
		Stage = SF_Hull;
	}
	break;
	case FRHIShader::EType::Domain:
	{
		FRHIDomainShader* DomainShaderRHI = static_cast<FRHIDomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShaderRHI);
		StateCache.SetConstantsFromUniformBuffer<SF_Domain>(BufferIndex, Buffer);
		Stage = SF_Domain;
	}
	break;
	case FRHIShader::EType::Geometry:
	{
		FRHIGeometryShader* GeometryShaderRHI = static_cast<FRHIGeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShaderRHI);
		StateCache.SetConstantsFromUniformBuffer<SF_Geometry>(BufferIndex, Buffer);
		Stage = SF_Geometry;
	}
	break;
	case FRHIShader::EType::Pixel:
	{
		FRHIPixelShader* PixelShaderRHI = static_cast<FRHIPixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShaderRHI);
		StateCache.SetConstantsFromUniformBuffer<SF_Pixel>(BufferIndex, Buffer);
		Stage = SF_Pixel;
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetType());
		return;
	}

	if (!GRHINeedsExtraDeletionLatency)
	{
		BoundUniformBufferRefs[Stage][BufferIndex] = BufferRHI;
	}

	BoundUniformBuffers[Stage][BufferIndex] = Buffer;
	DirtyUniformBuffers[Stage] |= (1 << BufferIndex);
}

void FD3D12CommandContext::RHISetShaderUniformBuffer(FRHIComputeShader* ComputeShader, uint32 BufferIndex, FRHIUniformBuffer* BufferRHI)
{
	//SCOPE_CYCLE_COUNTER(STAT_D3D12SetShaderUniformBuffer);
	//VALIDATE_BOUND_SHADER(ComputeShader);
	FD3D12UniformBuffer* Buffer = RetrieveObject<FD3D12UniformBuffer>(BufferRHI);

	StateCache.SetConstantsFromUniformBuffer<SF_Compute>(BufferIndex, Buffer);

	if (!GRHINeedsExtraDeletionLatency)
	{
		BoundUniformBufferRefs[SF_Compute][BufferIndex] = BufferRHI;
	}

	BoundUniformBuffers[SF_Compute][BufferIndex] = Buffer;
	DirtyUniformBuffers[SF_Compute] |= (1 << BufferIndex);
}

void FD3D12CommandContext::RHISetShaderParameter(FRHIGraphicsShader* ShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	checkSlow(BufferIndex == 0);

	switch (ShaderRHI->GetType())
	{
	case FRHIShader::EType::Vertex:
	{
		FRHIVertexShader* VertexShaderRHI = static_cast<FRHIVertexShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(VertexShaderRHI);
		VSConstantBuffer.UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	case FRHIShader::EType::Hull:
	{
		FRHIHullShader* HullShaderRHI = static_cast<FRHIHullShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(HullShaderRHI);
		HSConstantBuffer.UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	case FRHIShader::EType::Domain:
	{
		FRHIDomainShader* DomainShaderRHI = static_cast<FRHIDomainShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(DomainShaderRHI);
		DSConstantBuffer.UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	case FRHIShader::EType::Geometry:
	{
		FRHIGeometryShader* GeometryShaderRHI = static_cast<FRHIGeometryShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(GeometryShaderRHI);
		GSConstantBuffer.UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	case FRHIShader::EType::Pixel:
	{
		FRHIPixelShader* PixelShaderRHI = static_cast<FRHIPixelShader*>(ShaderRHI);
		VALIDATE_BOUND_SHADER(PixelShaderRHI);
		PSConstantBuffer.UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
	}
	break;
	default:
		checkf(0, TEXT("Undefined FRHIShader Type %d!"), (int32)ShaderRHI->GetType());
	}
}

void FD3D12CommandContext::RHISetShaderParameter(FRHIComputeShader* ComputeShaderRHI, uint32 BufferIndex, uint32 BaseIndex, uint32 NumBytes, const void* NewValue)
{
	//VALIDATE_BOUND_SHADER(ComputeShaderRHI);
	checkSlow(BufferIndex == 0);
	CSConstantBuffer.UpdateConstant((const uint8*)NewValue, BaseIndex, NumBytes);
}

void FD3D12CommandContext::ValidateExclusiveDepthStencilAccess(FExclusiveDepthStencil RequestedAccess) const
{
	const bool bSrcDepthWrite = RequestedAccess.IsDepthWrite();
	const bool bSrcStencilWrite = RequestedAccess.IsStencilWrite();

	if (bSrcDepthWrite || bSrcStencilWrite)
	{
		// New Rule: You have to call SetRenderTarget[s]() before
		ensure(CurrentDepthTexture);

		const bool bDstDepthWrite = CurrentDSVAccessType.IsDepthWrite();
		const bool bDstStencilWrite = CurrentDSVAccessType.IsStencilWrite();

		// requested access is not possible, fix SetRenderTarget EExclusiveDepthStencil or request a different one
		check(!bSrcDepthWrite || bDstDepthWrite);
		check(!bSrcStencilWrite || bDstStencilWrite);
	}
}

void FD3D12CommandContext::RHISetStencilRef(uint32 StencilRef)
{
	StateCache.SetStencilRef(StencilRef);
}

void FD3D12CommandContext::RHISetBlendFactor(const FLinearColor& BlendFactor)
{
	StateCache.SetBlendFactor((const float*)&BlendFactor);
}

void FD3D12CommandContext::CommitRenderTargetsAndUAVs()
{
	StateCache.SetRenderTargets(NumSimultaneousRenderTargets, CurrentRenderTargets, CurrentDepthStencilTarget);
	StateCache.ClearUAVs<SF_Pixel>();
}

struct FRTVDesc
{
	uint32 Width;
	uint32 Height;
	DXGI_SAMPLE_DESC SampleDesc;
};

// Return an FRTVDesc structure whose
// Width and height dimensions are adjusted for the RTV's miplevel.
FRTVDesc GetRenderTargetViewDesc(FD3D12RenderTargetView* RenderTargetView)
{
	const D3D12_RENDER_TARGET_VIEW_DESC &TargetDesc = RenderTargetView->GetDesc();

	FD3D12Resource* BaseResource = RenderTargetView->GetResource();
	uint32 MipIndex = 0;
	FRTVDesc ret;
	memset(&ret, 0, sizeof(ret));

	switch (TargetDesc.ViewDimension)
	{
	case D3D12_RTV_DIMENSION_TEXTURE2D:
	case D3D12_RTV_DIMENSION_TEXTURE2DMS:
	case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
	case D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY:
	{
		D3D12_RESOURCE_DESC const& Desc = BaseResource->GetDesc();
		ret.Width = (uint32)Desc.Width;
		ret.Height = Desc.Height;
		ret.SampleDesc = Desc.SampleDesc;
		if (TargetDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D || TargetDesc.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY)
		{
			// All the non-multisampled texture types have their mip-slice in the same position.
			MipIndex = TargetDesc.Texture2D.MipSlice;
		}
		break;
	}
	case D3D12_RTV_DIMENSION_TEXTURE3D:
	{
		D3D12_RESOURCE_DESC const& Desc = BaseResource->GetDesc();
		ret.Width = (uint32)Desc.Width;
		ret.Height = Desc.Height;
		ret.SampleDesc.Count = 1;
		ret.SampleDesc.Quality = 0;
		MipIndex = TargetDesc.Texture3D.MipSlice;
		break;
	}
	default:
	{
		// not expecting 1D targets.
		checkNoEntry();
	}
	}
	ret.Width >>= MipIndex;
	ret.Height >>= MipIndex;
	return ret;
}

void FD3D12CommandContext::RHISetRenderTargets(
	uint32 NewNumSimultaneousRenderTargets,
	const FRHIRenderTargetView* NewRenderTargetsRHI,
	const FRHIDepthRenderTargetView* NewDepthStencilTargetRHI
	)
{
	FD3D12TextureBase* NewDepthStencilTarget = NewDepthStencilTargetRHI ? RetrieveTextureBase(NewDepthStencilTargetRHI->Texture) : nullptr;

	check(NewNumSimultaneousRenderTargets <= MaxSimultaneousRenderTargets);

	bool bTargetChanged = false;

	// Set the appropriate depth stencil view depending on whether depth writes are enabled or not
	FD3D12DepthStencilView* DepthStencilView = NULL;
	if (NewDepthStencilTarget)
	{
		check(NewDepthStencilTargetRHI);	// Calm down static analysis
		CurrentDSVAccessType = NewDepthStencilTargetRHI->GetDepthStencilAccess();
		DepthStencilView = NewDepthStencilTarget->GetDepthStencilView(CurrentDSVAccessType);

		// Unbind any shader views of the depth stencil target that are bound.
		ConditionalClearShaderResource(&NewDepthStencilTarget->ResourceLocation);
	}

	// Check if the depth stencil target is different from the old state.
	if (CurrentDepthStencilTarget != DepthStencilView)
	{
		CurrentDepthTexture = NewDepthStencilTarget;
		CurrentDepthStencilTarget = DepthStencilView;
		bTargetChanged = true;
	}

	// Gather the render target views for the new render targets.
	FD3D12RenderTargetView* NewRenderTargetViews[MaxSimultaneousRenderTargets];
	for (uint32 RenderTargetIndex = 0;RenderTargetIndex < MaxSimultaneousRenderTargets;++RenderTargetIndex)
	{
		FD3D12RenderTargetView* RenderTargetView = NULL;
		if (RenderTargetIndex < NewNumSimultaneousRenderTargets && NewRenderTargetsRHI[RenderTargetIndex].Texture != nullptr)
		{
			int32 RTMipIndex = NewRenderTargetsRHI[RenderTargetIndex].MipIndex;
			int32 RTSliceIndex = NewRenderTargetsRHI[RenderTargetIndex].ArraySliceIndex;
			FD3D12TextureBase* NewRenderTarget = RetrieveTextureBase(NewRenderTargetsRHI[RenderTargetIndex].Texture);
			RenderTargetView = NewRenderTarget->GetRenderTargetView(RTMipIndex, RTSliceIndex);

			ensureMsgf(RenderTargetView, TEXT("Texture being set as render target has no RTV"));

			// Unbind any shader views of the render target that are bound.
			ConditionalClearShaderResource(&NewRenderTarget->ResourceLocation);
		}

		NewRenderTargetViews[RenderTargetIndex] = RenderTargetView;

		// Check if the render target is different from the old state.
		if (CurrentRenderTargets[RenderTargetIndex] != RenderTargetView)
		{
			CurrentRenderTargets[RenderTargetIndex] = RenderTargetView;
			bTargetChanged = true;
		}
	}
	if (NumSimultaneousRenderTargets != NewNumSimultaneousRenderTargets)
	{
		NumSimultaneousRenderTargets = NewNumSimultaneousRenderTargets;
		bTargetChanged = true;
	}

	// Only make the D3D call to change render targets if something actually changed.
	if (bTargetChanged)
	{
		CommitRenderTargetsAndUAVs();
	}

	// Set the viewport to the full size of render target 0.
	if (NewRenderTargetViews[0])
	{
		// check target 0 is valid
		check(0 < NewNumSimultaneousRenderTargets && NewRenderTargetsRHI[0].Texture != nullptr);
		FRTVDesc RTTDesc = GetRenderTargetViewDesc(NewRenderTargetViews[0]);
		RHISetViewport(0.0f, 0.0f, 0.0f, (float)RTTDesc.Width, (float)RTTDesc.Height, 1.0f);
	}
	else if (DepthStencilView)
	{
		FD3D12Resource* DepthTargetTexture = DepthStencilView->GetResource();
		D3D12_RESOURCE_DESC const& DTTDesc = DepthTargetTexture->GetDesc();
		RHISetViewport(0.0f, 0.0f, 0.0f, (float)DTTDesc.Width, (float)DTTDesc.Height, 1.0f);
	}
}

void FD3D12CommandContext::RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo)
{
	FRHIUnorderedAccessView* UAVs[MaxSimultaneousUAVs] = {};

	this->RHISetRenderTargets(RenderTargetsInfo.NumColorRenderTargets,
		RenderTargetsInfo.ColorRenderTarget,
		&RenderTargetsInfo.DepthStencilRenderTarget);

	if (RenderTargetsInfo.bClearColor || RenderTargetsInfo.bClearStencil || RenderTargetsInfo.bClearDepth)
	{
		FLinearColor ClearColors[MaxSimultaneousRenderTargets];
		float DepthClear = 0.0;
		uint32 StencilClear = 0;

		if (RenderTargetsInfo.bClearColor)
		{
			for (int32 i = 0; i < RenderTargetsInfo.NumColorRenderTargets; ++i)
			{
				if (RenderTargetsInfo.ColorRenderTarget[i].Texture != nullptr)
				{
					const FClearValueBinding& ClearValue = RenderTargetsInfo.ColorRenderTarget[i].Texture->GetClearBinding();
					checkf(ClearValue.ColorBinding == EClearBinding::EColorBound, TEXT("Texture: %s does not have a color bound for fast clears"), *RenderTargetsInfo.ColorRenderTarget[i].Texture->GetName().GetPlainNameString());
					ClearColors[i] = ClearValue.GetClearColor();
				}
				else
				{
					ClearColors[i] = FLinearColor(ForceInitToZero);
				}
			}
		}
		if (RenderTargetsInfo.bClearDepth || RenderTargetsInfo.bClearStencil)
		{
			const FClearValueBinding& ClearValue = RenderTargetsInfo.DepthStencilRenderTarget.Texture->GetClearBinding();
			checkf(ClearValue.ColorBinding == EClearBinding::EDepthStencilBound, TEXT("Texture: %s does not have a DS value bound for fast clears"), *RenderTargetsInfo.DepthStencilRenderTarget.Texture->GetName().GetPlainNameString());
			ClearValue.GetDepthStencil(DepthClear, StencilClear);
		}

		this->RHIClearMRTImpl(RenderTargetsInfo.bClearColor, RenderTargetsInfo.NumColorRenderTargets, ClearColors, RenderTargetsInfo.bClearDepth, DepthClear, RenderTargetsInfo.bClearStencil, StencilClear);
	}
}

// Occlusion/Timer queries.
void FD3D12CommandContext::RHIBeginRenderQuery(FRHIRenderQuery* QueryRHI)
{
	FD3D12RenderQuery* Query = RetrieveObject<FD3D12RenderQuery>(QueryRHI);
	check(IsDefaultContext());
	check(Query->Type == RQT_Occlusion);

	GetParentDevice()->GetOcclusionQueryHeap()->BeginQuery(*this, Query);

#if EXECUTE_DEBUG_COMMAND_LISTS
	GIsDoingQuery = true;
#endif
}

void FD3D12CommandContext::RHIEndRenderQuery(FRHIRenderQuery* QueryRHI)
{
	FD3D12RenderQuery* Query = RetrieveObject<FD3D12RenderQuery>(QueryRHI);
	check(IsDefaultContext());

	FD3D12QueryHeap* QueryHeap = nullptr;

	switch (Query->Type)
	{
	case RQT_Occlusion:
		QueryHeap = GetParentDevice()->GetOcclusionQueryHeap();
		break;

	case RQT_AbsoluteTime:
		QueryHeap = GetParentDevice()->GetTimestampQueryHeap();
		break;

	default:
		ensure(false);
	}

	QueryHeap->EndQuery(*this, Query);
	// Multi-GPU support : by setting a timestamp, we can filter only the relevant GPUs when getting the query results.
	Query->Timestamp = GFrameNumberRenderThread;

	// Query data isn't ready until it has been resolved.
	ensure(Query->bResultIsCached == false && Query->bResolved == false);

#if EXECUTE_DEBUG_COMMAND_LISTS
	GIsDoingQuery = false;
#endif
}

// Primitive drawing.

void FD3D12CommandContext::CommitNonComputeShaderConstants()
{
	//SCOPE_CYCLE_COUNTER(STAT_D3D12CommitGraphicsConstants);

	const FD3D12GraphicsPipelineState* const RESTRICT GraphicPSO = StateCache.GetGraphicsPipelineState();

	check(GraphicPSO);

	// Only set the constant buffer if this shader needs the global constant buffer bound
	// Otherwise we will overwrite a different constant buffer
	if (GraphicPSO->bShaderNeedsGlobalConstantBuffer[SF_Vertex])
	{
		StateCache.SetConstantBuffer<SF_Vertex>(VSConstantBuffer, bDiscardSharedConstants);
	}

	// Skip HS/DS CB updates in cases where tessellation isn't being used
	// Note that this is *potentially* unsafe because bDiscardSharedConstants is cleared at the
	// end of the function, however we're OK for now because bDiscardSharedConstants
	// is always reset whenever bUsingTessellation changes in SetBoundShaderState()
	if (bUsingTessellation)
	{
		if (GraphicPSO->bShaderNeedsGlobalConstantBuffer[SF_Hull])
		{
			StateCache.SetConstantBuffer<SF_Hull>(HSConstantBuffer, bDiscardSharedConstants);
		}

		if (GraphicPSO->bShaderNeedsGlobalConstantBuffer[SF_Domain])
		{
			StateCache.SetConstantBuffer<SF_Domain>(DSConstantBuffer, bDiscardSharedConstants);
		}
	}

	if (GraphicPSO->bShaderNeedsGlobalConstantBuffer[SF_Geometry])
	{
		StateCache.SetConstantBuffer<SF_Geometry>(GSConstantBuffer, bDiscardSharedConstants);
	}

	if (GraphicPSO->bShaderNeedsGlobalConstantBuffer[SF_Pixel])
	{
		StateCache.SetConstantBuffer<SF_Pixel>(PSConstantBuffer, bDiscardSharedConstants);
	}

	bDiscardSharedConstants = false;
}

void FD3D12CommandContext::CommitComputeShaderConstants()
{
	StateCache.SetConstantBuffer<SF_Compute>(CSConstantBuffer, bDiscardSharedConstants);
}

template <EShaderFrequency Frequency>
FORCEINLINE void SetResource(FD3D12CommandContext& CmdContext, uint32 BindIndex, FD3D12ShaderResourceView* RESTRICT SRV)
{
	// We set the resource through the RHI to track state for the purposes of unbinding SRVs when a UAV or RTV is bound.
	CmdContext.StateCache.SetShaderResourceView<Frequency>(SRV, BindIndex);
}

template <EShaderFrequency Frequency>
FORCEINLINE void SetResource(FD3D12CommandContext& CmdContext, uint32 BindIndex, FD3D12SamplerState* RESTRICT SamplerState)
{
	CmdContext.StateCache.SetSamplerState<Frequency>(SamplerState, BindIndex);
}

template <EShaderFrequency Frequency>
FORCEINLINE void SetResource(FD3D12CommandContext& CmdContext, uint32 BindIndex, FD3D12UnorderedAccessView* UAV)
{
	uint32 InitialCount = -1;

	// Actually set the UAV
	CmdContext.StateCache.SetUAVs<SF_Pixel>(BindIndex, 1, &UAV, &InitialCount);
}

template <EShaderFrequency ShaderFrequency>
inline int32 SetShaderResourcesFromBuffer_Surface(FD3D12CommandContext& CmdContext, FD3D12UniformBuffer* RESTRICT Buffer, const uint32 * RESTRICT ResourceMap, int32 BufferIndex)
{
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
	const float CurrentTime = FApp::GetCurrentTime();
	int32 NumSetCalls = 0;
	const uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			FRHITexture* TextureRHI = (FRHITexture*)Resources[ResourceIndex].GetReference();
			TextureRHI->SetLastRenderTime(CurrentTime);

			FD3D12TextureBase* TextureD3D12 = CmdContext.RetrieveTextureBase(TextureRHI);
			FD3D12ShaderResourceView* D3D12Resource = TextureD3D12->GetShaderResourceView();
			check(D3D12Resource != nullptr);

			SetResource<ShaderFrequency>(CmdContext, BindIndex, D3D12Resource);
			NumSetCalls++;
			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}

	INC_DWORD_STAT_BY(STAT_D3D12SetTextureInTableCalls, NumSetCalls);
	return NumSetCalls;
}

template <EShaderFrequency ShaderFrequency>
inline int32 SetShaderResourcesFromBuffer_SRV(FD3D12CommandContext& CmdContext, FD3D12UniformBuffer* RESTRICT Buffer, const uint32 * RESTRICT ResourceMap, int32 BufferIndex)
{
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
	int32 NumSetCalls = 0;
	const uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			FD3D12ShaderResourceView* D3D12Resource = CmdContext.RetrieveObject<FD3D12ShaderResourceView>((FRHIShaderResourceView*)(Resources[ResourceIndex].GetReference()));

			SetResource<ShaderFrequency>(CmdContext, BindIndex, D3D12Resource);
			NumSetCalls++;
			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}

	INC_DWORD_STAT_BY(STAT_D3D12SetTextureInTableCalls, NumSetCalls);
	return NumSetCalls;
}

template <EShaderFrequency ShaderFrequency>
inline int32 SetShaderResourcesFromBuffer_Sampler(FD3D12CommandContext& CmdContext, FD3D12UniformBuffer* RESTRICT Buffer, const uint32 * RESTRICT ResourceMap, int32 BufferIndex)
{
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
	int32 NumSetCalls = 0;
	const uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			// todo: could coalesce adjacent bound resources.
			FD3D12SamplerState* D3D12Resource = CmdContext.RetrieveObject<FD3D12SamplerState>((FRHISamplerState*)(Resources[ResourceIndex].GetReference()));

			SetResource<ShaderFrequency>(CmdContext, BindIndex, D3D12Resource);
			NumSetCalls++;
			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}

	INC_DWORD_STAT_BY(STAT_D3D12SetTextureInTableCalls, NumSetCalls);
	return NumSetCalls;
}

template <class ShaderType>
void FD3D12CommandContext::SetResourcesFromTables(const ShaderType* RESTRICT Shader)
{
	checkSlow(Shader);

	// Mask the dirty bits by those buffers from which the shader has bound resources.
	uint32 DirtyBits = Shader->ShaderResourceTable.ResourceTableBits & DirtyUniformBuffers[ShaderType::StaticFrequency];
	while (DirtyBits)
	{
		// Scan for the lowest set bit, compute its index, clear it in the set of dirty bits.
		const uint32 LowestBitMask = (DirtyBits)& (-(int32)DirtyBits);
		const int32 BufferIndex = FMath::FloorLog2(LowestBitMask); // todo: This has a branch on zero, we know it could never be zero...
		DirtyBits ^= LowestBitMask;
		FD3D12UniformBuffer* Buffer = BoundUniformBuffers[ShaderType::StaticFrequency][BufferIndex];
		check(Buffer);
		check(BufferIndex < Shader->ShaderResourceTable.ResourceTableLayoutHashes.Num());
		check(Buffer->GetLayout().GetHash() == Shader->ShaderResourceTable.ResourceTableLayoutHashes[BufferIndex]);

		// todo: could make this two pass: gather then set
		SetShaderResourcesFromBuffer_Surface<(EShaderFrequency)ShaderType::StaticFrequency>(*this, Buffer, Shader->ShaderResourceTable.TextureMap.GetData(), BufferIndex);
		SetShaderResourcesFromBuffer_SRV<(EShaderFrequency)ShaderType::StaticFrequency>(*this, Buffer, Shader->ShaderResourceTable.ShaderResourceViewMap.GetData(), BufferIndex);
		SetShaderResourcesFromBuffer_Sampler<(EShaderFrequency)ShaderType::StaticFrequency>(*this, Buffer, Shader->ShaderResourceTable.SamplerMap.GetData(), BufferIndex);
	}

	DirtyUniformBuffers[ShaderType::StaticFrequency] = 0;
}


template <EShaderFrequency ShaderFrequency>
inline int32 SetShaderResourcesFromBuffer_UAVPS(FD3D12CommandContext& CmdContext, FD3D12UniformBuffer* RESTRICT Buffer, const uint32 * RESTRICT ResourceMap, int32 BufferIndex)
{
	const TRefCountPtr<FRHIResource>* RESTRICT Resources = Buffer->ResourceTable.GetData();
	int32 NumSetCalls = 0;
	const uint32 BufferOffset = ResourceMap[BufferIndex];
	if (BufferOffset > 0)
	{
		const uint32* RESTRICT ResourceInfos = &ResourceMap[BufferOffset];
		uint32 ResourceInfo = *ResourceInfos++;
		do
		{
			checkSlow(FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
			const uint16 ResourceIndex = FRHIResourceTableEntry::GetResourceIndex(ResourceInfo);
			const uint8 BindIndex = FRHIResourceTableEntry::GetBindIndex(ResourceInfo);

			FRHIUnorderedAccessView* RHIUAV = (FRHIUnorderedAccessView*)(Resources[ResourceIndex].GetReference());

			FD3D12UnorderedAccessView* D3D12Resource = CmdContext.RetrieveObject<FD3D12UnorderedAccessView>(RHIUAV);
			SetResource<ShaderFrequency>(CmdContext, BindIndex, D3D12Resource);

			NumSetCalls++;
			ResourceInfo = *ResourceInfos++;
		} while (FRHIResourceTableEntry::GetUniformBufferIndex(ResourceInfo) == BufferIndex);
	}

	INC_DWORD_STAT_BY(STAT_D3D12SetTextureInTableCalls, NumSetCalls);
	return NumSetCalls;
}


template <class ShaderType>
uint32 FD3D12CommandContext::SetUAVPSResourcesFromTables(const ShaderType* RESTRICT Shader)
{
	checkSlow(Shader);

	int32 NumChanged = 0;
	// Mask the dirty bits by those buffers from which the shader has bound resources.
	uint32 DirtyBits = Shader->ShaderResourceTable.ResourceTableBits & DirtyUniformBuffers[ShaderType::StaticFrequency];
	while (DirtyBits)
	{
		// Scan for the lowest set bit, compute its index, clear it in the set of dirty bits.
		const uint32 LowestBitMask = (DirtyBits)& (-(int32)DirtyBits);
		const int32 BufferIndex = FMath::FloorLog2(LowestBitMask); // todo: This has a branch on zero, we know it could never be zero...
		DirtyBits ^= LowestBitMask;
		FD3D12UniformBuffer* Buffer = BoundUniformBuffers[ShaderType::StaticFrequency][BufferIndex];
		check(Buffer);
		check(BufferIndex < Shader->ShaderResourceTable.ResourceTableLayoutHashes.Num());
		check(Buffer->GetLayout().GetHash() == Shader->ShaderResourceTable.ResourceTableLayoutHashes[BufferIndex]);

		if ((EShaderFrequency)ShaderType::StaticFrequency == SF_Pixel)
		{
			NumChanged += SetShaderResourcesFromBuffer_UAVPS<(EShaderFrequency)ShaderType::StaticFrequency>(*this, Buffer, Shader->ShaderResourceTable.UnorderedAccessViewMap.GetData(), BufferIndex);
		}
	}
	return NumChanged;

}

void FD3D12CommandContext::CommitGraphicsResourceTables()
{
	//SCOPE_CYCLE_COUNTER(STAT_D3D12CommitResourceTables);

	const FD3D12GraphicsPipelineState* const RESTRICT GraphicPSO = StateCache.GetGraphicsPipelineState();
	check(GraphicPSO);

	auto* PixelShader = GraphicPSO->GetPixelShader();
	if (PixelShader)
	{
		SetUAVPSResourcesFromTables(PixelShader);
	}
	if (auto* Shader = GraphicPSO->GetVertexShader())
	{
		SetResourcesFromTables(Shader);
	}
	if (PixelShader)
	{
		SetResourcesFromTables(PixelShader);
	}
	if (auto* Shader = GraphicPSO->GetHullShader())
	{
		SetResourcesFromTables(Shader);
	}
	if (auto* Shader = GraphicPSO->GetDomainShader())
	{
		SetResourcesFromTables(Shader);
	}
	if (auto* Shader = GraphicPSO->GetGeometryShader())
	{
		SetResourcesFromTables(Shader);
	}
}

void FD3D12CommandContext::CommitComputeResourceTables(FD3D12ComputeShader* InComputeShader)
{
	//SCOPE_CYCLE_COUNTER(STAT_D3D12CommitResourceTables);

	FD3D12ComputeShader* RESTRICT ComputeShader = InComputeShader;
	check(ComputeShader);
	SetResourcesFromTables(ComputeShader);
}

void FD3D12CommandContext::RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	uint32 VertexCount = GetVertexCountForPrimitiveCount(NumPrimitives, StateCache.GetGraphicsPipelinePrimitiveType());

	NumInstances = FMath::Max<uint32>(1, NumInstances);
	numDraws++;
	numPrimitives += NumInstances * NumPrimitives;
	if (bTrackingEvents)
	{
		GetParentDevice()->RegisterGPUWork(NumPrimitives * NumInstances, VertexCount * NumInstances);
	}

	StateCache.ApplyState<D3D12PT_Graphics>();
	CommandListHandle->DrawInstanced(VertexCount, NumInstances, BaseVertexIndex, 0);

#if UE_BUILD_DEBUG	
	OwningRHI.DrawCount++;
#endif
	DEBUG_EXECUTE_COMMAND_LIST(this);
}

void FD3D12CommandContext::RHIDrawPrimitiveIndirect(FRHIVertexBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{
	FD3D12VertexBuffer* ArgumentBuffer = RetrieveObject<FD3D12VertexBuffer>(ArgumentBufferRHI);

	numDraws++;
	if (bTrackingEvents)
	{
		GetParentDevice()->RegisterGPUWork(0);
	}

	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	FD3D12ResourceLocation& Location = ArgumentBuffer->ResourceLocation;
	FD3D12DynamicRHI::TransitionResource(CommandListHandle, Location.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	StateCache.ApplyState<D3D12PT_Graphics>();

	CommandListHandle->ExecuteIndirect(
		GetParentDevice()->GetParentAdapter()->GetDrawIndirectCommandSignature(),
		1,
		Location.GetResource()->GetResource(),
		Location.GetOffsetFromBaseOfResource() + ArgumentOffset,
		NULL,
		0
		);

	CommandListHandle.UpdateResidency(Location.GetResource());

#if UE_BUILD_DEBUG	
	OwningRHI.DrawCount++;
#endif
	DEBUG_EXECUTE_COMMAND_LIST(this);
}

void FD3D12CommandContext::RHIDrawIndexedIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIStructuredBuffer* ArgumentsBufferRHI, int32 DrawArgumentsIndex, uint32 NumInstances)
{
	FD3D12IndexBuffer* IndexBuffer = RetrieveObject<FD3D12IndexBuffer>(IndexBufferRHI);
	FD3D12StructuredBuffer* ArgumentsBuffer = RetrieveObject<FD3D12StructuredBuffer>(ArgumentsBufferRHI);

	numDraws++;
	if (bTrackingEvents)
	{
		GetParentDevice()->RegisterGPUWork(1);
	}

	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	// determine 16bit vs 32bit indices
	const DXGI_FORMAT Format = (IndexBuffer->GetStride() == sizeof(uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT);

	StateCache.SetIndexBuffer(IndexBuffer->ResourceLocation, Format, 0);

	FD3D12ResourceLocation& Location = ArgumentsBuffer->ResourceLocation;
	FD3D12DynamicRHI::TransitionResource(CommandListHandle, Location.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	StateCache.ApplyState<D3D12PT_Graphics>();

	CommandListHandle->ExecuteIndirect(
		GetParentDevice()->GetParentAdapter()->GetDrawIndexedIndirectCommandSignature(),
		1,
		Location.GetResource()->GetResource(),
		Location.GetOffsetFromBaseOfResource() + DrawArgumentsIndex * ArgumentsBuffer->GetStride(),
		NULL,
		0
		);

	CommandListHandle.UpdateResidency(Location.GetResource());

#if UE_BUILD_DEBUG	
	OwningRHI.DrawCount++;
#endif
	DEBUG_EXECUTE_COMMAND_LIST(this);
}

void FD3D12CommandContext::RHIDrawIndexedPrimitive(FRHIIndexBuffer* IndexBufferRHI, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	// called should make sure the input is valid, this avoid hidden bugs
	ensure(NumPrimitives > 0);

	NumInstances = FMath::Max<uint32>(1, NumInstances);
	numDraws++;
	numPrimitives += NumInstances * NumPrimitives;
	if (bTrackingEvents)
	{
		GetParentDevice()->RegisterGPUWork(NumPrimitives * NumInstances, NumVertices * NumInstances);
	}
	uint32 IndexCount = GetVertexCountForPrimitiveCount(NumPrimitives, StateCache.GetGraphicsPipelinePrimitiveType());

	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	FD3D12IndexBuffer* IndexBuffer = RetrieveObject<FD3D12IndexBuffer>(IndexBufferRHI);

	// Verify that we are not trying to read outside the index buffer range
	// test is an optimized version of: StartIndex + IndexCount <= IndexBuffer->GetSize() / IndexBuffer->GetStride() 
	checkf((StartIndex + IndexCount) * IndexBuffer->GetStride() <= IndexBuffer->GetSize(),
		TEXT("Start %u, Count %u, Type %u, Buffer Size %u, Buffer stride %u"), StartIndex, IndexCount, StateCache.GetGraphicsPipelinePrimitiveType(), IndexBuffer->GetSize(), IndexBuffer->GetStride());

	// determine 16bit vs 32bit indices
	const DXGI_FORMAT Format = (IndexBuffer->GetStride() == sizeof(uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT);
	StateCache.SetIndexBuffer(IndexBuffer->ResourceLocation, Format, 0);
	StateCache.ApplyState<D3D12PT_Graphics>();

	CommandListHandle->DrawIndexedInstanced(IndexCount, NumInstances, StartIndex, BaseVertexIndex, FirstInstance);

#if UE_BUILD_DEBUG	
	OwningRHI.DrawCount++;
#endif
	DEBUG_EXECUTE_COMMAND_LIST(this);
}

void FD3D12CommandContext::RHIDrawIndexedPrimitiveIndirect(FRHIIndexBuffer* IndexBufferRHI, FRHIVertexBuffer* ArgumentBufferRHI, uint32 ArgumentOffset)
{
	FD3D12IndexBuffer* IndexBuffer = RetrieveObject<FD3D12IndexBuffer>(IndexBufferRHI);
	FD3D12VertexBuffer* ArgumentBuffer = RetrieveObject<FD3D12VertexBuffer>(ArgumentBufferRHI);

	numDraws++;
	if (bTrackingEvents)
	{
		GetParentDevice()->RegisterGPUWork(0);
	}

	CommitGraphicsResourceTables();
	CommitNonComputeShaderConstants();

	// Set the index buffer.
	const DXGI_FORMAT Format = (IndexBuffer->GetStride() == sizeof(uint16) ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT);
	StateCache.SetIndexBuffer(IndexBuffer->ResourceLocation, Format, 0);

	FD3D12ResourceLocation& Location = ArgumentBuffer->ResourceLocation;
	FD3D12DynamicRHI::TransitionResource(CommandListHandle, Location.GetResource(), D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

	StateCache.ApplyState<D3D12PT_Graphics>();

	CommandListHandle->ExecuteIndirect(
		GetParentDevice()->GetParentAdapter()->GetDrawIndexedIndirectCommandSignature(),
		1,
		Location.GetResource()->GetResource(),
		Location.GetOffsetFromBaseOfResource() + ArgumentOffset,
		NULL,
		0
		);

	CommandListHandle.UpdateResidency(Location.GetResource());

#if UE_BUILD_DEBUG	
	OwningRHI.DrawCount++;
#endif
	DEBUG_EXECUTE_COMMAND_LIST(this);
}

// Raster operations.
void FD3D12CommandContext::RHIClearMRT(bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil)
{
	RHIClearMRTImpl(bClearColor, NumClearColors, ClearColorArray, bClearDepth, Depth, bClearStencil, Stencil);
}

void FD3D12CommandContext::RHIClearMRTImpl(bool bClearColor, int32 NumClearColors, const FLinearColor* ClearColorArray, bool bClearDepth, float Depth, bool bClearStencil, uint32 Stencil)
{
	SCOPE_CYCLE_COUNTER(STAT_D3D12ClearMRT);

	const D3D12_VIEWPORT& Viewport = StateCache.GetViewport();
	const D3D12_RECT& ScissorRect = StateCache.GetScissorRect();

	if (ScissorRect.left >= ScissorRect.right || ScissorRect.top >= ScissorRect.bottom)
	{
		return;
	}

	FD3D12RenderTargetView* RenderTargetViews[MaxSimultaneousRenderTargets];
	FD3D12DepthStencilView* DSView = nullptr;
	uint32 NumSimultaneousRTs = 0;
	StateCache.GetRenderTargets(RenderTargetViews, &NumSimultaneousRTs, &DSView);
	FD3D12BoundRenderTargets BoundRenderTargets(RenderTargetViews, NumSimultaneousRTs, DSView);
	FD3D12DepthStencilView* DepthStencilView = BoundRenderTargets.GetDepthStencilView();

	// Use rounding for when the number can't be perfectly represented by a float
	const LONG Width = static_cast<LONG>(FMath::RoundToInt(Viewport.Width));
	const LONG Height = static_cast<LONG>(FMath::RoundToInt(Viewport.Height));

	// When clearing we must pay attention to the currently set scissor rect
	bool bClearCoversEntireSurface = false;
	if (ScissorRect.left <= 0 && ScissorRect.top <= 0 &&
		ScissorRect.right >= Width && ScissorRect.bottom >= Height)
	{
		bClearCoversEntireSurface = true;
	}

	// Must specify enough clear colors for all active RTs
	check(!bClearColor || NumClearColors >= BoundRenderTargets.GetNumActiveTargets());

	const bool bSupportsFastClear = true;
	uint32 ClearRectCount = 0;
	D3D12_RECT* pClearRects = nullptr;
	D3D12_RECT ClearRects[4];

	// Only pass a rect down to the driver if we specifically want to clear a sub-rect
	if (!bSupportsFastClear || !bClearCoversEntireSurface)
	{
		{
			ClearRects[ClearRectCount] = ScissorRect;
			ClearRectCount++;
		}

		pClearRects = ClearRects;

		static const bool bSpewPerfWarnings = false;

		if (bSpewPerfWarnings)
		{
			UE_LOG(LogD3D12RHI, Warning, TEXT("RHIClearMRTImpl: Using non-fast clear path! This has performance implications"));
			UE_LOG(LogD3D12RHI, Warning, TEXT("       Viewport: Width %d, Height: %d"), static_cast<LONG>(FMath::RoundToInt(Viewport.Width)), static_cast<LONG>(FMath::RoundToInt(Viewport.Height)));
			UE_LOG(LogD3D12RHI, Warning, TEXT("   Scissor Rect: Width %d, Height: %d"), ScissorRect.right, ScissorRect.bottom);
		}
	}

	const bool ClearRTV = bClearColor && BoundRenderTargets.GetNumActiveTargets() > 0;
	const bool ClearDSV = (bClearDepth || bClearStencil) && DepthStencilView;

	if (ClearRTV)
	{
		for (int32 TargetIndex = 0; TargetIndex < BoundRenderTargets.GetNumActiveTargets(); TargetIndex++)
		{
			FD3D12RenderTargetView* RTView = BoundRenderTargets.GetRenderTargetView(TargetIndex);

			if (RTView != nullptr)
			{
				FD3D12DynamicRHI::TransitionResource(CommandListHandle, RTView, D3D12_RESOURCE_STATE_RENDER_TARGET);
			}
		}
	}

	uint32 ClearFlags = 0;
	if (ClearDSV)
	{
		if (bClearDepth && DepthStencilView->HasDepth())
		{
			ClearFlags |= D3D12_CLEAR_FLAG_DEPTH;
		}
		else if (bClearDepth)
		{
			UE_LOG(LogD3D12RHI, Warning, TEXT("RHIClearMRTImpl: Asking to clear a DSV that does not store depth."));
		}

		if (bClearStencil && DepthStencilView->HasStencil())
		{
			ClearFlags |= D3D12_CLEAR_FLAG_STENCIL;
		}
		else if (bClearStencil)
		{
			UE_LOG(LogD3D12RHI, Warning, TEXT("RHIClearMRTImpl: Asking to clear a DSV that does not store stencil."));
		}

		if (bClearDepth && (!DepthStencilView->HasStencil() || bClearStencil))
		{
			// Transition the entire view (Both depth and stencil planes if applicable)
			// Some DSVs don't have stencil bits.
			FD3D12DynamicRHI::TransitionResource(CommandListHandle, DepthStencilView, D3D12_RESOURCE_STATE_DEPTH_WRITE);
		}
		else
		{
			if (bClearDepth)
			{
				// Transition just the depth plane
				check(bClearDepth && !bClearStencil);
				FD3D12DynamicRHI::TransitionResource(CommandListHandle, DepthStencilView->GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, DepthStencilView->GetDepthOnlyViewSubresourceSubset());
			}
			else
			{
				// Transition just the stencil plane
				check(!bClearDepth && bClearStencil);
				FD3D12DynamicRHI::TransitionResource(CommandListHandle, DepthStencilView->GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, DepthStencilView->GetStencilOnlyViewSubresourceSubset());
			}
		}
	}

	if (ClearRTV || ClearDSV)
	{
		CommandListHandle.FlushResourceBarriers();

		if (ClearRTV)
		{
			for (int32 TargetIndex = 0; TargetIndex < BoundRenderTargets.GetNumActiveTargets(); TargetIndex++)
			{
				FD3D12RenderTargetView* RTView = BoundRenderTargets.GetRenderTargetView(TargetIndex);

				if (RTView != nullptr)
				{
					numClears++;
					CommandListHandle->ClearRenderTargetView(RTView->GetView(), (float*)&ClearColorArray[TargetIndex], ClearRectCount, pClearRects);
					CommandListHandle.UpdateResidency(RTView->GetResource());
				}
			}
		}

		if (ClearDSV)
		{
			numClears++;
			CommandListHandle->ClearDepthStencilView(DepthStencilView->GetView(), (D3D12_CLEAR_FLAGS)ClearFlags, Depth, Stencil, ClearRectCount, pClearRects);
			CommandListHandle.UpdateResidency(DepthStencilView->GetResource());
		}
	}

	if (IsDefaultContext())
	{
		GetParentDevice()->RegisterGPUWork(0);
	}

	DEBUG_EXECUTE_COMMAND_LIST(this);
}

void FD3D12CommandContext::RHIBindClearMRTValues(bool bClearColor, bool bClearDepth, bool bClearStencil)
{
	// Not necessary for d3d.
}

// Blocks the CPU until the GPU catches up and goes idle.
void FD3D12DynamicRHI::RHIBlockUntilGPUIdle()
{
	const int32 NumAdapters = ChosenAdapters.Num();
	for (int32 Index = 0; Index < NumAdapters; ++Index)
	{
		GetAdapter(Index).BlockUntilIdle();
	}
}

void FD3D12DynamicRHI::RHISubmitCommandsAndFlushGPU()
{
	FD3D12Adapter& Adapter = GetAdapter();
	for (uint32 GPUIndex : FRHIGPUMask::All())
	{
		Adapter.GetDevice(GPUIndex)->GetDefaultCommandContext().RHISubmitCommandsHint();
	}
}

/*
* Returns the total GPU time taken to render the last frame. Same metric as FPlatformTime::Cycles().
*/
uint32 FD3D12DynamicRHI::RHIGetGPUFrameCycles(uint32 GPUIndex)
{
	return GGPUFrameTime;
}

void FD3D12DynamicRHI::RHIExecuteCommandList(FRHICommandList* CmdList)
{
	check(0); // this path has gone stale and needs updated methods, starting at ERCT_SetScissorRect
}


void FD3D12CommandContext::RHISetDepthBounds(float MinDepth, float MaxDepth)
{
	StateCache.SetDepthBounds(MinDepth, MaxDepth);
}

void FD3D12CommandContext::SetDepthBounds(float MinDepth, float MaxDepth)
{
#if PLATFORM_WINDOWS
	if (GSupportsDepthBoundsTest && CommandListHandle.GraphicsCommandList1())
	{
		// This should only be called if Depth Bounds Test is supported.
		CommandListHandle.GraphicsCommandList1()->OMSetDepthBounds(MinDepth, MaxDepth);
	}
#endif
}

void FD3D12CommandContext::RHISubmitCommandsHint()
{
	// Resolve any timestamp queries so far (if any).
	GetParentDevice()->GetTimestampQueryHeap()->EndQueryBatchAndResolveQueryData(*this);

	// Submit the work we have so far, and start a new command list.
	FlushCommands();
}

#define USE_COPY_QUEUE_FOR_RESOURCE_SYNC 1
/*
* When using AFR certain inter-frame dependecies need to be synchronized across all GPUs.
* For example a rendering technique that relies on results from the previous frame (which occured on the other GPU).
*/

void FD3D12CommandContext::RHIWaitForTemporalEffect(const FName& InEffectName)
{
#if WITH_MGPU
	check(IsDefaultContext());

	if (GNumAlternateFrameRenderingGroups == 1 || !AFRSyncTemporalResources)
	{
		return;
	}

#if USE_COPY_QUEUE_FOR_RESOURCE_SYNC
	FD3D12Adapter* Adapter = GetParentAdapter();
	FD3D12TemporalEffect* Effect = Adapter->GetTemporalEffect(InEffectName);

	const uint32 GPUIndex = GetGPUIndex();
	if (Effect->ShouldWaitForPrevious(GPUIndex))
	{
		// Execute the current command list so we can have a point to insert a wait
		FlushCommands();

		Effect->WaitForPrevious(GPUIndex, bIsAsyncComputeContext ? ED3D12CommandQueueType::Async : ED3D12CommandQueueType::Default);
	}
#endif
#endif // WITH_MGPU
}

void FD3D12CommandContext::RHIBroadcastTemporalEffect(const FName& InEffectName, const TArrayView<FRHITexture*> InTextures)
{
#if WITH_MGPU
	check(IsDefaultContext());

	if (GNumAlternateFrameRenderingGroups == 1 || !AFRSyncTemporalResources)
	{
		return;
	}

	const uint32 GPUIndex = GetGPUIndex();
	TArray<FD3D12TextureBase*, TInlineAllocator<8>> SrcTextures, DstTextures;
	const int32 NumTextures = InTextures.Num();
	for (int32 i = 0; i < NumTextures; i++)
	{
		SrcTextures.Emplace(RetrieveTextureBase(InTextures[i]));
		const uint32 NextSiblingGPUIndex = AFRUtils::GetNextSiblingGPUIndex(GPUIndex);
		DstTextures.Emplace(RetrieveTextureBase(InTextures[i], [NextSiblingGPUIndex](FD3D12Device* Device) { return Device->GetGPUIndex() == NextSiblingGPUIndex; }));
	}

#if USE_COPY_QUEUE_FOR_RESOURCE_SYNC

	FD3D12Device* Device = GetParentDevice();
	FD3D12Adapter* Adapter = Device->GetParentAdapter();
	FD3D12TemporalEffect* Effect = Adapter->GetTemporalEffect(InEffectName);

	for (int32 i = 0; i < NumTextures; i++)
	{
		// Resources must be in the COMMON state before using on the copy queue.
		FD3D12DynamicRHI::TransitionResource(CommandListHandle, SrcTextures[i]->GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		FD3D12DynamicRHI::TransitionResource(CommandListHandle, DstTextures[i]->GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	}
	CommandListHandle.FlushResourceBarriers();

	// Finish rendering on the current queue.
	FlushCommands();

	// Tell the copy queue to wait for the current queue to finish rendering before starting the copy.
	Effect->SignalSyncComplete(GPUIndex, bIsAsyncComputeContext ? ED3D12CommandQueueType::Async : ED3D12CommandQueueType::Default);
	Effect->WaitForPrevious(GPUIndex, ED3D12CommandQueueType::Copy);

	FD3D12CommandAllocatorManager& CopyCommandAllocatorManager = Device->GetTextureStreamingCommandAllocatorManager();
	FD3D12CommandAllocator* CopyCommandAllocator = CopyCommandAllocatorManager.ObtainCommandAllocator();
	FD3D12CommandListManager& CopyCommandListManager = Device->GetCopyCommandListManager();
	FD3D12CommandListHandle hCopyCommandList = CopyCommandListManager.ObtainCommandList(*CopyCommandAllocator);
	hCopyCommandList.SetCurrentOwningContext(this);

	for (int32 i = 0; i < NumTextures; i++)
	{
		// NB: We do not increment numCopies here because the main context isn't doing any work.
		hCopyCommandList->CopyResource(DstTextures[i]->GetResource()->GetResource(), SrcTextures[i]->GetResource()->GetResource());
	}
	hCopyCommandList.Close();

	CopyCommandListManager.ExecuteCommandList(hCopyCommandList);
	CopyCommandAllocatorManager.ReleaseCommandAllocator(CopyCommandAllocator);

	// Signal again once the copy queue copy is complete.
	Effect->SignalSyncComplete(GPUIndex, ED3D12CommandQueueType::Copy);

#else

	for (int32 i = 0; i < NumTextures; i++)
	{
		FD3D12DynamicRHI::TransitionResource(CommandListHandle, SrcTextures[i]->GetResource(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
		FD3D12DynamicRHI::TransitionResource(CommandListHandle, DstTextures[i]->GetResource(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);
	}
	CommandListHandle.FlushResourceBarriers();

	for (int32 i = 0; i < NumTextures; i++)
	{
		numCopies++;
		CommandListHandle->CopyResource(DstTextures[i]->GetResource()->GetResource(), SrcTextures[i]->GetResource()->GetResource());
	}

#endif // USE_COPY_QUEUE_FOR_RESOURCE_SYNC
#endif // WITH_MGPU
}
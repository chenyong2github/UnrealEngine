// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	RHICommandListCommandExecutes.inl: RHI Command List execute functions.
=============================================================================*/

#if !defined(INTERNAL_DECORATOR)
	#define INTERNAL_DECORATOR(Method) CmdList.GetContext().Method
#endif

//for functions where the signatures do not match between gfx and compute commandlists
#if !defined(INTERNAL_DECORATOR_COMPUTE)
#define INTERNAL_DECORATOR_COMPUTE(Method) CmdList.GetComputeContext().Method
#endif

class FRHICommandListBase;
class IRHIComputeContext;
struct FComputedBSS;
struct FComputedGraphicsPipelineState;
struct FComputedUniformBuffer;
struct FMemory;
struct FRHICommandAutomaticCacheFlushAfterComputeShader;
struct FRHICommandBeginDrawingViewport;
struct FRHICommandBeginFrame;
struct FRHICommandBeginOcclusionQueryBatch;
struct FRHICommandBeginRenderQuery;
struct FRHICommandBeginScene;
struct FRHICommandBindClearMRTValues;
struct FRHICommandBuildLocalBoundShaderState;
struct FRHICommandBuildLocalGraphicsPipelineState;
struct FRHICommandBuildLocalUniformBuffer;
struct FRHICommandCopyToResolveTarget;
struct FRHICommandDrawIndexedIndirect;
struct FRHICommandDrawIndexedPrimitive;
struct FRHICommandDrawIndexedPrimitiveIndirect;
struct FRHICommandDrawPrimitive;
struct FRHICommandDrawPrimitiveIndirect;
struct FRHICommandSetDepthBounds;
struct FRHICommandEndDrawingViewport;
struct FRHICommandEndFrame;
struct FRHICommandEndOcclusionQueryBatch;
struct FRHICommandEndRenderQuery;
struct FRHICommandEndScene;
struct FRHICommandFlushComputeShaderCache;
struct FRHICommandSetBlendFactor;
struct FRHICommandSetBoundShaderState;
struct FRHICommandSetLocalGraphicsPipelineState;
struct FRHICommandSetRasterizerState;
struct FRHICommandSetRenderTargets;
struct FRHICommandSetRenderTargetsAndClear;
struct FRHICommandSetScissorRect;
struct FRHICommandSetStencilRef;
struct FRHICommandSetStereoViewport;
struct FRHICommandSetStreamSource;
struct FRHICommandSetViewport;
struct FRHICommandTransitionTextures;
struct FRHICommandTransitionTexturesDepth;
struct FRHICommandTransitionTexturesArray;
struct FRHICommandUpdateTextureReference;
struct FRHICommandClearRayTracingBindings;
struct FRHICommandRayTraceOcclusion;
struct FRHICommandRayTraceIntersection;
struct FRHICommandRayTraceDispatch;
struct FRHICommandSetRayTracingBindings;

template <typename TRHIShader> struct FRHICommandSetLocalUniformBuffer;

void FRHICommandBeginUpdateMultiFrameResource::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginUpdateMultiFrameResource);
	INTERNAL_DECORATOR(RHIBeginUpdateMultiFrameResource)(Texture);
}

void FRHICommandEndUpdateMultiFrameResource::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndUpdateMultiFrameResource);
	INTERNAL_DECORATOR(RHIEndUpdateMultiFrameResource)(Texture);
}

void FRHICommandBeginUpdateMultiFrameUAV::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginUpdateMultiFrameUAV);
	INTERNAL_DECORATOR(RHIBeginUpdateMultiFrameResource)(UAV);
}

void FRHICommandEndUpdateMultiFrameUAV::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndUpdateMultiFrameUAV);
	INTERNAL_DECORATOR(RHIEndUpdateMultiFrameResource)(UAV);
}

#if WITH_MGPU
void FRHICommandSetGPUMask::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetGPUMask);
	INTERNAL_DECORATOR_COMPUTE(RHISetGPUMask)(GPUMask);
}
void FRHICommandWaitForTemporalEffect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(WaitForTemporalEffect);
	INTERNAL_DECORATOR(RHIWaitForTemporalEffect)(EffectName);
}

void FRHICommandBroadcastTemporalEffect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BroadcastTemporalEffect);
	INTERNAL_DECORATOR(RHIBroadcastTemporalEffect)(EffectName, { Textures, NumTextures });
}
#endif // WITH_MGPU

void FRHICommandSetStencilRef::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetStencilRef);
	INTERNAL_DECORATOR(RHISetStencilRef)(StencilRef);
}

template<> void FRHICommandSetShaderParameter<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderParameter);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderParameter)(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
}

template <> void FRHICommandSetShaderParameter<FRHIGraphicsShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderParameter);
	INTERNAL_DECORATOR(RHISetShaderParameter)(Shader, BufferIndex, BaseIndex, NumBytes, NewValue);
}

template<> void FRHICommandSetShaderUniformBuffer<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderUniformBuffer);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderUniformBuffer)(Shader, BaseIndex, UniformBuffer);
}

template<> void FRHICommandSetShaderUniformBuffer<FRHIGraphicsShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderUniformBuffer);
	INTERNAL_DECORATOR(RHISetShaderUniformBuffer)(Shader, BaseIndex, UniformBuffer);
}

template<> void FRHICommandSetShaderTexture<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderTexture);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderTexture)(Shader, TextureIndex, Texture);
}

template<> void FRHICommandSetShaderTexture<FRHIGraphicsShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderTexture);
	INTERNAL_DECORATOR(RHISetShaderTexture)(Shader, TextureIndex, Texture);
}

template<> void FRHICommandSetShaderResourceViewParameter<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderResourceViewParameter);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderResourceViewParameter)(Shader, SamplerIndex, SRV);
}

template<> void FRHICommandSetShaderResourceViewParameter<FRHIGraphicsShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderResourceViewParameter);
	INTERNAL_DECORATOR(RHISetShaderResourceViewParameter)(Shader, SamplerIndex, SRV);
}

template <> void FRHICommandSetUAVParameter<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetUAVParameter);
	INTERNAL_DECORATOR_COMPUTE(RHISetUAVParameter)(Shader, UAVIndex, UAV);
}

template <> void FRHICommandSetUAVParameter<FRHIPixelShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetUAVParameter);
	INTERNAL_DECORATOR(RHISetUAVParameter)(Shader, UAVIndex, UAV);
}

void FRHICommandSetUAVParameter_InitialCount::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetUAVParameter);
	INTERNAL_DECORATOR_COMPUTE(RHISetUAVParameter)(Shader, UAVIndex, UAV, InitialCount);
}

template<> void FRHICommandSetShaderSampler<FRHIComputeShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderSampler);
	INTERNAL_DECORATOR_COMPUTE(RHISetShaderSampler)(Shader, SamplerIndex, Sampler);
}

template<> void FRHICommandSetShaderSampler<FRHIGraphicsShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetShaderSampler);
	INTERNAL_DECORATOR(RHISetShaderSampler)(Shader, SamplerIndex, Sampler);
}

void FRHICommandDrawPrimitive::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawPrimitive);
	INTERNAL_DECORATOR(RHIDrawPrimitive)(BaseVertexIndex, NumPrimitives, NumInstances);
}

void FRHICommandDrawIndexedPrimitive::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawIndexedPrimitive);
	INTERNAL_DECORATOR(RHIDrawIndexedPrimitive)(IndexBuffer, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances);
}

void FRHICommandSetBlendFactor::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetBlendFactor);
	INTERNAL_DECORATOR(RHISetBlendFactor)(BlendFactor);
}

void FRHICommandSetStreamSource::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetStreamSource);
	INTERNAL_DECORATOR(RHISetStreamSource)(StreamIndex, VertexBuffer, Offset);
}

void FRHICommandSetViewport::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetViewport);
	INTERNAL_DECORATOR(RHISetViewport)(MinX, MinY, MinZ, MaxX, MaxY, MaxZ);
}

void FRHICommandSetStereoViewport::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetStereoViewport);
	INTERNAL_DECORATOR(RHISetStereoViewport)(LeftMinX, RightMinX, LeftMinY, RightMinY, MinZ, LeftMaxX, RightMaxX, LeftMaxY, RightMaxY, MaxZ);
}

void FRHICommandSetScissorRect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetScissorRect);
	INTERNAL_DECORATOR(RHISetScissorRect)(bEnable, MinX, MinY, MaxX, MaxY);
}

void FRHICommandBeginRenderPass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginRenderPass);
	INTERNAL_DECORATOR(RHIBeginRenderPass)(Info, Name);
}

void FRHICommandEndRenderPass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndRenderPass);
	INTERNAL_DECORATOR(RHIEndRenderPass)();
}

void FRHICommandNextSubpass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(NextSubpass);
	INTERNAL_DECORATOR(RHINextSubpass)();
}

void FRHICommandBeginComputePass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginComputePass);
	INTERNAL_DECORATOR(RHIBeginComputePass)(Name);
}

void FRHICommandEndComputePass::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndComputePass);
	INTERNAL_DECORATOR(RHIEndComputePass)();
}

void FRHICommandSetRenderTargets::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetRenderTargets);
	INTERNAL_DECORATOR(RHISetRenderTargets)(
		NewNumSimultaneousRenderTargets,
		NewRenderTargetsRHI,
		&NewDepthStencilTarget);
}

void FRHICommandBindClearMRTValues::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BindClearMRTValues);
	INTERNAL_DECORATOR(RHIBindClearMRTValues)(
		bClearColor,
		bClearDepth,
		bClearStencil		
		);
}

void FRHICommandSetComputeShader::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetComputeShader);
	INTERNAL_DECORATOR_COMPUTE(RHISetComputeShader)(ComputeShader);
}

void FRHICommandSetComputePipelineState::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetComputePipelineState);
	extern FRHIComputePipelineState* ExecuteSetComputePipelineState(FComputePipelineState* ComputePipelineState);
	FRHIComputePipelineState* RHIComputePipelineState = ExecuteSetComputePipelineState(ComputePipelineState);
	INTERNAL_DECORATOR_COMPUTE(RHISetComputePipelineState)(RHIComputePipelineState);
}

void FRHICommandSetGraphicsPipelineState::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetGraphicsPipelineState);
	extern FRHIGraphicsPipelineState* ExecuteSetGraphicsPipelineState(FGraphicsPipelineState* GraphicsPipelineState);
	FRHIGraphicsPipelineState* RHIGraphicsPipelineState = ExecuteSetGraphicsPipelineState(GraphicsPipelineState);
	INTERNAL_DECORATOR(RHISetGraphicsPipelineState)(RHIGraphicsPipelineState);
}

void FRHICommandDispatchComputeShader::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DispatchComputeShader);
	INTERNAL_DECORATOR_COMPUTE(RHIDispatchComputeShader)(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
}

void FRHICommandDispatchIndirectComputeShader::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DispatchIndirectComputeShader);
	INTERNAL_DECORATOR_COMPUTE(RHIDispatchIndirectComputeShader)(ArgumentBuffer, ArgumentOffset);
}

void FRHICommandAutomaticCacheFlushAfterComputeShader::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(AutomaticCacheFlushAfterComputeShader);
	INTERNAL_DECORATOR(RHIAutomaticCacheFlushAfterComputeShader)(bEnable);
}

void FRHICommandFlushComputeShaderCache::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(FlushComputeShaderCache);
	INTERNAL_DECORATOR(RHIFlushComputeShaderCache)();
}

void FRHICommandDrawPrimitiveIndirect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawPrimitiveIndirect);
	INTERNAL_DECORATOR(RHIDrawPrimitiveIndirect)(ArgumentBuffer, ArgumentOffset);
}

void FRHICommandDrawIndexedIndirect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawIndexedIndirect);
	INTERNAL_DECORATOR(RHIDrawIndexedIndirect)(IndexBufferRHI, ArgumentsBufferRHI, DrawArgumentsIndex, NumInstances);
}

void FRHICommandDrawIndexedPrimitiveIndirect::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(DrawIndexedPrimitiveIndirect);
	INTERNAL_DECORATOR(RHIDrawIndexedPrimitiveIndirect)(IndexBuffer, ArgumentsBuffer, ArgumentOffset);
}

void FRHICommandSetDepthBounds::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EnableDepthBoundsTest);
	INTERNAL_DECORATOR(RHISetDepthBounds)(MinDepth, MaxDepth);
}

void FRHICommandClearUAVFloat::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(ClearUAV);
	INTERNAL_DECORATOR_COMPUTE(RHIClearUAVFloat)(UnorderedAccessViewRHI, Values);
}

void FRHICommandClearUAVUint::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(ClearUAV);
	INTERNAL_DECORATOR_COMPUTE(RHIClearUAVUint)(UnorderedAccessViewRHI, Values);
}

void FRHICommandCopyToResolveTarget::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(CopyToResolveTarget);
	INTERNAL_DECORATOR(RHICopyToResolveTarget)(SourceTexture, DestTexture, ResolveParams);
}

void FRHICommandCopyTexture::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(CopyTexture);
	INTERNAL_DECORATOR(RHICopyTexture)(SourceTexture, DestTexture, CopyInfo);
}

void FRHICommandResummarizeHTile::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(ResummarizeHTile);
	INTERNAL_DECORATOR(RHIResummarizeHTile)(DepthTexture);
}

void FRHICommandTransitionTextures::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(TransitionTextures);
	INTERNAL_DECORATOR(RHITransitionResources)(TransitionType, &Textures[0], NumTextures);
}

void FRHICommandTransitionTexturesDepth::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(TransitionTextures);
	INTERNAL_DECORATOR(RHITransitionResources)(DepthStencilMode, DepthTexture);
}

void FRHICommandTransitionTexturesArray::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(TransitionTextures);
	INTERNAL_DECORATOR(RHITransitionResources)(TransitionType, &Textures[0], Textures.Num());
}

void FRHICommandTransitionUAVs::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(TransitionUAVs);
	INTERNAL_DECORATOR_COMPUTE(RHITransitionResources)(TransitionType, TransitionPipeline, UAVs, NumUAVs, WriteFence);
}

void FRHICommandSetAsyncComputeBudget::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetAsyncComputeBudget);
	INTERNAL_DECORATOR_COMPUTE(RHISetAsyncComputeBudget)(Budget);
}

void FRHICommandWaitComputeFence::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(WaitComputeFence);
	INTERNAL_DECORATOR_COMPUTE(RHIWaitComputeFence)(WaitFence);
}

void FRHICommandCopyToStagingBuffer::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EnqueueStagedRead);
	INTERNAL_DECORATOR_COMPUTE(RHICopyToStagingBuffer)(SourceBuffer, DestinationStagingBuffer, Offset, NumBytes);
}

void FRHICommandWriteGPUFence::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(WriteGPUFence);
	INTERNAL_DECORATOR_COMPUTE(RHIWriteGPUFence)(Fence);
}

void FRHICommandSetGlobalUniformBuffers::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetGlobalUniformBuffers);
	INTERNAL_DECORATOR_COMPUTE(RHISetGlobalUniformBuffers)(UniformBuffers);
}

void FRHICommandBuildLocalUniformBuffer::Execute(FRHICommandListBase& CmdList)
{
	LLM_SCOPE(ELLMTag::Shaders);
	RHISTAT(BuildLocalUniformBuffer);
	check(!IsValidRef(WorkArea.ComputedUniformBuffer->UniformBuffer)); // should not already have been created
	check(WorkArea.Layout);
	check(WorkArea.Contents); 
	if (WorkArea.ComputedUniformBuffer->UseCount)
	{
		WorkArea.ComputedUniformBuffer->UniformBuffer = GDynamicRHI->RHICreateUniformBuffer(WorkArea.Contents, *WorkArea.Layout, UniformBuffer_SingleFrame, EUniformBufferValidation::ValidateResources);
	}
	WorkArea.Layout = nullptr;
	WorkArea.Contents = nullptr;
}

template <typename TRHIShader>
void FRHICommandSetLocalUniformBuffer<TRHIShader>::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetLocalUniformBuffer);
	check(LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UseCount > 0 && IsValidRef(LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UniformBuffer)); // this should have been created and should have uses outstanding
	INTERNAL_DECORATOR(RHISetShaderUniformBuffer)(Shader, BaseIndex, LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UniformBuffer);
	if (--LocalUniformBuffer.WorkArea->ComputedUniformBuffer->UseCount == 0)
	{
		LocalUniformBuffer.WorkArea->ComputedUniformBuffer->~FComputedUniformBuffer();
	}
}
template struct FRHICommandSetLocalUniformBuffer<FRHIVertexShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIHullShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIDomainShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIGeometryShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIPixelShader>;
template struct FRHICommandSetLocalUniformBuffer<FRHIComputeShader>;

void FRHICommandBeginRenderQuery::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginRenderQuery);
	INTERNAL_DECORATOR(RHIBeginRenderQuery)(RenderQuery);
}

void FRHICommandEndRenderQuery::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndRenderQuery);
	INTERNAL_DECORATOR(RHIEndRenderQuery)(RenderQuery);
}

void FRHICommandSubmitCommandsHint::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SubmitCommandsHint);
	INTERNAL_DECORATOR_COMPUTE(RHISubmitCommandsHint)();
}

void FRHICommandPollOcclusionQueries::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(PollOcclusionQueries);
	INTERNAL_DECORATOR(RHIPollOcclusionQueries)();
}

#if RHI_RAYTRACING

void FRHICommandCopyBufferRegion::Execute(FRHICommandListBase& CmdList)
{
	INTERNAL_DECORATOR(RHICopyBufferRegion)(DestBuffer, DstOffset, SourceBuffer, SrcOffset, NumBytes);
}

void FRHICommandCopyBufferRegions::Execute(FRHICommandListBase& CmdList)
{
	INTERNAL_DECORATOR(RHICopyBufferRegions)(Params);
}

void FRHICommandBuildAccelerationStructure::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BuildAccelerationStructure);
	INTERNAL_DECORATOR_COMPUTE(RHIBuildAccelerationStructure)(Scene);
}

void FRHICommandClearRayTracingBindings::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(ClearRayTracingBindings);
	INTERNAL_DECORATOR(RHIClearRayTracingBindings)(Scene);
}

void FRHICommandBuildAccelerationStructures::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BuildAccelerationStructure);
	INTERNAL_DECORATOR_COMPUTE(RHIBuildAccelerationStructures)(Params);
}

void FRHICommandRayTraceOcclusion::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RayTraceOcclusion);
	INTERNAL_DECORATOR(RHIRayTraceOcclusion)(Scene, Rays, Output, NumRays);
}

void FRHICommandRayTraceIntersection::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RayTraceIntersection);
	INTERNAL_DECORATOR(RHIRayTraceIntersection)(Scene, Rays, Output, NumRays);
}

void FRHICommandRayTraceDispatch::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RayTraceDispatch);
	extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
	INTERNAL_DECORATOR(RHIRayTraceDispatch)(GetRHIRayTracingPipelineState(Pipeline), RayGenShader, Scene, GlobalResourceBindings, Width, Height);
}

void FRHICommandSetRayTracingBindings::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(SetRayTracingHitGroup);
	extern RHI_API FRHIRayTracingPipelineState* GetRHIRayTracingPipelineState(FRayTracingPipelineState*);
	if (BindingType == EBindingType_HitGroup)
	{
		INTERNAL_DECORATOR(RHISetRayTracingHitGroup)(Scene, InstanceIndex, SegmentIndex, ShaderSlot, GetRHIRayTracingPipelineState(Pipeline), ShaderIndex,
			NumUniformBuffers, UniformBuffers,
			LooseParameterDataSize, LooseParameterData,
			UserData);
	}
	else if (BindingType == EBindingType_HitGroupBatch)
	{
		INTERNAL_DECORATOR(RHISetRayTracingHitGroups)(Scene, GetRHIRayTracingPipelineState(Pipeline), NumBindings, Bindings);
	}
	else if (BindingType == EBindingType_CallableShader)
	{
		INTERNAL_DECORATOR(RHISetRayTracingCallableShader)(Scene, ShaderSlot, GetRHIRayTracingPipelineState(Pipeline), ShaderIndex, NumUniformBuffers, UniformBuffers, UserData);
	}
	else 
	{
		INTERNAL_DECORATOR(RHISetRayTracingMissShader)(Scene, ShaderSlot, GetRHIRayTracingPipelineState(Pipeline), ShaderIndex, NumUniformBuffers, UniformBuffers, UserData);
	}
}

#endif // RHI_RAYTRACING

void FRHICommandUpdateTextureReference::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(UpdateTextureReference);
	INTERNAL_DECORATOR(RHIUpdateTextureReference)(TextureRef, NewTexture);
}

void FRHIResourceUpdateInfo::ReleaseRefs()
{
	switch (Type)
	{
	case UT_VertexBuffer:
		VertexBuffer.DestBuffer->Release();
		if (VertexBuffer.SrcBuffer)
		{
			VertexBuffer.SrcBuffer->Release();
		}
		break;
	case UT_IndexBuffer:
		IndexBuffer.DestBuffer->Release();
		if (IndexBuffer.SrcBuffer)
		{
			IndexBuffer.SrcBuffer->Release();
		}
		break;
	case UT_VertexBufferSRV:
		VertexBufferSRV.SRV->Release();
		if (VertexBufferSRV.VertexBuffer)
		{
			VertexBufferSRV.VertexBuffer->Release();
		}
		break;
	case UT_IndexBufferSRV:
		IndexBufferSRV.SRV->Release();
		if (IndexBufferSRV.IndexBuffer)
		{
			IndexBufferSRV.IndexBuffer->Release();
		}
		break;
	default:
		// Unrecognized type, do nothing
		break;
	}
}

FRHICommandUpdateRHIResources::~FRHICommandUpdateRHIResources()
{
	if (bNeedReleaseRefs)
	{
		for (int32 Idx = 0; Idx < Num; ++Idx)
		{
			UpdateInfos[Idx].ReleaseRefs();
		}
	}
}

void FRHICommandUpdateRHIResources::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(UpdateRHIResources);
	for (int32 Idx = 0; Idx < Num; ++Idx)
	{
		FRHIResourceUpdateInfo& Info = UpdateInfos[Idx];
		switch (Info.Type)
		{
		case FRHIResourceUpdateInfo::UT_VertexBuffer:
			GDynamicRHI->RHITransferVertexBufferUnderlyingResource(
				Info.VertexBuffer.DestBuffer,
				Info.VertexBuffer.SrcBuffer);
			break;
		case FRHIResourceUpdateInfo::UT_IndexBuffer:
			GDynamicRHI->RHITransferIndexBufferUnderlyingResource(
				Info.IndexBuffer.DestBuffer,
				Info.IndexBuffer.SrcBuffer);
			break;
		case FRHIResourceUpdateInfo::UT_VertexBufferSRV:
			GDynamicRHI->RHIUpdateShaderResourceView(
				Info.VertexBufferSRV.SRV,
				Info.VertexBufferSRV.VertexBuffer,
				Info.VertexBufferSRV.Stride,
				Info.VertexBufferSRV.Format);
			break;
		case FRHIResourceUpdateInfo::UT_IndexBufferSRV:
			GDynamicRHI->RHIUpdateShaderResourceView(
				Info.IndexBufferSRV.SRV,
				Info.IndexBufferSRV.IndexBuffer);
			break;
		default:
			// Unrecognized type, do nothing
			break;
		}
	}
}

void FRHICommandBeginScene::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginScene);
	INTERNAL_DECORATOR(RHIBeginScene)();
}

void FRHICommandEndScene::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndScene);
	INTERNAL_DECORATOR(RHIEndScene)();
}

void FRHICommandBeginFrame::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginFrame);
	INTERNAL_DECORATOR(RHIBeginFrame)();
}

void FRHICommandEndFrame::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndFrame);
	INTERNAL_DECORATOR(RHIEndFrame)();
}

void FRHICommandBeginDrawingViewport::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(BeginDrawingViewport);
	INTERNAL_DECORATOR(RHIBeginDrawingViewport)(Viewport, RenderTargetRHI);
}

void FRHICommandEndDrawingViewport::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(EndDrawingViewport);
	INTERNAL_DECORATOR(RHIEndDrawingViewport)(Viewport, bPresent, bLockToVsync);
}

void FRHICommandPushEvent::Execute(FRHICommandListBase& CmdList)
{
#if	RHI_COMMAND_LIST_DEBUG_TRACES
	if (GetEmitDrawEventsOnlyOnCommandlist())
	{
		return;
	}
#endif
	RHISTAT(PushEvent);
	INTERNAL_DECORATOR_COMPUTE(RHIPushEvent)(Name, Color);
}

void FRHICommandPopEvent::Execute(FRHICommandListBase& CmdList)
{
#if	RHI_COMMAND_LIST_DEBUG_TRACES
	if (GetEmitDrawEventsOnlyOnCommandlist())
	{
		return;
	}
#endif
	RHISTAT(PopEvent);
	INTERNAL_DECORATOR_COMPUTE(RHIPopEvent)();
}

void FRHICommandInvalidateCachedState::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RHIInvalidateCachedState);
	INTERNAL_DECORATOR(RHIInvalidateCachedState)();
}

void FRHICommandDiscardRenderTargets::Execute(FRHICommandListBase& CmdList)
{
	RHISTAT(RHIDiscardRenderTargets);
	INTERNAL_DECORATOR(RHIDiscardRenderTargets)(Depth, Stencil, ColorBitMask);
}

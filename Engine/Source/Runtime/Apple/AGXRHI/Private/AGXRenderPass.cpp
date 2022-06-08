// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXRenderPass.cpp: AGX RHI command pass wrapper.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "AGXShaderTypes.h"
#include "AGXGraphicsPipelineState.h"
#include "AGXVertexDeclaration.h"
#include "AGXRenderPass.h"
#include "AGXProfiler.h"
#include "AGXFrameAllocator.h"

#pragma mark - Private Console Variables -

static int32 GAGXCommandBufferCommitThreshold = 0;
static FAutoConsoleVariableRef CVarAGXCommandBufferCommitThreshold(
	TEXT("rhi.AGX.CommandBufferCommitThreshold"),
	GAGXCommandBufferCommitThreshold,
	TEXT("When enabled (> 0) if the command buffer has more than this number of draw/dispatch command encoded then it will be committed at the next encoder boundary to keep the GPU busy. (Default: 0, set to <= 0 to disable)"));

static int32 GAGXDeferRenderPasses = 1;
static FAutoConsoleVariableRef CVarAGXDeferRenderPasses(
	TEXT("rhi.AGX.DeferRenderPasses"),
	GAGXDeferRenderPasses,
	TEXT("Whether to defer creating render command encoders. (Default: 1)"));

// Deliberately not static!
int32 GAGXDebugOpsCount = PLATFORM_MAC ? 1 : 10;
static FAutoConsoleVariableRef CVarAGXDebugOpsCount(
	TEXT("rhi.AGX.DebugOpsCount"),
	GAGXDebugOpsCount,
	TEXT("The number of operations to allow between GPU debug markers for the r.GPUCrashDebugging reports. (Default: Mac = 1 : iOS/tvOS = 10)"));

#pragma mark - Public C++ Boilerplate -

FAGXRenderPass::FAGXRenderPass(FAGXCommandList& InCmdList, FAGXStateCache& Cache)
: CmdList(InCmdList)
, State(Cache)
, CurrentEncoder(InCmdList, EAGXCommandEncoderCurrent)
, PrologueEncoder(InCmdList, EAGXCommandEncoderPrologue)
, RenderPassDesc(nil)
, ComputeDispatchType(MTLDispatchTypeSerial)
, NumOutstandingOps(0)
, bWithinRenderPass(false)
{
}

FAGXRenderPass::~FAGXRenderPass(void)
{
	check(!CurrentEncoder.GetCommandBuffer());
	check(!PrologueEncoder.GetCommandBuffer());
}

void FAGXRenderPass::SetDispatchType(MTLDispatchType Type)
{
	ComputeDispatchType = Type;
}

void FAGXRenderPass::Begin(bool bParallelBegin)
{
	if (!CmdList.IsParallel() && !CurrentEncoder.GetCommandBuffer())
	{
		CurrentEncoder.StartCommandBuffer();
		check(CurrentEncoder.GetCommandBuffer());
	}
}

void FAGXRenderPass::Submit(EAGXSubmitFlags Flags)
{
	if (CurrentEncoder.GetCommandBuffer() || (Flags & EAGXSubmitFlagsAsyncCommandBuffer))
	{
		if (PrologueEncoder.IsBlitCommandEncoderActive() || PrologueEncoder.IsComputeCommandEncoderActive())
		{
			check(PrologueEncoder.GetCommandBuffer());
			PrologueEncoder.EndEncoding();
		}
		if (PrologueEncoder.GetCommandBuffer())
		{
			PrologueEncoder.CommitCommandBuffer((Flags & EAGXSubmitFlagsAsyncCommandBuffer) ? Flags : EAGXSubmitFlagsNone);
        }
    }

    // Must be on the render thread if there's no RHI thread
    // Must be on the RHI thread otherwise
    CheckMetalThread();
    
    if (Flags & EAGXSubmitFlagsLastCommandBuffer)
    {
        check(CurrentEncoder.GetCommandBuffer());
        id <MTLCommandBuffer> CommandBuffer = CurrentEncoder.GetCommandBuffer().GetPtr();
        
        FAGXDeviceContext& DeviceContext = (FAGXDeviceContext&)GetAGXDeviceContext();
        FAGXFrameAllocator* UniformAllocator = DeviceContext.GetUniformAllocator();
        
        UniformAllocator->MarkEndOfFrame(DeviceContext.GetFrameNumberRHIThread(), CommandBuffer);
		
		FAGXFrameAllocator* TransferAllocator = DeviceContext.GetTransferAllocator();
		TransferAllocator->MarkEndOfFrame(DeviceContext.GetFrameNumberRHIThread(), CommandBuffer);
    }
    
    if (CurrentEncoder.GetCommandBuffer() && !(Flags & EAGXSubmitFlagsAsyncCommandBuffer))
    {
        if (CurrentEncoder.IsRenderCommandEncoderActive() || CurrentEncoder.IsBlitCommandEncoderActive() || CurrentEncoder.IsComputeCommandEncoderActive())
        {
            if (CurrentEncoder.IsRenderCommandEncoderActive())
            {
                State.SetRenderStoreActions(CurrentEncoder, (Flags & EAGXSubmitFlagsBreakCommandBuffer));
				State.FlushVisibilityResults(CurrentEncoder);
            }
            CurrentEncoder.EndEncoding();
        }
		
        CurrentEncoder.CommitCommandBuffer(Flags);
    }
	
	OutstandingBufferUploads.Empty();
	if (Flags & EAGXSubmitFlagsResetState)
	{
		PrologueEncoder.Reset();
		CurrentEncoder.Reset();
	}
}

void FAGXRenderPass::BeginParallelRenderPass(MTLRenderPassDescriptor* InRenderPassDesc, uint32 NumParallelContextsInPass)
{
	check(!bWithinRenderPass);
	check(!RenderPassDesc);
	check(InRenderPassDesc);
	check(CurrentEncoder.GetCommandBuffer());

	if (!CurrentEncoder.GetParallelRenderCommandEncoder())
	{
		if (PrologueEncoder.IsBlitCommandEncoderActive() || PrologueEncoder.IsComputeCommandEncoderActive())
		{
			PrologueEncoder.EndEncoding();
		}
		if (CurrentEncoder.IsRenderCommandEncoderActive() || CurrentEncoder.IsBlitCommandEncoderActive() || CurrentEncoder.IsComputeCommandEncoderActive())
		{
			State.FlushVisibilityResults(CurrentEncoder);
			CurrentEncoder.EndEncoding();
		}

		CurrentEncoder.SetRenderPassDescriptor(InRenderPassDesc);

		CurrentEncoder.BeginParallelRenderCommandEncoding(NumParallelContextsInPass);

		RenderPassDesc = InRenderPassDesc;

		bWithinRenderPass = true;
	}
}

void FAGXRenderPass::BeginRenderPass(MTLRenderPassDescriptor* InRenderPassDesc)
{
	check(!bWithinRenderPass);
	check(!RenderPassDesc);
	check(InRenderPassDesc);
	check(!CurrentEncoder.IsRenderCommandEncoderActive());
	if (!CmdList.IsParallel() && !CmdList.IsImmediate() && !CurrentEncoder.GetCommandBuffer())
	{
		CurrentEncoder.StartCommandBuffer();
	}
	check(CmdList.IsParallel() || CurrentEncoder.GetCommandBuffer());
	
	// EndEncoding should provide the encoder fence...
	if (PrologueEncoder.IsBlitCommandEncoderActive() || PrologueEncoder.IsComputeCommandEncoderActive())
	{
		PrologueEncoder.EndEncoding();
	}
	if (CurrentEncoder.IsRenderCommandEncoderActive() || CurrentEncoder.IsBlitCommandEncoderActive() || CurrentEncoder.IsComputeCommandEncoderActive())
	{
		State.FlushVisibilityResults(CurrentEncoder);
		CurrentEncoder.EndEncoding();
	}
	State.SetStateDirty();
	State.SetRenderTargetsActive(true);
	
	RenderPassDesc = InRenderPassDesc;
	
	CurrentEncoder.SetRenderPassDescriptor(RenderPassDesc);

	if (!GAGXDeferRenderPasses || !State.CanRestartRenderPass() || CmdList.IsParallel())
	{
		CurrentEncoder.BeginRenderCommandEncoding();
		State.SetRenderStoreActions(CurrentEncoder, false);
		check(CurrentEncoder.IsRenderCommandEncoderActive());
	}
	
	bWithinRenderPass = true;
	
	check(!PrologueEncoder.IsBlitCommandEncoderActive() && !PrologueEncoder.IsComputeCommandEncoderActive());
}

void FAGXRenderPass::RestartRenderPass(MTLRenderPassDescriptor* InRenderPassDesc)
{
	check(bWithinRenderPass);
	check(RenderPassDesc);
	check(CmdList.IsParallel() || CurrentEncoder.GetCommandBuffer());
	
	MTLRenderPassDescriptor* StartDesc = nil;
	if (InRenderPassDesc != nil)
	{
		// Just restart with the render pass we were given - the caller should have ensured that this is restartable
		check(State.CanRestartRenderPass());
		StartDesc = InRenderPassDesc;
	}
	else if (State.PrepareToRestart(CurrentEncoder.IsRenderPassDescriptorValid() && (State.GetRenderPassDescriptor() == CurrentEncoder.GetRenderPassDescriptor())))
	{
		// Restart with the render pass we have in the state cache - the state cache says its safe
		StartDesc = State.GetRenderPassDescriptor();
	}
	else
	{
		METAL_FATAL_ERROR(TEXT("Failed to restart render pass with descriptor: %s"), *FString([RenderPassDesc description]));
	}
	check(StartDesc);
	
	RenderPassDesc = StartDesc;
	
	// EndEncoding should provide the encoder fence...
	if (CurrentEncoder.IsBlitCommandEncoderActive() || CurrentEncoder.IsComputeCommandEncoderActive() || CurrentEncoder.IsRenderCommandEncoderActive())
	{
		if (CurrentEncoder.IsRenderCommandEncoderActive())
		{
			State.SetRenderStoreActions(CurrentEncoder, true);
			State.FlushVisibilityResults(CurrentEncoder);
		}
		CurrentEncoder.EndEncoding();
	}
	State.SetStateDirty();
	State.SetRenderTargetsActive(true);
	
	CurrentEncoder.SetRenderPassDescriptor(RenderPassDesc);
	CurrentEncoder.BeginRenderCommandEncoding();
	State.SetRenderStoreActions(CurrentEncoder, false);
	
	check(CurrentEncoder.IsRenderCommandEncoderActive());
}

void FAGXRenderPass::DrawPrimitive(uint32 PrimitiveType, uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	NumInstances = FMath::Max(NumInstances,1u);
	
	ConditionalSwitchToRender();
	check(CurrentEncoder.GetCommandBuffer());
	check(CurrentEncoder.IsRenderCommandEncoderActive());
	
	PrepareToRender(PrimitiveType);

	// draw!
	// how many verts to render
	uint32 NumVertices = GetVertexCountForPrimitiveCount(NumPrimitives, PrimitiveType);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDraw(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__, NumPrimitives, NumVertices, NumInstances));

	[CurrentEncoder.GetRenderCommandEncoder() drawPrimitives:AGXTranslatePrimitiveType(PrimitiveType)
												 vertexStart:(NSUInteger)BaseVertexIndex
												 vertexCount:(NSUInteger)NumVertices
											   instanceCount:(NSUInteger)NumInstances];

	ConditionalSubmit();	
}

void FAGXRenderPass::DrawPrimitiveIndirect(uint32 PrimitiveType, FAGXVertexBuffer* VertexBuffer, uint32 ArgumentOffset)
{
	if (GetAGXDeviceContext().SupportsFeature(EAGXFeaturesIndirectBuffer))
	{
		ConditionalSwitchToRender();
		check(CurrentEncoder.GetCommandBuffer());
		check(CurrentEncoder.IsRenderCommandEncoderActive());
		
		FAGXBuffer TheBackingBuffer = VertexBuffer->GetCurrentBuffer();
		check(TheBackingBuffer);
		
		PrepareToRender(PrimitiveType);
		
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDraw(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__, 1, 1, 1));

		[CurrentEncoder.GetRenderCommandEncoder() drawPrimitives:AGXTranslatePrimitiveType(PrimitiveType)
												  indirectBuffer:TheBackingBuffer.GetPtr()
											indirectBufferOffset:((NSUInteger)ArgumentOffset + TheBackingBuffer.GetOffset())];

		ConditionalSubmit();
	}
	else
	{
		NOT_SUPPORTED("RHIDrawPrimitiveIndirect");
	}
}

void FAGXRenderPass::DrawIndexedPrimitive(FAGXBuffer const& IndexBuffer, uint32 IndexStride, uint32 PrimitiveType, int32 BaseVertexIndex, uint32 FirstInstance,
											 uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	// We need at least one to cover all use cases
	NumInstances = FMath::Max(NumInstances,1u);
	
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	{
		FAGXGraphicsPipelineState* PipelineState = State.GetGraphicsPSO();
		check(PipelineState != nullptr);
		FAGXVertexDeclaration* VertexDecl = PipelineState->VertexDeclaration;
		check(VertexDecl != nullptr);
		
		// Set our local copy and try to disprove the passed in value
		uint32 ClampedNumInstances = NumInstances;
		const CrossCompiler::FShaderBindingInOutMask& InOutMask = PipelineState->VertexShader->Bindings.InOutMask;

		// I think it is valid to have no elements in this list
		for(int VertexElemIdx = 0;VertexElemIdx < VertexDecl->Elements.Num();++VertexElemIdx)
		{
			FVertexElement const & VertexElem = VertexDecl->Elements[VertexElemIdx];
			if(VertexElem.Stride > 0 && VertexElem.bUseInstanceIndex && InOutMask.IsFieldEnabled(VertexElem.AttributeIndex))
			{
				uint32 AvailElementCount = 0;
				
				uint32 BufferSize = State.GetVertexBufferSize(VertexElem.StreamIndex);
				uint32 ElementCount = (BufferSize / VertexElem.Stride);
				
				if(ElementCount > FirstInstance)
				{
					AvailElementCount = ElementCount - FirstInstance;
				}
				
				ClampedNumInstances = FMath::Clamp<uint32>(ClampedNumInstances, 0, AvailElementCount);
				
				if(ClampedNumInstances < NumInstances)
				{
					FString ShaderName = TEXT("Unknown");
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					ShaderName = PipelineState->PixelShader->ShaderName;
#endif
					// Setting NumInstances to ClampedNumInstances would fix any visual rendering bugs resulting from this bad call but these draw calls are wrong - don't hide the issue
					UE_LOG(LogAGX, Error, TEXT("Metal DrawIndexedPrimitive requested to draw %d Instances but vertex stream only has %d instance data available. ShaderName: %s, Deficient Attribute Index: %u"), NumInstances, ClampedNumInstances,
						   *ShaderName, VertexElem.AttributeIndex);
				}
			}
		}
	}
#endif
	
	ConditionalSwitchToRender();
	check(CurrentEncoder.GetCommandBuffer());
	check(CurrentEncoder.IsRenderCommandEncoderActive());
	
	PrepareToRender(PrimitiveType);
	
	uint32 NumIndices = GetVertexCountForPrimitiveCount(NumPrimitives, PrimitiveType);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDraw(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__, NumPrimitives, NumVertices, NumInstances));

	if (GRHISupportsBaseVertexIndex && GRHISupportsFirstInstance)
	{
		[CurrentEncoder.GetRenderCommandEncoder() drawIndexedPrimitives:AGXTranslatePrimitiveType(PrimitiveType)
															 indexCount:(NSUInteger)NumIndices
															  indexType:((IndexStride == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32)
															indexBuffer:IndexBuffer.GetPtr()
													  indexBufferOffset:((NSUInteger)(StartIndex * IndexStride) + IndexBuffer.GetOffset())
														  instanceCount:(NSUInteger)NumInstances
															 baseVertex:(NSUInteger)BaseVertexIndex
														   baseInstance:(NSUInteger)FirstInstance];
	}
	else
	{
		[CurrentEncoder.GetRenderCommandEncoder() drawIndexedPrimitives:AGXTranslatePrimitiveType(PrimitiveType)
															 indexCount:(NSUInteger)NumIndices
															  indexType:((IndexStride == 2) ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32)
															indexBuffer:IndexBuffer.GetPtr()
													  indexBufferOffset:((NSUInteger)(StartIndex * IndexStride) + IndexBuffer.GetOffset())
														  instanceCount:(NSUInteger)NumInstances];
	}
	
	ConditionalSubmit();
}

void FAGXRenderPass::DrawIndexedIndirect(FAGXIndexBuffer* IndexBuffer, uint32 PrimitiveType, FAGXStructuredBuffer* VertexBuffer, int32 DrawArgumentsIndex, uint32 NumInstances)
{
	if (GetAGXDeviceContext().SupportsFeature(EAGXFeaturesIndirectBuffer))
	{
		check(NumInstances > 1);
		
		ConditionalSwitchToRender();
		check(CurrentEncoder.GetCommandBuffer());
		check(CurrentEncoder.IsRenderCommandEncoderActive());
		
		FAGXBuffer TheBackingIndexBuffer = IndexBuffer->GetCurrentBuffer();
		FAGXBuffer TheBackingBuffer = VertexBuffer->GetCurrentBuffer();
		
		check(TheBackingIndexBuffer);
		check(TheBackingBuffer);
		
		// finalize any pending state
		PrepareToRender(PrimitiveType);
		
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDraw(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__, 1, 1, 1));

		[CurrentEncoder.GetRenderCommandEncoder()	drawIndexedPrimitives:AGXTranslatePrimitiveType(PrimitiveType)
																indexType:(MTLIndexType)IndexBuffer->IndexType
															  indexBuffer:TheBackingIndexBuffer.GetPtr()
														indexBufferOffset:(0 + TheBackingIndexBuffer.GetOffset())
														   indirectBuffer:TheBackingBuffer.GetPtr()
													 indirectBufferOffset:((NSUInteger)(DrawArgumentsIndex * 5 * sizeof(uint32)) + TheBackingBuffer.GetOffset())];

		ConditionalSubmit();
	}
	else
	{
		NOT_SUPPORTED("RHIDrawIndexedIndirect");
	}
}

void FAGXRenderPass::DrawIndexedPrimitiveIndirect(uint32 PrimitiveType,FAGXIndexBuffer* IndexBuffer,FAGXVertexBuffer* VertexBuffer,uint32 ArgumentOffset)
{
	if (GetAGXDeviceContext().SupportsFeature(EAGXFeaturesIndirectBuffer))
	{		 
		ConditionalSwitchToRender();
		check(CurrentEncoder.GetCommandBuffer());
		check(CurrentEncoder.IsRenderCommandEncoderActive());
		
		FAGXBuffer TheBackingIndexBuffer = IndexBuffer->GetCurrentBuffer();
		FAGXBuffer TheBackingBuffer = VertexBuffer->GetCurrentBuffer();
		
		check(TheBackingIndexBuffer);
		check(TheBackingBuffer);
		
		PrepareToRender(PrimitiveType);
		
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDraw(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__, 1, 1, 1));

		[CurrentEncoder.GetRenderCommandEncoder()	drawIndexedPrimitives:AGXTranslatePrimitiveType(PrimitiveType)
																indexType:(MTLIndexType)IndexBuffer->IndexType
															  indexBuffer:TheBackingIndexBuffer.GetPtr()
														indexBufferOffset:(0 + TheBackingIndexBuffer.GetOffset())
														   indirectBuffer:TheBackingBuffer.GetPtr()
													 indirectBufferOffset:((NSUInteger)ArgumentOffset + TheBackingBuffer.GetOffset())];

		ConditionalSubmit();
	}
	else
	{
		NOT_SUPPORTED("RHIDrawIndexedPrimitiveIndirect");
	}
}

void FAGXRenderPass::Dispatch(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
{
    if (CurrentEncoder.IsParallel() || CurrentEncoder.NumEncodedPasses() == 0)
	{
		ConditionalSwitchToAsyncCompute();
		check(PrologueEncoder.GetCommandBuffer());
		check(PrologueEncoder.IsComputeCommandEncoderActive());
		
		PrepareToAsyncDispatch();
		
		TRefCountPtr<FAGXComputeShader> ComputeShader = State.GetComputeShader();
		check(ComputeShader);
		
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDispatch(PrologueEncoder.GetCommandBufferStats(), __FUNCTION__));
		
		MTLSize ThreadgroupCounts = MTLSizeMake(ComputeShader->NumThreadsX, ComputeShader->NumThreadsY, ComputeShader->NumThreadsZ);
		check(ComputeShader->NumThreadsX > 0 && ComputeShader->NumThreadsY > 0 && ComputeShader->NumThreadsZ > 0);
		MTLSize Threadgroups = MTLSizeMake(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
		[PrologueEncoder.GetComputeCommandEncoder() dispatchThreadgroups:Threadgroups threadsPerThreadgroup:ThreadgroupCounts];
		
		ConditionalSubmit();
	}
	else
	{
		ConditionalSwitchToCompute();
		check(CurrentEncoder.GetCommandBuffer());
		check(CurrentEncoder.IsComputeCommandEncoderActive());

		PrepareToDispatch();
		
		TRefCountPtr<FAGXComputeShader> ComputeShader = State.GetComputeShader();
		check(ComputeShader);
		
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDispatch(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
		
		MTLSize ThreadgroupCounts = MTLSizeMake(ComputeShader->NumThreadsX, ComputeShader->NumThreadsY, ComputeShader->NumThreadsZ);
		check(ComputeShader->NumThreadsX > 0 && ComputeShader->NumThreadsY > 0 && ComputeShader->NumThreadsZ > 0);
		MTLSize Threadgroups = MTLSizeMake(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
		[CurrentEncoder.GetComputeCommandEncoder() dispatchThreadgroups:Threadgroups threadsPerThreadgroup:ThreadgroupCounts];
		
		ConditionalSubmit();
	}
}

void FAGXRenderPass::DispatchIndirect(FAGXVertexBuffer* ArgumentBuffer, uint32 ArgumentOffset)
{
	check(ArgumentBuffer);
	
    if (CurrentEncoder.IsParallel() || CurrentEncoder.NumEncodedPasses() == 0)
	{
		ConditionalSwitchToAsyncCompute();
		check(PrologueEncoder.GetCommandBuffer());
		check(PrologueEncoder.IsComputeCommandEncoderActive());
		check(ArgumentBuffer->GetCurrentBuffer());
		
		PrepareToAsyncDispatch();
		
		TRefCountPtr<FAGXComputeShader> ComputeShader = State.GetComputeShader();
		check(ComputeShader);
		
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDispatch(PrologueEncoder.GetCommandBufferStats(), __FUNCTION__));
		MTLSize ThreadgroupCounts = MTLSizeMake(ComputeShader->NumThreadsX, ComputeShader->NumThreadsY, ComputeShader->NumThreadsZ);
		check(ComputeShader->NumThreadsX > 0 && ComputeShader->NumThreadsY > 0 && ComputeShader->NumThreadsZ > 0);
		
		[PrologueEncoder.GetComputeCommandEncoder() dispatchThreadgroupsWithIndirectBuffer:ArgumentBuffer->GetCurrentBuffer().GetPtr()
																	  indirectBufferOffset:(ArgumentOffset + ArgumentBuffer->GetCurrentBuffer().GetOffset())
																	 threadsPerThreadgroup:ThreadgroupCounts];
		
		ConditionalSubmit();
	}
	else
	{
		ConditionalSwitchToCompute();
		check(CurrentEncoder.GetCommandBuffer());
		check(CurrentEncoder.IsComputeCommandEncoderActive());
		
		PrepareToDispatch();
		
		TRefCountPtr<FAGXComputeShader> ComputeShader = State.GetComputeShader();
		check(ComputeShader);
		
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeDispatch(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
		MTLSize ThreadgroupCounts = MTLSizeMake(ComputeShader->NumThreadsX, ComputeShader->NumThreadsY, ComputeShader->NumThreadsZ);
		check(ComputeShader->NumThreadsX > 0 && ComputeShader->NumThreadsY > 0 && ComputeShader->NumThreadsZ > 0);

		[CurrentEncoder.GetComputeCommandEncoder() dispatchThreadgroupsWithIndirectBuffer:ArgumentBuffer->GetCurrentBuffer()
																	 indirectBufferOffset:(ArgumentOffset + ArgumentBuffer->GetCurrentBuffer().GetOffset())
																	threadsPerThreadgroup:ThreadgroupCounts];

		ConditionalSubmit();
	}
}

void FAGXRenderPass::EndRenderPass(void)
{
	if (bWithinRenderPass)
	{
		check(RenderPassDesc);
		check(CurrentEncoder.GetCommandBuffer());
		
		// This just calls End - it exists only to enforce assumptions
		End();
	}
}

void FAGXRenderPass::InsertTextureBarrier()
{
#if PLATFORM_MAC
	check(CurrentEncoder.IsRenderCommandEncoderActive());
	
	id<MTLRenderCommandEncoder> RenderEncoder = CurrentEncoder.GetRenderCommandEncoder();
	check(RenderEncoder);
	
	[RenderEncoder memoryBarrierWithScope:MTLBarrierScopeRenderTargets afterStages:MTLRenderStageFragment beforeStages:MTLRenderStageVertex];
#endif
}

void FAGXRenderPass::CopyFromTextureToBuffer(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXBuffer const& toBuffer, uint32 destinationOffset, uint32 destinationBytesPerRow, uint32 destinationBytesPerImage, mtlpp::BlitOption options)
{
	ConditionalSwitchToBlit();
	
	id<MTLBlitCommandEncoder> Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	{
		[Encoder			 copyFromTexture:Texture.GetPtr()
								 sourceSlice:(NSUInteger)sourceSlice
								 sourceLevel:(NSUInteger)sourceLevel
								sourceOrigin:*reinterpret_cast<MTLOrigin*>(&sourceOrigin)
								  sourceSize:*reinterpret_cast<MTLSize*>(&sourceSize)
									toBuffer:toBuffer.GetPtr()
						   destinationOffset:(NSUInteger)destinationOffset + toBuffer.GetOffset()
					  destinationBytesPerRow:(NSUInteger)destinationBytesPerRow
					destinationBytesPerImage:(NSUInteger)destinationBytesPerImage
									 options:*reinterpret_cast<MTLBlitOption*>(&options)];
	}
	
	ConditionalSubmit();
}

void FAGXRenderPass::CopyFromBufferToTexture(FAGXBuffer const& Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin, mtlpp::BlitOption options)
{
	ConditionalSwitchToBlit();
	
	id<MTLBlitCommandEncoder> Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	
	if (options == mtlpp::BlitOption::None)
	{
		[Encoder		 copyFromBuffer:Buffer.GetPtr()
						   sourceOffset:(NSUInteger)sourceOffset + Buffer.GetOffset()
					  sourceBytesPerRow:(NSUInteger)sourceBytesPerRow
					sourceBytesPerImage:(NSUInteger)sourceBytesPerImage
							 sourceSize:*reinterpret_cast<MTLSize*>(&sourceSize)
							  toTexture:toTexture.GetPtr()
					   destinationSlice:(NSUInteger)destinationSlice
					   destinationLevel:(NSUInteger)destinationLevel
					  destinationOrigin:*reinterpret_cast<MTLOrigin*>(&destinationOrigin)];
	}
	else
	{
		[Encoder		 copyFromBuffer:Buffer.GetPtr()
						   sourceOffset:(NSUInteger)sourceOffset + Buffer.GetOffset()
					  sourceBytesPerRow:(NSUInteger)sourceBytesPerRow
					sourceBytesPerImage:(NSUInteger)sourceBytesPerImage
							 sourceSize:*reinterpret_cast<MTLSize*>(&sourceSize)
							  toTexture:toTexture.GetPtr()
					   destinationSlice:(NSUInteger)destinationSlice
					   destinationLevel:(NSUInteger)destinationLevel
					  destinationOrigin:*reinterpret_cast<MTLOrigin*>(&destinationOrigin)
								options:*reinterpret_cast<MTLBlitOption*>(&options)];
	}
	
	ConditionalSubmit();
}

void FAGXRenderPass::CopyFromTextureToTexture(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin)
{
	ConditionalSwitchToBlit();
	
	id<MTLBlitCommandEncoder> Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	
	[Encoder	  copyFromTexture:Texture.GetPtr()
					  sourceSlice:(NSUInteger)sourceSlice
					  sourceLevel:(NSUInteger)sourceLevel
					 sourceOrigin:*reinterpret_cast<MTLOrigin*>(&sourceOrigin)
					   sourceSize:*reinterpret_cast<MTLSize*>(&sourceSize)
						toTexture:toTexture.GetPtr()
				 destinationSlice:(NSUInteger)destinationSlice
				 destinationLevel:(NSUInteger)destinationLevel
				destinationOrigin:*reinterpret_cast<MTLOrigin*>(&destinationOrigin)];
	
	ConditionalSubmit();
}

void FAGXRenderPass::CopyFromBufferToBuffer(FAGXBuffer const& SourceBuffer, NSUInteger SourceOffset, FAGXBuffer const& DestinationBuffer, NSUInteger DestinationOffset, NSUInteger Size)
{
	ConditionalSwitchToBlit();
	
	id<MTLBlitCommandEncoder> Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));

	[Encoder	   copyFromBuffer:SourceBuffer.GetPtr()
					 sourceOffset:SourceOffset + SourceBuffer.GetOffset()
						 toBuffer:DestinationBuffer.GetPtr()
				destinationOffset:DestinationOffset + DestinationBuffer.GetOffset()
							 size:Size];
	
	ConditionalSubmit();
}

void FAGXRenderPass::PresentTexture(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin)
{
	ConditionalSwitchToBlit();
	
	id<MTLBlitCommandEncoder> Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));

	[Encoder	  copyFromTexture:Texture.GetPtr()
					  sourceSlice:(NSUInteger)sourceSlice
					  sourceLevel:(NSUInteger)sourceLevel
					 sourceOrigin:*reinterpret_cast<MTLOrigin*>(&sourceOrigin)
					   sourceSize:*reinterpret_cast<MTLSize*>(&sourceSize)
						toTexture:toTexture.GetPtr()
				 destinationSlice:(NSUInteger)destinationSlice
				 destinationLevel:(NSUInteger)destinationLevel
				destinationOrigin:*reinterpret_cast<MTLOrigin*>(&destinationOrigin)];
}

void FAGXRenderPass::SynchronizeTexture(FAGXTexture const& Texture, uint32 Slice, uint32 Level)
{
	check(Texture);
#if PLATFORM_MAC
	ConditionalSwitchToBlit();
	
	id<MTLBlitCommandEncoder> Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	// METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	[Encoder synchronizeTexture:Texture.GetPtr() slice:(NSUInteger)Slice level:(NSUInteger)Level];
	
	ConditionalSubmit();
#endif
}

void FAGXRenderPass::SynchronizeResource(mtlpp::Resource const& Resource)
{
	check(Resource);
#if PLATFORM_MAC
	ConditionalSwitchToBlit();
	
	id<MTLBlitCommandEncoder> Encoder = CurrentEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	// METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	[Encoder synchronizeResource:Resource.GetPtr()];

	ConditionalSubmit();
#endif
}

void FAGXRenderPass::FillBuffer(FAGXBuffer const& Buffer, ns::Range Range, uint8 Value)
{
	check(Buffer);
	
	id<MTLBlitCommandEncoder> TargetEncoder = nil;
	bool bAsync = !CurrentEncoder.HasBufferBindingHistory(Buffer);
	if(bAsync)
	{
		ConditionalSwitchToAsyncBlit();
		TargetEncoder = PrologueEncoder.GetBlitCommandEncoder();
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(PrologueEncoder.GetCommandBufferStats(), FString::Printf(TEXT("FillBuffer: %p %llu %llu"), Buffer.GetPtr(), Buffer.GetOffset() + Range.Location, Range.Length)));
	}
	else
	{
		ConditionalSwitchToBlit();
		TargetEncoder = CurrentEncoder.GetBlitCommandEncoder();
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), FString::Printf(TEXT("FillBuffer: %p %llu %llu"), Buffer.GetPtr(), Buffer.GetOffset() + Range.Location, Range.Length)));
	}
	
	check(TargetEncoder);
	
	[TargetEncoder fillBuffer:Buffer.GetPtr() range:NSMakeRange(Range.Location + Buffer.GetOffset(), Range.Length) value:Value];
	
	if (!bAsync)
	{
		ConditionalSubmit();
	}
}

bool FAGXRenderPass::AsyncCopyFromBufferToTexture(FAGXBuffer const& Buffer, uint32 sourceOffset, uint32 sourceBytesPerRow, uint32 sourceBytesPerImage, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin, mtlpp::BlitOption options)
{
	id<MTLBlitCommandEncoder> TargetEncoder = nil;
	bool bAsync = !CurrentEncoder.HasTextureBindingHistory(toTexture);
	if(bAsync)
	{
		ConditionalSwitchToAsyncBlit();
		TargetEncoder = PrologueEncoder.GetBlitCommandEncoder();
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(PrologueEncoder.GetCommandBufferStats(), __FUNCTION__));
	}
	else
	{
		ConditionalSwitchToBlit();
		TargetEncoder = CurrentEncoder.GetBlitCommandEncoder();
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	}
	
	check(TargetEncoder);
	
	if (options == mtlpp::BlitOption::None)
	{
		[TargetEncoder		 copyFromBuffer:Buffer.GetPtr()
							   sourceOffset:(NSUInteger)sourceOffset + Buffer.GetOffset()
						  sourceBytesPerRow:(NSUInteger)sourceBytesPerRow
						sourceBytesPerImage:(NSUInteger)sourceBytesPerImage
								 sourceSize:*reinterpret_cast<MTLSize*>(&sourceSize)
								  toTexture:toTexture.GetPtr()
						   destinationSlice:(NSUInteger)destinationSlice
						   destinationLevel:(NSUInteger)destinationLevel
						  destinationOrigin:*reinterpret_cast<MTLOrigin*>(&destinationOrigin)];
	}
	else
	{
		[TargetEncoder		 copyFromBuffer:Buffer.GetPtr()
							   sourceOffset:(NSUInteger)sourceOffset + Buffer.GetOffset()
						  sourceBytesPerRow:(NSUInteger)sourceBytesPerRow
						sourceBytesPerImage:(NSUInteger)sourceBytesPerImage
								 sourceSize:*reinterpret_cast<MTLSize*>(&sourceSize)
								  toTexture:toTexture.GetPtr()
						   destinationSlice:(NSUInteger)destinationSlice
						   destinationLevel:(NSUInteger)destinationLevel
						  destinationOrigin:*reinterpret_cast<MTLOrigin*>(&destinationOrigin)
									options:*reinterpret_cast<MTLBlitOption*>(&options)];
	}
	
	return bAsync;
}

bool FAGXRenderPass::AsyncCopyFromTextureToTexture(FAGXTexture const& Texture, uint32 sourceSlice, uint32 sourceLevel, mtlpp::Origin sourceOrigin, mtlpp::Size sourceSize, FAGXTexture const& toTexture, uint32 destinationSlice, uint32 destinationLevel, mtlpp::Origin destinationOrigin)
{
	id<MTLBlitCommandEncoder> TargetEncoder = nil;
	bool bAsync = !CurrentEncoder.HasTextureBindingHistory(toTexture);
	if(bAsync)
	{
		ConditionalSwitchToAsyncBlit();
		TargetEncoder = PrologueEncoder.GetBlitCommandEncoder();
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(PrologueEncoder.GetCommandBufferStats(), __FUNCTION__));
	}
	else
	{
		ConditionalSwitchToBlit();
		TargetEncoder = CurrentEncoder.GetBlitCommandEncoder();
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	}
	
	check(TargetEncoder);
	
	[TargetEncoder	  copyFromTexture:Texture.GetPtr()
						  sourceSlice:(NSUInteger)sourceSlice
						  sourceLevel:(NSUInteger)sourceLevel
						 sourceOrigin:*reinterpret_cast<MTLOrigin*>(&sourceOrigin)
						   sourceSize:*reinterpret_cast<MTLSize*>(&sourceSize)
							toTexture:toTexture.GetPtr()
					 destinationSlice:(NSUInteger)destinationSlice
					 destinationLevel:(NSUInteger)destinationLevel
					destinationOrigin:*reinterpret_cast<MTLOrigin*>(&destinationOrigin)];

	return bAsync;
}

bool FAGXRenderPass::CanAsyncCopyToBuffer(FAGXBuffer const& DestinationBuffer)
{
	return !CurrentEncoder.HasBufferBindingHistory(DestinationBuffer);
}

void FAGXRenderPass::AsyncCopyFromBufferToBuffer(FAGXBuffer const& SourceBuffer, NSUInteger SourceOffset, FAGXBuffer const& DestinationBuffer, NSUInteger DestinationOffset, NSUInteger Size)
{
	id<MTLBlitCommandEncoder> TargetEncoder = nil;
	bool bAsync = !CurrentEncoder.HasBufferBindingHistory(DestinationBuffer);
	if(bAsync)
	{
		ConditionalSwitchToAsyncBlit();
		TargetEncoder = PrologueEncoder.GetBlitCommandEncoder();
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(PrologueEncoder.GetCommandBufferStats(), FString::Printf(TEXT("AsyncCopyFromBufferToBuffer: %p %llu %llu"), DestinationBuffer.GetPtr(), DestinationBuffer.GetOffset() + DestinationOffset, Size)));
	}
	else
	{
		ConditionalSwitchToBlit();
		TargetEncoder = CurrentEncoder.GetBlitCommandEncoder();
		METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), FString::Printf(TEXT("AsyncCopyFromBufferToBuffer: %p %llu %llu"), DestinationBuffer.GetPtr(), DestinationBuffer.GetOffset() + DestinationOffset, Size)));
	}
	
	check(TargetEncoder);
	
	[TargetEncoder	   copyFromBuffer:SourceBuffer.GetPtr()
						 sourceOffset:SourceOffset + SourceBuffer.GetOffset()
							 toBuffer:DestinationBuffer.GetPtr()
					destinationOffset:DestinationOffset + DestinationBuffer.GetOffset()
								 size:Size];
}

FAGXBuffer FAGXRenderPass::AllocateTemporyBufferForCopy(FAGXBuffer const& DestinationBuffer, NSUInteger Size, NSUInteger Align)
{
	FAGXBuffer Buffer;
	bool bAsync = !CurrentEncoder.HasBufferBindingHistory(DestinationBuffer);
	if(bAsync)
	{
		Buffer = PrologueEncoder.GetRingBuffer().NewBuffer(Size, Align);
	}
	else
	{
		Buffer = CurrentEncoder.GetRingBuffer().NewBuffer(Size, Align);
	}
	return Buffer;
}

void FAGXRenderPass::AsyncGenerateMipmapsForTexture(FAGXTexture const& Texture)
{
	// This must be a plain old error
	check(!CurrentEncoder.HasTextureBindingHistory(Texture));
	ConditionalSwitchToAsyncBlit();
	
	id<MTLBlitCommandEncoder> Encoder = PrologueEncoder.GetBlitCommandEncoder();
	check(Encoder);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));

	[Encoder generateMipmapsForTexture:Texture.GetPtr()];
}

void FAGXRenderPass::End(void)
{
	// EndEncoding should provide the encoder fence...
	if (PrologueEncoder.IsBlitCommandEncoderActive() || PrologueEncoder.IsComputeCommandEncoderActive())
	{
		PrologueEncoder.EndEncoding();
	}
	
	if (CmdList.IsImmediate() && IsWithinParallelPass() && CurrentEncoder.IsParallelRenderCommandEncoderActive())
	{
		State.SetRenderStoreActions(CurrentEncoder, false);
		CurrentEncoder.EndEncoding();
		
		ConditionalSwitchToBlit();
		CurrentEncoder.EndEncoding();
	}
	else if (CurrentEncoder.IsRenderCommandEncoderActive() || CurrentEncoder.IsBlitCommandEncoderActive() || CurrentEncoder.IsComputeCommandEncoderActive())
	{
		State.FlushVisibilityResults(CurrentEncoder);
		CurrentEncoder.EndEncoding();
	}
	
	State.SetRenderTargetsActive(false);
	
	RenderPassDesc = nil;
	bWithinRenderPass = false;
}

void FAGXRenderPass::InsertCommandBufferFence(FAGXCommandBufferFence& Fence, mtlpp::CommandBufferHandler Handler)
{
	CurrentEncoder.InsertCommandBufferFence(Fence, Handler);
}

void FAGXRenderPass::AddCompletionHandler(mtlpp::CommandBufferHandler Handler)
{
	CurrentEncoder.AddCompletionHandler(Handler);
}

void FAGXRenderPass::AddAsyncCommandBufferHandlers(mtlpp::CommandBufferHandler Scheduled, mtlpp::CommandBufferHandler Completion)
{
	check(PrologueEncoder.GetCommandBuffer() && PrologueEncoder.IsBlitCommandEncoderActive());
	if (Scheduled)
	{
		PrologueEncoder.GetCommandBuffer().AddScheduledHandler(Scheduled);
	}
	if (Completion)
	{
		PrologueEncoder.AddCompletionHandler(Completion);
	}
}

void FAGXRenderPass::TransitionResources(mtlpp::Resource const& Resource)
{
	PrologueEncoder.TransitionResources(Resource);
	CurrentEncoder.TransitionResources(Resource);
}

#pragma mark - Public Debug Support -

void FAGXRenderPass::InsertDebugEncoder()
{
	FAGXBuffer NewBuf = CurrentEncoder.GetRingBuffer().NewBuffer(BufferOffsetAlignment, BufferOffsetAlignment);
	check(NewBuf);
	
	ConditionalSwitchToBlit();
	
	id<MTLBlitCommandEncoder> TargetEncoder = CurrentEncoder.GetBlitCommandEncoder();
	check(TargetEncoder);
	
	METAL_GPUPROFILE(FAGXProfiler::GetProfiler()->EncodeBlit(CurrentEncoder.GetCommandBufferStats(), __FUNCTION__));
	
	[TargetEncoder fillBuffer:NewBuf.GetPtr() range:NSMakeRange(0 + NewBuf.GetOffset(), BufferOffsetAlignment) value:0xFF];
	
	ConditionalSubmit();
}

void FAGXRenderPass::InsertDebugSignpost(ns::String const& String)
{
	CurrentEncoder.InsertDebugSignpost(String);
	PrologueEncoder.InsertDebugSignpost(FString::Printf(TEXT("Prologue %s"), *FString(String.GetPtr())).GetNSString());
}

void FAGXRenderPass::PushDebugGroup(ns::String const& String)
{
	CurrentEncoder.PushDebugGroup(String);
	PrologueEncoder.PushDebugGroup(FString::Printf(TEXT("Prologue %s"), *FString(String.GetPtr())).GetNSString());
}

void FAGXRenderPass::PopDebugGroup(void)
{
	CurrentEncoder.PopDebugGroup();
	PrologueEncoder.PopDebugGroup();
}

#pragma mark - Public Accessors -
	
mtlpp::CommandBuffer const& FAGXRenderPass::GetCurrentCommandBuffer(void) const
{
	return CurrentEncoder.GetCommandBuffer();
}

mtlpp::CommandBuffer& FAGXRenderPass::GetCurrentCommandBuffer(void)
{
	return CurrentEncoder.GetCommandBuffer();
}
	
FAGXSubBufferRing& FAGXRenderPass::GetRingBuffer(void)
{
	return CurrentEncoder.GetRingBuffer();
}

void FAGXRenderPass::ShrinkRingBuffers(void)
{
	PrologueEncoder.GetRingBuffer().Shrink();
	CurrentEncoder.GetRingBuffer().Shrink();
}

bool FAGXRenderPass::IsWithinParallelPass(void)
{
	return bWithinRenderPass && CurrentEncoder.IsParallelRenderCommandEncoderActive();
}

id<MTLRenderCommandEncoder> FAGXRenderPass::GetParallelRenderCommandEncoder(uint32 Index, id<MTLParallelRenderCommandEncoder>* ParallelEncoder)
{
	check(IsWithinParallelPass());
	*ParallelEncoder = CurrentEncoder.GetParallelRenderCommandEncoder();
	return CurrentEncoder.GetChildRenderCommandEncoder(Index);
}

void FAGXRenderPass::ConditionalSwitchToRender(void)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXSwitchToRenderTime);
	
	check(bWithinRenderPass);
	check(RenderPassDesc);
	check(CmdList.IsParallel() || CurrentEncoder.GetCommandBuffer());
	
	if (CurrentEncoder.IsComputeCommandEncoderActive() || CurrentEncoder.IsBlitCommandEncoderActive())
	{
		CurrentEncoder.EndEncoding();
	}
	
	if (!CurrentEncoder.IsRenderCommandEncoderActive())
	{
		RestartRenderPass(nil);
	}
	
	check(CurrentEncoder.IsRenderCommandEncoderActive());
}

void FAGXRenderPass::ConditionalSwitchToCompute(void)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXSwitchToComputeTime);
	
	check(CurrentEncoder.GetCommandBuffer());
	check(!CurrentEncoder.IsParallel());
	
	if (CurrentEncoder.IsRenderCommandEncoderActive() || CurrentEncoder.IsBlitCommandEncoderActive())
	{
		if (CurrentEncoder.IsRenderCommandEncoderActive())
		{
			State.SetRenderStoreActions(CurrentEncoder, true);
			State.FlushVisibilityResults(CurrentEncoder);
		}
		CurrentEncoder.EndEncoding();
		State.SetRenderTargetsActive(false);
	}
	
	if (!CurrentEncoder.IsComputeCommandEncoderActive())
	{
		State.SetStateDirty();
		CurrentEncoder.BeginComputeCommandEncoding(ComputeDispatchType);
	}
	
	check(CurrentEncoder.IsComputeCommandEncoderActive());
}

void FAGXRenderPass::ConditionalSwitchToBlit(void)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXSwitchToBlitTime);
	
	check(CurrentEncoder.GetCommandBuffer());
	check(!CurrentEncoder.IsParallel());
	
	if (CurrentEncoder.IsRenderCommandEncoderActive() || CurrentEncoder.IsComputeCommandEncoderActive())
	{
		if (CurrentEncoder.IsRenderCommandEncoderActive())
		{
			State.SetRenderStoreActions(CurrentEncoder, true);
			State.FlushVisibilityResults(CurrentEncoder);
		}
		CurrentEncoder.EndEncoding();
		State.SetRenderTargetsActive(false);
	}
	
	if (!CurrentEncoder.IsBlitCommandEncoderActive())
	{
		CurrentEncoder.BeginBlitCommandEncoding();
	}
	
	check(CurrentEncoder.IsBlitCommandEncoderActive());
}

void FAGXRenderPass::ConditionalSwitchToAsyncBlit(void)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXSwitchToAsyncBlitTime);
	
	if (PrologueEncoder.IsComputeCommandEncoderActive() || PrologueEncoder.IsRenderCommandEncoderActive())
	{
		PrologueEncoder.EndEncoding();
	}
	
	if (!PrologueEncoder.IsBlitCommandEncoderActive())
	{
		if (!PrologueEncoder.GetCommandBuffer())
		{
			PrologueEncoder.StartCommandBuffer();
		}
		PrologueEncoder.BeginBlitCommandEncoding();
	}
	
	check(PrologueEncoder.IsBlitCommandEncoderActive());
}

void FAGXRenderPass::ConditionalSwitchToAsyncCompute(void)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXSwitchToComputeTime);
	
	if (PrologueEncoder.IsBlitCommandEncoderActive() || PrologueEncoder.IsRenderCommandEncoderActive())
	{
		PrologueEncoder.EndEncoding();
	}
	
	if (!PrologueEncoder.IsComputeCommandEncoderActive())
	{
		if (!PrologueEncoder.GetCommandBuffer())
		{
			PrologueEncoder.StartCommandBuffer();
		}
		State.SetStateDirty();
		PrologueEncoder.BeginComputeCommandEncoding(ComputeDispatchType);
	}
	
	check(PrologueEncoder.IsComputeCommandEncoderActive());
}

void FAGXRenderPass::CommitRenderResourceTables(void)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXCommitRenderResourceTablesTime);
	
	State.CommitRenderResources(&CurrentEncoder);
	
	State.CommitResourceTable(EAGXShaderStages::Vertex, mtlpp::FunctionType::Vertex, CurrentEncoder);
	
	FAGXGraphicsPipelineState const* BoundShaderState = State.GetGraphicsPSO();
	
	if (BoundShaderState->VertexShader->SideTableBinding >= 0)
	{
		CurrentEncoder.SetShaderSideTable(mtlpp::FunctionType::Vertex, BoundShaderState->VertexShader->SideTableBinding);
		State.SetShaderBuffer(EAGXShaderStages::Vertex, nil, nil, 0, 0, BoundShaderState->VertexShader->SideTableBinding, 0);
	}
	
	if (IsValidRef(BoundShaderState->PixelShader))
	{
		State.CommitResourceTable(EAGXShaderStages::Pixel, mtlpp::FunctionType::Fragment, CurrentEncoder);
		if (BoundShaderState->PixelShader->SideTableBinding >= 0)
		{
			CurrentEncoder.SetShaderSideTable(mtlpp::FunctionType::Fragment, BoundShaderState->PixelShader->SideTableBinding);
			State.SetShaderBuffer(EAGXShaderStages::Pixel, nil, nil, 0, 0, BoundShaderState->PixelShader->SideTableBinding, 0);
		}
	}
}

void FAGXRenderPass::CommitDispatchResourceTables(void)
{
	State.CommitComputeResources(&CurrentEncoder);
	
	State.CommitResourceTable(EAGXShaderStages::Compute, mtlpp::FunctionType::Kernel, CurrentEncoder);
	
	FAGXComputeShader const* ComputeShader = State.GetComputeShader();
	if (ComputeShader->SideTableBinding >= 0)
	{
		CurrentEncoder.SetShaderSideTable(mtlpp::FunctionType::Kernel, ComputeShader->SideTableBinding);
		State.SetShaderBuffer(EAGXShaderStages::Compute, nil, nil, 0, 0, ComputeShader->SideTableBinding, 0);
	}
}

void FAGXRenderPass::CommitAsyncDispatchResourceTables(void)
{
	State.CommitComputeResources(&PrologueEncoder);
	
	State.CommitResourceTable(EAGXShaderStages::Compute, mtlpp::FunctionType::Kernel, PrologueEncoder);
	
	FAGXComputeShader const* ComputeShader = State.GetComputeShader();
	if (ComputeShader->SideTableBinding >= 0)
	{
		PrologueEncoder.SetShaderSideTable(mtlpp::FunctionType::Kernel, ComputeShader->SideTableBinding);
		State.SetShaderBuffer(EAGXShaderStages::Compute, nil, nil, 0, 0, ComputeShader->SideTableBinding, 0);
	}
}

void FAGXRenderPass::PrepareToRender(uint32 PrimitiveType)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXPrepareToRenderTime);
	
	check(CurrentEncoder.GetCommandBuffer());
	check(CurrentEncoder.IsRenderCommandEncoderActive());
	
	// Set raster state
	State.SetRenderState(CurrentEncoder, nullptr);
	
	// Bind shader resources
	CommitRenderResourceTables();
    
    State.SetRenderPipelineState(CurrentEncoder, nullptr);
}

void FAGXRenderPass::PrepareToDispatch(void)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXPrepareToDispatchTime);
	
	check(CurrentEncoder.GetCommandBuffer());
	check(CurrentEncoder.IsComputeCommandEncoderActive());
	
	// Bind shader resources
	CommitDispatchResourceTables();
    
    State.SetComputePipelineState(CurrentEncoder);
}

void FAGXRenderPass::PrepareToAsyncDispatch(void)
{
	SCOPE_CYCLE_COUNTER(STAT_AGXPrepareToDispatchTime);
	
	check(PrologueEncoder.GetCommandBuffer());
	check(PrologueEncoder.IsComputeCommandEncoderActive());
	
	// Bind shader resources
	CommitAsyncDispatchResourceTables();
	
	State.SetComputePipelineState(PrologueEncoder);
}

void FAGXRenderPass::ConditionalSubmit()
{
	NumOutstandingOps++;
	
	bool bCanForceSubmit = State.CanRestartRenderPass();

	FRHIRenderPassInfo CurrentRenderTargets = State.GetRenderPassInfo();
	
	// Force a command-encoder when GAGXRuntimeDebugLevel is enabled to help track down intermittent command-buffer failures.
	if (GAGXCommandBufferCommitThreshold > 0 && NumOutstandingOps >= GAGXCommandBufferCommitThreshold && CmdList.GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelConditionalSubmit)
	{
		bool bCanChangeRT = true;
		
		if (bWithinRenderPass)
		{
			const bool bIsMSAAActive = State.GetHasValidRenderTarget() && State.GetSampleCount() != 1;
			bCanChangeRT = !bIsMSAAActive;
			
			for (int32 RenderTargetIndex = 0; bCanChangeRT && RenderTargetIndex < CurrentRenderTargets.GetNumColorRenderTargets(); RenderTargetIndex++)
			{
				FRHIRenderPassInfo::FColorEntry& RenderTargetView = CurrentRenderTargets.ColorRenderTargets[RenderTargetIndex];
				
				if (GetStoreAction(RenderTargetView.Action) != ERenderTargetStoreAction::EMultisampleResolve)
				{
					RenderTargetView.Action = MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore);
				}
				else
				{
					bCanChangeRT = false;
				}
			}
			
			if (bCanChangeRT && CurrentRenderTargets.DepthStencilRenderTarget.DepthStencilTarget)
			{
				if (GetStoreAction(GetDepthActions(CurrentRenderTargets.DepthStencilRenderTarget.Action)) != ERenderTargetStoreAction::EMultisampleResolve && GetStoreAction(GetStencilActions(CurrentRenderTargets.DepthStencilRenderTarget.Action)) != ERenderTargetStoreAction::EMultisampleResolve)
				{
					ERenderTargetActions Actions = MakeRenderTargetActions(ERenderTargetLoadAction::ELoad, ERenderTargetStoreAction::EStore);
					CurrentRenderTargets.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(Actions, Actions);
				}
				else
				{
					bCanChangeRT = false;
				}
			}
		}
		
		bCanForceSubmit = bCanChangeRT;
	}
	
	if (GAGXCommandBufferCommitThreshold > 0 && NumOutstandingOps > 0 && NumOutstandingOps >= GAGXCommandBufferCommitThreshold && bCanForceSubmit && !CurrentEncoder.IsParallel())
	{
		if (CurrentEncoder.GetCommandBuffer())
		{
			Submit(EAGXSubmitFlagsCreateCommandBuffer);
			NumOutstandingOps = 0;
		}
		
		// Force a command-encoder when GAGXRuntimeDebugLevel is enabled to help track down intermittent command-buffer failures.
		if (bWithinRenderPass && CmdList.GetCommandQueue().GetRuntimeDebuggingLevel() >= EAGXDebugLevelConditionalSubmit && State.GetHasValidRenderTarget())
		{
			bool bSet = false;
			State.InvalidateRenderTargets();
			if (IsFeatureLevelSupported( GMaxRHIShaderPlatform, ERHIFeatureLevel::SM5 ))
			{
				bSet = State.SetRenderPassInfo(CurrentRenderTargets, State.GetVisibilityResultsBuffer(), false);
			}
			else
			{
				bSet = State.SetRenderPassInfo(CurrentRenderTargets, NULL, false);
			}
			
			if (bSet)
			{
				RestartRenderPass(State.GetRenderPassDescriptor());
			}
		}
	}
}

uint32 FAGXRenderPass::GetEncoderIndex(void) const
{
	if (!CmdList.IsParallel())
	{
		return PrologueEncoder.NumEncodedPasses() + CurrentEncoder.NumEncodedPasses();
	}
	else
	{
		return GetAGXDeviceContext().GetCurrentRenderPass().GetEncoderIndex();
	}
}

uint32 FAGXRenderPass::GetCommandBufferIndex(void) const
{
	if (!CmdList.IsParallel())
	{
		return CurrentEncoder.GetCommandBufferIndex();
	}
	else
	{
		return GetAGXDeviceContext().GetCurrentRenderPass().GetCommandBufferIndex();
	}
}

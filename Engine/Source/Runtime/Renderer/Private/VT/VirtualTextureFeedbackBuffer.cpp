// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/VirtualTextureFeedbackBuffer.h"

#include "RenderGraphUtils.h"
#include "VT/VirtualTextureFeedback.h"

int32 GVirtualTextureFeedbackFactor = 16;
static FAutoConsoleVariableRef CVarVirtualTextureFeedbackFactor(
	TEXT("r.vt.FeedbackFactor"),
	GVirtualTextureFeedbackFactor,
	TEXT("The size of the VT feedback buffer is calculated by dividing the render resolution by this factor.")
	TEXT("The value set here is rounded up to the nearest power of two before use."),
	ECVF_RenderThreadSafe
);

int32 GetVirtualTextureFeedbackScale()
{
	// Round to nearest power of two to ensure that shader maths is efficient and sampling sequence logic is simple.
	return FMath::RoundUpToPowerOfTwo(FMath::Max(GVirtualTextureFeedbackFactor, 1));
}

FIntPoint GetVirtualTextureFeedbackBufferSize(FIntPoint InSceneTextureExtent)
{
	return FIntPoint::DivideAndRoundUp(InSceneTextureExtent, GetVirtualTextureFeedbackScale());
}

uint32 SampleVirtualTextureFeedbackSequence(uint32 InFrameIndex)
{
	const uint32 TileSize = GetVirtualTextureFeedbackScale();
	const uint32 TileSizeLog2 = FMath::CeilLogTwo(TileSize);
	const uint32 SequenceSize = FMath::Square(TileSize);
	const uint32 PixelIndex = InFrameIndex % SequenceSize;
	const uint32 PixelAddress = ReverseBits(PixelIndex) >> (32U - 2 * TileSizeLog2);
	const uint32 X = FMath::ReverseMortonCode2(PixelAddress);
	const uint32 Y = FMath::ReverseMortonCode2(PixelAddress >> 1);
	const uint32 PixelSequenceIndex = X + Y * TileSize;
	return PixelSequenceIndex;
}

void FVirtualTextureFeedbackBufferDesc::Init(int32 InBufferSize)
{
	BufferSize = FIntPoint(InBufferSize, 1);
	NumRects = 0;
	TotalReadSize = InBufferSize;
}

void FVirtualTextureFeedbackBufferDesc::Init2D(FIntPoint InBufferSize)
{
	BufferSize = InBufferSize;
	NumRects = 0;
	TotalReadSize = BufferSize.X * BufferSize.Y;
}

void FVirtualTextureFeedbackBufferDesc::Init2D(FIntPoint InUnscaledBufferSize, TArrayView<FIntRect> const& InUnscaledViewRects, int32 InBufferScale)
{
	const int32 BufferScale = FMath::Max(InBufferScale, 1);

	BufferSize = FIntPoint::DivideAndRoundUp(InUnscaledBufferSize, BufferScale);
	NumRects = 0;
	TotalReadSize = BufferSize.X * BufferSize.Y;

	if (InUnscaledViewRects.Num() > 0 && InUnscaledViewRects[0].Size() != InUnscaledBufferSize)
	{
		NumRects = FMath::Min((int32)MaxRectPerTransfer, InUnscaledViewRects.Num());
		TotalReadSize = 0;

		for (int32 RectIndex = 0; RectIndex < NumRects; ++RectIndex)
		{
			FIntRect const& Rect = InUnscaledViewRects[RectIndex];
			Rects[RectIndex].Min = FIntPoint::DivideAndRoundDown(Rect.Min, BufferScale);
			Rects[RectIndex].Max = FIntPoint::DivideAndRoundUp(Rect.Max, BufferScale);
			TotalReadSize += Rects[RectIndex].Area();
		}
	}
}

void FVirtualTextureFeedbackBuffer::Begin(FRDGBuilder& GraphBuilder, const FVirtualTextureFeedbackBufferDesc& InDesc)
{
	// NOTE: Transitions and allocations are handled manually right now, because the VT feedback UAV is used by
	// the view uniform buffer, which is not an RDG uniform buffer. If it can be factored out into its own RDG
	// uniform buffer (or put on the pass uniform buffers), then the resource can be fully converted to RDG.

	Desc = InDesc;

	FRDGBufferDesc BufferDesc(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), Desc.BufferSize.X * Desc.BufferSize.Y));
	BufferDesc.Usage |= BUF_SourceCopy;

	if (GetPooledFreeBuffer(GraphBuilder.RHICmdList, BufferDesc, PooledBuffer, TEXT("VirtualTextureFeedbackGPU")))
	{
		FRDGBufferUAVDesc UAVDesc;
		UAVDesc.Format = PF_R32_UINT;
		UAV = PooledBuffer->GetOrCreateUAV(UAVDesc);
	}

	AddPass(GraphBuilder, RDG_EVENT_NAME("VirtualTextureFeedbackClear"), [this](FRHICommandList& RHICmdList)
	{
		// Clear virtual texture feedback to default value
		RHICmdList.Transition(FRHITransitionInfo(UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		RHICmdList.ClearUAVUint(UAV, FUintVector4(~0u, ~0u, ~0u, ~0u));
		RHICmdList.Transition(FRHITransitionInfo(UAV, ERHIAccess::UAVCompute, ERHIAccess::UAVGraphics));
		RHICmdList.BeginUAVOverlap(UAV);
	});
}

void FVirtualTextureFeedbackBuffer::End(FRDGBuilder& GraphBuilder)
{
	AddPass(GraphBuilder, RDG_EVENT_NAME("VirtualTextureFeedbackCopy"), [this](FRHICommandListImmediate& RHICmdList)
	{
		RHICmdList.EndUAVOverlap(UAV);
		RHICmdList.Transition(FRHITransitionInfo(UAV, ERHIAccess::UAVGraphics, ERHIAccess::CopySrc));
		SubmitVirtualTextureFeedbackBuffer(RHICmdList, PooledBuffer->GetRHI(), Desc);
	});
}

void FVirtualTextureFeedbackBuffer::ReleaseRHI()
{
	PooledBuffer = nullptr;
	UAV = nullptr;
}

void SubmitVirtualTextureFeedbackBuffer(FRHICommandListImmediate& RHICmdList, FBufferRHIRef const& InBuffer, FVirtualTextureFeedbackBufferDesc const& InDesc)
{
	GVirtualTextureFeedback.TransferGPUToCPU(RHICmdList, InBuffer, InDesc);
}

void SubmitVirtualTextureFeedbackBuffer(class FRDGBuilder& GraphBuilder, FRDGBuffer* InBuffer, FVirtualTextureFeedbackBufferDesc const& InDesc)
{
	GVirtualTextureFeedback.TransferGPUToCPU(GraphBuilder, InBuffer, InDesc);
}

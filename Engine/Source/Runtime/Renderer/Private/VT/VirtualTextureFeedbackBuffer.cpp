// Copyright Epic Games, Inc. All Rights Reserved.

#include "VT/VirtualTextureFeedbackBuffer.h"
#include "VT/VirtualTextureFeedback.h"

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

void SubmitVirtualTextureFeedbackBuffer(FRHICommandListImmediate& RHICmdList, FVertexBufferRHIRef const& Buffer, FVirtualTextureFeedbackBufferDesc const& Desc)
{
	GVirtualTextureFeedback.TransferGPUToCPU(RHICmdList, Buffer, Desc);
}

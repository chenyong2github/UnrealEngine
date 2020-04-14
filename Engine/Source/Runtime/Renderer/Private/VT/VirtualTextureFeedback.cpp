// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualTextureFeedback.h"

#if PLATFORM_WINDOWS
// Use Query objects until RHI has a good fence on D3D11
#define USE_RHI_FENCES 0
#else
#define USE_RHI_FENCES 1
#endif

#if USE_RHI_FENCES

/** Container for GPU fences. */
class FFeedbackGPUFencePool
{
public:
	TArray<FGPUFenceRHIRef> Fences;

	FFeedbackGPUFencePool(int32 NumFences)
	{
		Fences.AddDefaulted(NumFences);
	}

	void InitRHI()
	{
	}

	void ReleaseRHI()
	{
		for (int i = 0; i < Fences.Num(); ++i)
		{
			Fences[i].SafeRelease();
		}
	}

	void Allocate(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		if (!Fences[Index])
		{
			Fences[Index] = RHICmdList.CreateGPUFence(FName(""));
		}
		Fences[Index]->Clear();
	}

	void Write(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		RHICmdList.WriteGPUFence(Fences[Index]);
	}

	bool Poll(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		return Fences[Index]->Poll();
	}

	FGPUFenceRHIRef GetMapFence(int32 Index)
	{
		return Fences[Index];
	}

	void Release(int32 Index)
	{
		Fences[Index].SafeRelease();
	}
};

#else // USE_RHI_FENCES

/** Container for GPU fences. Implemented as GPU Queries. */
class FFeedbackGPUFencePool
{
public:
	FRenderQueryPoolRHIRef FenceQueryPool;
	FGPUFenceRHIRef DummyFence;
	TArray<FRHIPooledRenderQuery> Fences;
	bool bDummyFenceWritten = false;

	FFeedbackGPUFencePool(int32 InSize)
	{
		Fences.AddDefaulted(InSize);
	}

	void InitRHI()
	{
		if (!FenceQueryPool.IsValid())
		{
			FenceQueryPool = RHICreateRenderQueryPool(RQT_AbsoluteTime, Fences.Num());
		}

		if (!DummyFence.IsValid())
		{
			DummyFence = RHICreateGPUFence(FName());
			bDummyFenceWritten = false;
		}
	}

	void ReleaseRHI()
	{
		for (int i = 0; i < Fences.Num(); ++i)
		{
			if (Fences[i].IsValid())
			{
				Fences[i].ReleaseQuery();
			}
		}

		DummyFence.SafeRelease();
		bDummyFenceWritten = false;

		FenceQueryPool.SafeRelease();
	}

	void Allocate(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		if (!Fences[Index].IsValid())
		{
			Fences[Index] = FenceQueryPool->AllocateQuery();
		}

		if (!bDummyFenceWritten && DummyFence.IsValid())
		{
			// Write dummy fence one time on first Allocate
			// After that we want it to always Poll() true
			RHICmdList.WriteGPUFence(DummyFence);
			bDummyFenceWritten = true;
		}
	}
	
	void Write(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		RHICmdList.EndRenderQuery(Fences[Index].GetQuery());
	}

	bool Poll(FRHICommandListImmediate& RHICmdList, int32 Index)
	{
		uint64 Dummy;
		return RHICmdList.GetRenderQueryResult(Fences[Index].GetQuery(), Dummy, false);
	}

	FGPUFenceRHIRef GetMapFence(int32 Index)
	{
		return DummyFence;
	}

	void Release(int32 Index)
	{
		Fences[Index].ReleaseQuery();
	}
};

#endif // USE_RHI_FENCES

void FVirtualTextureFeedback::FBufferDesc::Init(int32 InBufferSize)
{
	BufferSize = FIntPoint(InBufferSize, 1);
	NumRects = 0;
	TotalReadSize = InBufferSize;
}

void FVirtualTextureFeedback::FBufferDesc::Init2D(FIntPoint InBufferSize)
{
	BufferSize = InBufferSize;
	NumRects = 0;
	TotalReadSize = BufferSize.X * BufferSize.Y;
}

void FVirtualTextureFeedback::FBufferDesc::Init2D(FIntPoint InUnscaledBufferSize, TArrayView<FIntRect> const& InUnscaledViewRects, int32 InBufferScale)
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

FVirtualTextureFeedback::FVirtualTextureFeedback()
	: NumPending(0)
	, WriteIndex(0)
	, ReadIndex(0)
{
	Fences = new FFeedbackGPUFencePool(MaxTransfers);
}

FVirtualTextureFeedback::~FVirtualTextureFeedback()
{
	delete Fences;
}

void FVirtualTextureFeedback::InitRHI()
{
	for (int32 Index = 0; Index < MaxTransfers; ++Index)
	{
		FeedbackItems[Index].StagingBuffer = RHICreateStagingBuffer();
	}

	Fences->InitRHI();
}

void FVirtualTextureFeedback::ReleaseRHI()
{
	for (int32 Index = 0; Index < MaxTransfers; ++Index)
	{
		FeedbackItems[Index].StagingBuffer.SafeRelease();
	}

	Fences->ReleaseRHI();
}

void FVirtualTextureFeedback::TransferGPUToCPU(FRHICommandListImmediate& RHICmdList, FVertexBufferRHIRef const& Buffer, FBufferDesc const& Desc)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_VirtualTextureFeedback_TransferGPUToCPU);

	if (NumPending >= MaxTransfers)
	{
		// If we have too many pending transfers, start throwing away the oldest in the ring buffer.
		// We will need to allocate a new fence, since the previous fence will still be set on the old CopyToResolveTarget command (which we will now ignore/discard).
		Fences->Release(ReadIndex);
		NumPending --;
		ReadIndex = (ReadIndex + 1) % MaxTransfers;
	}

	FFeedbackItem& FeedbackItem = FeedbackItems[WriteIndex];
	FeedbackItem.Desc = Desc;

	// We only need to transfer 1 copy of the data, so restrict mask to the first active GPU.
	FeedbackItem.GPUMask = FRHIGPUMask::FromIndex(RHICmdList.GetGPUMask().GetFirstIndex());
	SCOPED_GPU_MASK(RHICmdList, FeedbackItem.GPUMask);

	RHICmdList.CopyToStagingBuffer(Buffer, FeedbackItem.StagingBuffer, 0, Desc.BufferSize.X * Desc.BufferSize.Y * sizeof(uint32));

	Fences->Allocate(RHICmdList, WriteIndex);
	Fences->Write(RHICmdList, WriteIndex);

	// Increment the ring buffer write position.
	WriteIndex = (WriteIndex + 1) % MaxTransfers;
	++NumPending;
}

bool FVirtualTextureFeedback::CanMap(FRHICommandListImmediate& RHICmdList)
{
	return NumPending > 0u && Fences->Poll(RHICmdList, ReadIndex);
}

FVirtualTextureFeedback::FMapResult FVirtualTextureFeedback::Map(FRHICommandListImmediate& RHICmdList, int32 MaxTransfersToMap)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_VirtualTextureFeedback_Map);

	FVirtualTextureFeedback::FMapResult MapResult;

	// Calculate number and size of available results.
	int32 NumResults = 0;
	int32 NumRects = 0;
	int32 TotalReadSize = 0;
	for (int32 ResultIndex = 0; ResultIndex < MaxTransfersToMap && ResultIndex < NumPending; ++ResultIndex)
	{
		const int32 FeedbackIndex = (ReadIndex + ResultIndex) % MaxTransfers;
		FBufferDesc const& FeedbackItemDesc = FeedbackItems[FeedbackIndex].Desc;

		if (!Fences->Poll(RHICmdList, FeedbackIndex))
		{
			break;
		}

		NumResults ++;
		NumRects += FeedbackItemDesc.NumRects;
		TotalReadSize += FeedbackItemDesc.TotalReadSize;
	}

	// Fetch the valid results.
	if (NumResults > 0)
	{
		// Get a FMapResources object to store anything that will need cleaning up on Unmap()
		MapResult.MapHandle = FreeMapResources.Num() ? FreeMapResources.Pop() : MapResources.AddDefaulted();

		if (NumResults == 1 && NumRects == 0)
		{
			// If only one target with no rectangles then fast path is to return the locked buffer.
			const int32 FeedbackIndex = ReadIndex;
			FBufferDesc const& FeedbackItemDesc = FeedbackItems[FeedbackIndex].Desc;
			FRHIGPUMask GPUMask = FeedbackItems[FeedbackIndex].GPUMask;
			FStagingBufferRHIRef StagingBuffer = FeedbackItems[FeedbackIndex].StagingBuffer;
			
			SCOPED_GPU_MASK(RHICmdList, GPUMask);
			const int32 BufferSize = FeedbackItemDesc.BufferSize.X * FeedbackItemDesc.BufferSize.Y;
			MapResult.Data = (uint32*)RHICmdList.LockStagingBuffer(StagingBuffer, Fences->GetMapFence(FeedbackIndex), 0, BufferSize * sizeof(uint32));
			MapResult.Size = BufferSize;

			// Store index so that we can unlock staging buffer when we call Unmap().
			MapResources[MapResult.MapHandle].FeedbackItemToUnlockIndex = FeedbackIndex;
		}
		else
		{
			// Concatenate the results to a single buffer (stored in the MapResources) and return that.
			MapResources[MapResult.MapHandle].ResultData.SetNumUninitialized(TotalReadSize, false);
			MapResult.Data = MapResources[MapResult.MapHandle].ResultData.GetData();
			MapResult.Size = 0;

			for (int32 ResultIndex = 0; ResultIndex < NumResults; ++ResultIndex)
			{
				const int32 FeedbackIndex = (ReadIndex + ResultIndex) % MaxTransfers;
				FBufferDesc const& FeedbackItemDesc = FeedbackItems[FeedbackIndex].Desc;
				FRHIGPUMask GPUMask = FeedbackItems[FeedbackIndex].GPUMask;
				FStagingBufferRHIRef StagingBuffer = FeedbackItems[FeedbackIndex].StagingBuffer;

				SCOPED_GPU_MASK(RHICmdList, GPUMask);
				const int32 BufferSize = FeedbackItemDesc.BufferSize.X * FeedbackItemDesc.BufferSize.Y;
				uint32 const* Data = (uint32*)RHICmdList.LockStagingBuffer(StagingBuffer, Fences->GetMapFence(FeedbackIndex), 0, BufferSize * sizeof(uint32));

				if (FeedbackItemDesc.NumRects == 0)
				{
					// Copy full buffer
					FMemory::Memcpy(MapResult.Data + MapResult.Size, Data, BufferSize * sizeof(uint32));
					MapResult.Size += BufferSize;
				}
				else
				{
					// Copy individual rectangles from the buffer
					const int32 BufferWidth = FeedbackItemDesc.BufferSize.X;
					for (int32 RectIndex = 0; RectIndex < FeedbackItemDesc.NumRects; ++RectIndex)
					{
						const FIntRect Rect = FeedbackItemDesc.Rects[RectIndex];
						const int32 RectWidth = Rect.Width();
						const int32 RectHeight = Rect.Height();

						uint32 const* Src = Data + Rect.Min.Y * BufferWidth + Rect.Min.X;
						uint32* Dst = MapResult.Data + MapResult.Size;

						for (int32 y = 0; y < RectHeight; ++y)
						{
							FMemory::Memcpy(Dst, Src, RectWidth * sizeof(uint32));

							Src += BufferWidth;
							Dst += RectWidth;
						}

						MapResult.Size += RectWidth * RectHeight;
					}
				}

				RHICmdList.UnlockStagingBuffer(StagingBuffer);
			}
		}

		check(MapResult.Size == TotalReadSize)
	
		// Increment the ring buffer read position.
		NumPending -= NumResults;
		ReadIndex = (ReadIndex + NumResults) % MaxTransfers;
	}

	return MapResult;
}

FVirtualTextureFeedback::FMapResult FVirtualTextureFeedback::Map(FRHICommandListImmediate& RHICmdList)
{
	return Map(RHICmdList, MaxTransfers);
}

void FVirtualTextureFeedback::Unmap(FRHICommandListImmediate& RHICmdList, int32 MapHandle)
{
	if (MapHandle >= 0)
	{
		FMapResources& Resources = MapResources[MapHandle];

		// Do any required buffer Unlock.
		if (Resources.FeedbackItemToUnlockIndex >= 0)
		{
			SCOPED_GPU_MASK(RHICmdList, FeedbackItems[Resources.FeedbackItemToUnlockIndex].GPUMask);
			RHICmdList.UnlockStagingBuffer(FeedbackItems[Resources.FeedbackItemToUnlockIndex].StagingBuffer);
			Resources.FeedbackItemToUnlockIndex = -1;
		}

		// Reset any allocated data buffer.
		Resources.ResultData.Reset();

		// Return to free list.
		FreeMapResources.Add(MapHandle);
	}
}

TGlobalResource< FVirtualTextureFeedback > GVirtualTextureFeedback;

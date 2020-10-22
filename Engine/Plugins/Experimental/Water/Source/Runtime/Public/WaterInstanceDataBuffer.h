// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

template <bool bWithWaterSelectionSupport>
class TWaterInstanceDataBuffers
{
public:
	static constexpr int32 NumBuffers = bWithWaterSelectionSupport ? 3 : 2;

	TWaterInstanceDataBuffers(int32 InInstanceCount)
	{
		ENQUEUE_RENDER_COMMAND(AllocateWaterInstanceDataBuffer)
		(
			[this, InInstanceCount](FRHICommandListImmediate& RHICmdList)
			{
				FRHIResourceCreateInfo CreateInfo;

				int32 SizeInBytes = Align<int32>(InInstanceCount * sizeof(FVector4), 4 * 1024);

				for (int32 i = 0; i < NumBuffers; ++i)
				{
					Buffer[i] = RHICreateVertexBuffer(SizeInBytes, BUF_Dynamic, CreateInfo);
					BufferMemory[i] = nullptr;
				}
			}
		);
	}

	~TWaterInstanceDataBuffers()
	{
		for (int32 i = 0; i < NumBuffers; ++i)
		{
			Buffer[i].SafeRelease();
		}
	}

	void Lock(int32 InInstanceCount)
	{
		for (int32 i = 0; i < NumBuffers; ++i)
		{
			BufferMemory[i] = Lock(InInstanceCount, i);
		}
	}

	void Unlock()
	{
		for (int32 i = 0; i < NumBuffers; ++i)
		{
			Unlock(i);
			BufferMemory[i] = nullptr;
		}
	}

	FVertexBufferRHIRef GetBuffer(int32 InBufferID) const
	{
		return Buffer[InBufferID];
	}

	FVector4* GetBufferMemory(int32 InBufferID) const
	{
		check(BufferMemory[InBufferID]);
		return BufferMemory[InBufferID];
	}

private:
	FVector4* Lock(int32 InInstanceCount, int32 InBufferID)
	{
		check(IsInRenderingThread());

		uint32 SizeInBytes = InInstanceCount * sizeof(FVector4);

		if (SizeInBytes > Buffer[InBufferID]->GetSize())
		{
			Buffer[InBufferID].SafeRelease();

			FRHIResourceCreateInfo CreateInfo;

			// Align size in to avoid reallocating for a few differences of instance count
			uint32 AlignedSizeInBytes = Align<uint32>(SizeInBytes, 4 * 1024);

			Buffer[InBufferID] = RHICreateVertexBuffer(AlignedSizeInBytes, BUF_Dynamic, CreateInfo);
		}

		return reinterpret_cast<FVector4*>(RHILockVertexBuffer(Buffer[InBufferID], 0, SizeInBytes, RLM_WriteOnly));
	}

	void Unlock(int32 InBufferID)
	{
		RHIUnlockVertexBuffer(Buffer[InBufferID]);
	}

	FVertexBufferRHIRef Buffer[NumBuffers];
	FVector4* BufferMemory[NumBuffers];
};

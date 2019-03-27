// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	NiagaraCutoutVertexBuffer.h: Niagara cutout uv buffer implementation.
=============================================================================*/

#include "NiagaraCutoutVertexBuffer.h"

FNiagaraCutoutVertexBuffer::FNiagaraCutoutVertexBuffer(int32 ZeroInitCount)
{
	if (ZeroInitCount > 0)
	{
		Data.AddZeroed(ZeroInitCount);
	}
}

void FNiagaraCutoutVertexBuffer::InitRHI()
{
	if (Data.Num())
	{
		// create a static vertex buffer
		FRHIResourceCreateInfo CreateInfo;
		void* BufferData = nullptr;

		const int32 DataSize = Data.Num() * sizeof(FVector2D);
		VertexBufferRHI = RHICreateAndLockVertexBuffer(DataSize, BUF_Static | BUF_ShaderResource, CreateInfo, BufferData);
		FMemory::Memcpy(BufferData, Data.GetData(), DataSize);
		RHIUnlockVertexBuffer(VertexBufferRHI);
		VertexBufferSRV = RHICreateShaderResourceView(VertexBufferRHI, sizeof(FVector2D), PF_G32R32F);

		Data.Empty();
	}
}

void FNiagaraCutoutVertexBuffer::ReleaseRHI()
{
	VertexBufferSRV.SafeRelease();
	FVertexBuffer::ReleaseRHI();
}

TGlobalResource<FNiagaraCutoutVertexBuffer> GFNiagaraNullCutoutVertexBuffer(4);


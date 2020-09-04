// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DStreamIn.cpp: Stream in helper for 2D textures.
=============================================================================*/

#include "Streaming/Texture2DStreamIn.h"
#include "RenderUtils.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

FTexture2DStreamIn::FTexture2DStreamIn(UTexture2D* InTexture)
	: FTexture2DUpdate(InTexture)
{
	ensure(ResourceState.NumRequestedLODs > ResourceState.NumResidentLODs);
	MipData.AddZeroed(ResourceState.MaxNumLODs);
}

FTexture2DStreamIn::~FTexture2DStreamIn()
{
#if DO_CHECK
	for (void* ThisMipData : MipData)
	{
		check(!ThisMipData);
	}
#endif
}

void FTexture2DStreamIn::DoAllocateNewMips(const FContext& Context)
{
	if (!IsCancelled() && Context.Resource)
	{
		for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx; ++MipIndex)
		{
			const FTexture2DMipMap& MipMap = *Context.MipsView[MipIndex];
			const int32 MipSize = CalcTextureMipMapSize(MipMap.SizeX, MipMap.SizeY, Context.Resource->GetPixelFormat(), 0);

			check(!MipData[MipIndex]);
			MipData[MipIndex] = FMemory::Malloc(MipSize);
		}
	}
}

void FTexture2DStreamIn::DoFreeNewMips(const FContext& Context)
{
	for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx; ++MipIndex)
	{
		if (MipData[MipIndex])
		{
			FMemory::Free(MipData[MipIndex]);
			MipData[MipIndex] = nullptr;
		}
	}
}

void FTexture2DStreamIn::DoLockNewMips(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);

	if (!IsCancelled() && IntermediateTextureRHI && Context.Resource)
	{
		// With virtual textures, all mips exist although they might not be allocated.
		const int32 MipOffset = !!(IntermediateTextureRHI->GetFlags() & TexCreate_Virtual) ? 0 : PendingFirstLODIdx;

		for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx; ++MipIndex)
		{
			check(!MipData[MipIndex]);
			uint32 DestPitch = 0;
			MipData[MipIndex] = RHILockTexture2D(IntermediateTextureRHI, MipIndex - MipOffset, RLM_WriteOnly, DestPitch, false, CVarFlushRHIThreadOnSTreamingTextureLocks.GetValueOnAnyThread() > 0);
		}
	}
}


void FTexture2DStreamIn::DoUnlockNewMips(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);

	if (IntermediateTextureRHI && Context.Resource)
	{
		// With virtual textures, all mips exist although they might not be allocated.
		const int32 MipOffset = !!(IntermediateTextureRHI->GetFlags() & TexCreate_Virtual) ? 0 : PendingFirstLODIdx;

		for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx; ++MipIndex)
		{
			if (MipData[MipIndex])
			{
				RHIUnlockTexture2D(IntermediateTextureRHI, MipIndex - MipOffset, false, CVarFlushRHIThreadOnSTreamingTextureLocks.GetValueOnAnyThread() > 0 );
				MipData[MipIndex] = nullptr;
			}
		}
	}
}

void FTexture2DStreamIn::DoCopySharedMips(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);

	if (!IsCancelled() && IntermediateTextureRHI && Context.Resource)
	{
		RHICopySharedMips(IntermediateTextureRHI, Context.Resource->GetTexture2DRHI());
	}
}

// Async create the texture to the requested size.
void FTexture2DStreamIn::DoAsyncCreateWithNewMips(const FContext& Context)
{
	check(Context.CurrentThread == TT_Async);

	if (!IsCancelled() && Context.Resource)
	{
		const FTexture2DMipMap& RequestedMipMap = *Context.MipsView[PendingFirstLODIdx];
		ensure(!IntermediateTextureRHI);

		IntermediateTextureRHI = RHIAsyncCreateTexture2D(
			RequestedMipMap.SizeX,
			RequestedMipMap.SizeY,
			Context.Resource->GetPixelFormat(),
			ResourceState.NumRequestedLODs,
			Context.Resource->GetCreationFlags(),
			&MipData[PendingFirstLODIdx],
			ResourceState.NumRequestedLODs - ResourceState.NumResidentLODs);
	}
}

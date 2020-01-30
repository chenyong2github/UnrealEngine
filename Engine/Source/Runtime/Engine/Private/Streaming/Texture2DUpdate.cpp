// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
Texture2DUpdate.cpp: Helpers to stream in and out mips.
=============================================================================*/

#include "Streaming/Texture2DUpdate.h"
#include "RenderUtils.h"
#include "Containers/ResourceArray.h"
#include "Streaming/RenderAssetUpdate.inl"

// Instantiate TRenderAssetUpdate for FTexture2DUpdateContext
template class TRenderAssetUpdate<FTexture2DUpdateContext>;

#if STATS
extern volatile int64 GPending2DUpdateCount;
volatile int64 GPending2DUpdateCount = 0;
#endif

FTexture2DUpdateContext::FTexture2DUpdateContext(UTexture2D* InTexture, EThreadType InCurrentThread)
	: Texture(InTexture)
	, CurrentThread(InCurrentThread)
{
	check(InTexture);
	checkSlow(InCurrentThread != FTexture2DUpdate::TT_Render || IsInRenderingThread());
	Resource = static_cast<FTexture2DResource*>(Texture->Resource);
}

FTexture2DUpdateContext::FTexture2DUpdateContext(UStreamableRenderAsset* InTexture, EThreadType InCurrentThread)
#if UE_BUILD_SHIPPING
	: FTexture2DUpdateContext(static_cast<UTexture2D*>(InTexture), InCurrentThread)
#else
	: FTexture2DUpdateContext(Cast<UTexture2D>(InTexture), InCurrentThread)
#endif
{}

FTexture2DUpdate::FTexture2DUpdate(UTexture2D* InTexture, int32 InRequestedMips) 
	: TRenderAssetUpdate<FTexture2DUpdateContext>(InTexture, InRequestedMips)
{
	if (!InTexture->Resource)
	{
		RequestedMips = INDEX_NONE;
		PendingFirstMip = INDEX_NONE;
		bIsCancelled = true;
	}

	STAT(FPlatformAtomics::InterlockedIncrement(&GPending2DUpdateCount));
}

FTexture2DUpdate::~FTexture2DUpdate()
{
	ensure(!IntermediateTextureRHI);

	STAT(FPlatformAtomics::InterlockedDecrement(&GPending2DUpdateCount));
}


// ****************************
// ********* Helpers **********
// ****************************

void FTexture2DUpdate::DoAsyncReallocate(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);

	if (!IsCancelled() && Context.Texture && Context.Resource)
	{
		const FTexture2DMipMap& RequestedMipMap = Context.Texture->GetPlatformMips()[PendingFirstMip];

		TaskSynchronization.Set(1);

		ensure(!IntermediateTextureRHI);

		IntermediateTextureRHI = RHIAsyncReallocateTexture2D(
			Context.Resource->GetTexture2DRHI(),
			RequestedMips,
			RequestedMipMap.SizeX,
			RequestedMipMap.SizeY,
			&TaskSynchronization);
	}
}


//  Transform the texture into a virtual texture.
void FTexture2DUpdate::DoConvertToVirtualWithNewMips(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);

	if (!IsCancelled() && Context.Texture && Context.Resource)
	{
		// If the texture is not virtual, then make it virtual immediately.
		const FTexture2DRHIRef Texture2DRHI = Context.Resource->Texture2DRHI;
		if ((Texture2DRHI->GetFlags() & TexCreate_Virtual) != TexCreate_Virtual)
		{
			const TIndirectArray<FTexture2DMipMap>& OwnerMips = Context.Texture->GetPlatformMips();
			const uint32 TexCreateFlags = Texture2DRHI->GetFlags() | TexCreate_Virtual;

			ensure(!IntermediateTextureRHI);

			// Create a copy of the texture that is a virtual texture.
			FRHIResourceCreateInfo CreateInfo(Context.Resource->ResourceMem);
			IntermediateTextureRHI = RHICreateTexture2D(OwnerMips[0].SizeX, OwnerMips[0].SizeY, Texture2DRHI->GetFormat(), OwnerMips.Num(), 1, TexCreateFlags, CreateInfo);
			RHIVirtualTextureSetFirstMipInMemory(IntermediateTextureRHI, Context.Resource->GetCurrentFirstMip());
			RHIVirtualTextureSetFirstMipVisible(IntermediateTextureRHI, Context.Resource->GetCurrentFirstMip());
			RHICopySharedMips(IntermediateTextureRHI, Texture2DRHI);
		}
		else
		{
			// Otherwise the current texture is already virtual and we can update it directly.
			IntermediateTextureRHI = Context.Resource->GetTexture2DRHI();
		}
		RHIVirtualTextureSetFirstMipInMemory(IntermediateTextureRHI, PendingFirstMip);
	}
}

bool FTexture2DUpdate::DoConvertToNonVirtual(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);

	// If the texture is virtual, then create a new copy of the texture.
	if (!IsCancelled() && !IntermediateTextureRHI && Context.Texture && Context.Resource)
	{
		const FTexture2DRHIRef Texture2DRHI = Context.Resource->Texture2DRHI;
		if ((Texture2DRHI->GetFlags() & TexCreate_Virtual) == TexCreate_Virtual)
		{
			const TIndirectArray<FTexture2DMipMap>& OwnerMips = Context.Texture->GetPlatformMips();
			const uint32 TexCreateFlags = Context.Resource->Texture2DRHI->GetFlags() & ~TexCreate_Virtual;

			ensure(!IntermediateTextureRHI);
			FRHIResourceCreateInfo CreateInfo(Context.Resource->ResourceMem);
			IntermediateTextureRHI = RHICreateTexture2D(OwnerMips[PendingFirstMip].SizeX, OwnerMips[PendingFirstMip].SizeY, Texture2DRHI->GetFormat(), OwnerMips.Num() - PendingFirstMip, 1, TexCreateFlags, CreateInfo);
			RHICopySharedMips(IntermediateTextureRHI, Texture2DRHI);

			return true;
		}
	}
	return false;
}

void FTexture2DUpdate::DoFinishUpdate(const FContext& Context)
{
	check(Context.CurrentThread == TT_Render);

	if (IntermediateTextureRHI && Context.Resource)
	{
		if (!IsCancelled())
		{
			Context.Resource->UpdateTexture(IntermediateTextureRHI, PendingFirstMip);
		}
		IntermediateTextureRHI.SafeRelease();

	}
}

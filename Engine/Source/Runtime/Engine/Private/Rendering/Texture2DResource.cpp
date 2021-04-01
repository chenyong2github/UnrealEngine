// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Texture2DResource.cpp: Implementation of FTexture2DResource used  by streamable UTexture2D.
=============================================================================*/

#include "Rendering/Texture2DResource.h"
#include "Engine/Texture2D.h"
#include "RenderUtils.h"

// TODO Only adding this setting to allow backwards compatibility to be forced.  The default
// behavior is to NOT do this.  This variable should be removed in the future.  #ADDED 4.13
static TAutoConsoleVariable<int32> CVarForceHighestMipOnUITexturesEnabled(
	TEXT("r.ForceHighestMipOnUITextures"),
	0,
	TEXT("If set to 1, texutres in the UI Group will have their highest mip level forced."),
	ECVF_RenderThreadSafe);

/**
 * Minimal initialization constructor.
 *
 * @param InOwner			UTexture2D which this FTexture2DResource represents.
 * @param InState			
 */
FTexture2DResource::FTexture2DResource(UTexture2D* InOwner, const FStreamableRenderResourceState& InState)
	: FStreamableTextureResource(InOwner, InOwner->PlatformData, InState, true)
	, ResourceMem( InOwner->ResourceMem )
{
	// Retrieve initial mip data.
	MipData.AddZeroed(State.MaxNumLODs);
	InOwner->GetMipData(State.LODCountToAssetFirstLODIdx(State.NumRequestedLODs), MipData.GetData() + State.LODCountToFirstLODIdx(State.NumRequestedLODs));

	CacheSamplerStateInitializer(InOwner);
}

/**
 * Destructor, freeing MipData in the case of resource being destroyed without ever 
 * having been initialized by the rendering thread via InitRHI.
 */
FTexture2DResource::~FTexture2DResource()
{
	// free resource memory that was preallocated
	// The deletion needs to happen in the rendering thread.
	FTexture2DResourceMem* InResourceMem = ResourceMem;
	ENQUEUE_RENDER_COMMAND(DeleteResourceMem)(
		[InResourceMem](FRHICommandList& RHICmdList)
		{
			delete InResourceMem;
		});

	// Make sure we're not leaking memory if InitRHI has never been called.
	for( int32 MipIndex=0; MipIndex<MipData.Num(); MipIndex++ )
	{
		// free any mip data that was copied 
		if( MipData[MipIndex] )
		{
			FMemory::Free( MipData[MipIndex] );
		}
		MipData[MipIndex] = NULL;
	}
}

void FTexture2DResource::CacheSamplerStateInitializer(const UTexture2D* InOwner)
{
	float DefaultMipBias = 0;
	if (PlatformData && LODGroup == TEXTUREGROUP_UI && CVarForceHighestMipOnUITexturesEnabled.GetValueOnAnyThread() > 0)
	{
		DefaultMipBias = -PlatformData->Mips.Num();
	}

	// Set FStreamableTextureResource sampler state settings as it is UTexture2D specific.
	AddressU = InOwner->AddressX == TA_Wrap ? AM_Wrap : (InOwner->AddressX == TA_Clamp ? AM_Clamp : AM_Mirror);
	AddressV = InOwner->AddressY == TA_Wrap ? AM_Wrap : (InOwner->AddressY == TA_Clamp ? AM_Clamp : AM_Mirror);
	MipBias = UTexture2D::GetGlobalMipMapLODBias() + DefaultMipBias;
}


void FTexture2DResource::CreateTexture()
{
	FTexture2DRHIRef Texture2DRHI;
	const int32 RequestedMipIdx = State.RequestedFirstLODIdx();
	const FTexture2DMipMap* RequestedMip = GetPlatformMip(RequestedMipIdx);

	// create texture with ResourceMem data when available
	FRHIResourceCreateInfo CreateInfo(ResourceMem);
	CreateInfo.ExtData = PlatformData->GetExtData();
	Texture2DRHI = RHICreateTexture2D( RequestedMip->SizeX, RequestedMip->SizeY, PixelFormat, State.NumRequestedLODs, 1, CreationFlags, CreateInfo);

	// if( ResourceMem && !State.bReadyForStreaming) // To investigate!
	if (ResourceMem)
	{
		// when using resource memory the RHI texture has already been initialized with data and won't need to have mips copied
		check(State.NumRequestedLODs == ResourceMem->GetNumMips());
		check(RequestedMip->SizeX == ResourceMem->GetSizeX() && RequestedMip->SizeY == ResourceMem->GetSizeY());
		for( int32 MipIndex=0; MipIndex < MipData.Num(); MipIndex++ )
		{
			MipData[MipIndex] = nullptr;
		}
	}
	else
	{
		// Read the resident mip-levels into the RHI texture.
		for (int32 RHIMipIdx = 0; RHIMipIdx < State.NumRequestedLODs; ++RHIMipIdx)
		{
			const int32 ResourceMipIdx = RHIMipIdx + RequestedMipIdx;
			if (MipData[ResourceMipIdx])
			{
				uint32 DestPitch;
				void* TheMipData = RHILockTexture2D( Texture2DRHI, RHIMipIdx, RLM_WriteOnly, DestPitch, false );
				GetData( ResourceMipIdx, TheMipData, DestPitch );
				RHIUnlockTexture2D( Texture2DRHI, RHIMipIdx, false );
			}
		}
	}
	TextureRHI = Texture2DRHI;
}

void FTexture2DResource::CreatePartiallyResidentTexture()
{
	FTexture2DRHIRef Texture2DRHI;
	const int32 CurrentFirstMip = State.RequestedFirstLODIdx();

	check(bUsePartiallyResidentMips);
	FRHIResourceCreateInfo CreateInfo(ResourceMem);
	CreateInfo.ExtData = PlatformData->GetExtData();
	Texture2DRHI = RHICreateTexture2D( SizeX, SizeY, PixelFormat, State.MaxNumLODs, 1, CreationFlags | TexCreate_Virtual, CreateInfo);
	RHIVirtualTextureSetFirstMipInMemory(Texture2DRHI, CurrentFirstMip);
	RHIVirtualTextureSetFirstMipVisible(Texture2DRHI, CurrentFirstMip);

	check( ResourceMem == nullptr );

	// Read the resident mip-levels into the RHI texture.
	for( int32 MipIndex=CurrentFirstMip; MipIndex < State.MaxNumLODs; MipIndex++ )
	{
		if ( MipData[MipIndex] != NULL )
		{
			uint32 DestPitch;
			void* TheMipData = RHILockTexture2D( Texture2DRHI, MipIndex, RLM_WriteOnly, DestPitch, false );
			GetData( MipIndex, TheMipData, DestPitch );
			RHIUnlockTexture2D( Texture2DRHI, MipIndex, false );
		}
	}

	TextureRHI = Texture2DRHI;
}

#if STATS
void FTexture2DResource::CalcRequestedMipsSize()
{
	if (PlatformData && State.NumRequestedLODs > 0)
	{
		static TConsoleVariableData<int32>* CVarReducedMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextureReducedMemory"));
		check(CVarReducedMode);

		uint32 TextureAlign = 0;
		// Must be consistent with the logic in FTexture2DResource::InitRHI
		if (bUsePartiallyResidentMips && (!CVarReducedMode->GetValueOnRenderThread() || State.NumRequestedLODs > State.NumNonStreamingLODs))
		{
			TextureSize = RHICalcVMTexture2DPlatformSize(SizeX, SizeY, PixelFormat, State.NumRequestedLODs, State.RequestedFirstLODIdx(), 1, CreationFlags | TexCreate_Virtual, TextureAlign);
		}
		else
		{
			const FIntPoint MipExtents = CalcMipMapExtent(SizeX, SizeY, PixelFormat, State.RequestedFirstLODIdx());
			TextureSize = RHICalcTexture2DPlatformSize(MipExtents.X, MipExtents.Y, PixelFormat, State.NumRequestedLODs, 1, CreationFlags, FRHIResourceCreateInfo(PlatformData->GetExtData()), TextureAlign);
		}
	}
	else
	{
		TextureSize = 0;
	}
}
#endif

/**
 * Writes the data for a single mip-level into a destination buffer.
 *
 * @param MipIndex		Index of the mip-level to read.
 * @param Dest			Address of the destination buffer to receive the mip-level's data.
 * @param DestPitch		Number of bytes per row
 */
void FTexture2DResource::GetData( uint32 MipIndex, void* Dest, uint32 DestPitch )
{
	const FTexture2DMipMap& MipMap = *GetPlatformMip(MipIndex);
	check( MipData[MipIndex] );

	// for platforms that returned 0 pitch from Lock, we need to just use the bulk data directly, never do 
	// runtime block size checking, conversion, or the like
	if (DestPitch == 0)
	{
		FMemory::Memcpy(Dest, MipData[MipIndex], MipMap.BulkData.GetBulkDataSize());
	}
	else
	{
		const uint32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;		// Block width in pixels
		const uint32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;		// Block height in pixels
		const uint32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;
		uint32 NumColumns		= (MipMap.SizeX + BlockSizeX - 1) / BlockSizeX;	// Num-of columns in the source data (in blocks)
		uint32 NumRows			= (MipMap.SizeY + BlockSizeY - 1) / BlockSizeY;	// Num-of rows in the source data (in blocks)
		if ( PixelFormat == PF_PVRTC2 || PixelFormat == PF_PVRTC4 )
		{
			// PVRTC has minimum 2 blocks width and height
			NumColumns = FMath::Max<uint32>(NumColumns, 2);
			NumRows = FMath::Max<uint32>(NumRows, 2);
		}
		const uint32 SrcPitch   = NumColumns * BlockBytes;						// Num-of bytes per row in the source data
		const uint32 EffectiveSize = BlockBytes*NumColumns*NumRows;

	#if !WITH_EDITORONLY_DATA
		// on console we don't want onload conversions
		checkf(EffectiveSize == (uint32)MipMap.BulkData.GetBulkDataSize(), 
			TEXT("Texture '%s', mip %d, has a BulkDataSize [%d] that doesn't match calculated size [%d]. Texture size %dx%d, format %d"),
			*TextureName.ToString(), MipIndex, MipMap.BulkData.GetBulkDataSize(), EffectiveSize, GetSizeX(), GetSizeY(), (int32)PixelFormat);
	#endif

		// Copy the texture data.
		CopyTextureData2D(MipData[MipIndex],Dest,MipMap.SizeY,PixelFormat,SrcPitch,DestPitch);
	}
	
	// Free data retrieved via GetCopy inside constructor.
	FMemory::Free(MipData[MipIndex]);
	MipData[MipIndex] = NULL;
}

// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Texture2D.cpp: Implementation of UTexture2D.
=============================================================================*/

#include "Engine/Texture2D.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/App.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/Paths.h"
#include "Containers/ResourceArray.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/LinkerLoad.h"
#include "UObject/CoreRedirects.h"
#include "RenderUtils.h"
#include "ContentStreaming.h"
#include "EngineUtils.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "DerivedDataCacheInterface.h"
#include "Engine/TextureStreamingTypes.h"
#include "Streaming/TextureStreamingHelpers.h"
#include "Streaming/Texture2DStreamOut_AsyncReallocate.h"
#include "Streaming/Texture2DStreamOut_Virtual.h"
#include "Streaming/Texture2DStreamIn_DDC_AsyncCreate.h"
#include "Streaming/Texture2DStreamIn_DDC_AsyncReallocate.h"
#include "Streaming/Texture2DStreamIn_IO_AsyncCreate.h"
#include "Streaming/Texture2DStreamIn_IO_AsyncReallocate.h"
#include "Streaming/Texture2DStreamIn_IO_Virtual.h"
// Generic path
#include "Streaming/TextureStreamIn.h"
#include "Streaming/Texture2DMipAllocator_AsyncCreate.h"
#include "Streaming/Texture2DMipAllocator_AsyncReallocate.h"
#include "Streaming/Texture2DMipDataProvider_DDC.h"
#include "Streaming/Texture2DMipDataProvider_IO.h"
#include "Engine/TextureMipDataProviderFactory.h"

#include "Async/AsyncFileHandle.h"
#include "EngineModule.h"
#include "Engine/Texture2DArray.h"
#include "VT/UploadingVirtualTexture.h"
#include "VT/VirtualTexturePoolConfig.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

UTexture2D::UTexture2D(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PendingUpdate = nullptr;
	StreamingIndex = INDEX_NONE;
	LevelIndex = INDEX_NONE;
	SRGB = true;
}

/*-----------------------------------------------------------------------------
	Global helper functions
-----------------------------------------------------------------------------*/

/** CVars */
static TAutoConsoleVariable<float> CVarSetMipMapLODBias(
	TEXT("r.MipMapLODBias"),
	0.0f,
	TEXT("Apply additional mip map bias for all 2D textures, range of -15.0 to 15.0"),
	ECVF_RenderThreadSafe | ECVF_Scalability);

TAutoConsoleVariable<int32> CVarFlushRHIThreadOnSTreamingTextureLocks(
	TEXT("r.FlushRHIThreadOnSTreamingTextureLocks"),
	0,
	TEXT("If set to 0, we won't do any flushes for streaming textures. This is safe because the texture streamer deals with these hazards explicitly."),
	ECVF_RenderThreadSafe);
static TAutoConsoleVariable<int32> CVarMobileReduceLoadedMips(
	TEXT("r.MobileReduceLoadedMips"),
	0,
	TEXT("Reduce loaded texture mipmaps for nonstreaming mobile platforms.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMobileMaxLoadedMips(
	TEXT("r.MobileMaxLoadedMips"),
	MAX_TEXTURE_MIP_COUNT,
	TEXT("Maximum number of loaded mips for nonstreaming mobile platforms.\n"),
	ECVF_RenderThreadSafe);


int32 GUseGenericStreamingPath = 0;
static FAutoConsoleVariableRef CVarUseGenericStreamingPath(
	TEXT("r.Streaming.UseGenericStreamingPath"),
	GUseGenericStreamingPath,
	TEXT("Control when to use the mip data provider implementation: (default=0)\n")
	TEXT("0 to use it when there is a custom asset override.\n")
	TEXT("1 to always use it.\n")
	TEXT("2 to never use it."),
	ECVF_Default
);


static int32 MobileReduceLoadedMips(int32 NumTotalMips)
{
	int32 NumReduceMips = FMath::Max(0, CVarMobileReduceLoadedMips.GetValueOnAnyThread());
	int32 MaxLoadedMips = FMath::Clamp(CVarMobileMaxLoadedMips.GetValueOnAnyThread(), 1, GMaxTextureMipCount);

	int32 NumMips = NumTotalMips;
	// Reduce number of mips as requested
	NumMips = FMath::Max(NumMips - NumReduceMips, 1);
	// Clamp number of mips as requested
	NumMips = FMath::Min(NumMips, MaxLoadedMips);
	
	return NumMips;
}

/** Number of times to retry to reallocate a texture before trying a panic defragmentation, the first time. */
int32 GDefragmentationRetryCounter = 10;
/** Number of times to retry to reallocate a texture before trying a panic defragmentation, subsequent times. */
int32 GDefragmentationRetryCounterLong = 100;

/** Turn on ENABLE_RENDER_ASSET_TRACKING in ContentStreaming.cpp and setup GTrackedTextures to track specific textures/meshes through the streaming system. */
extern bool TrackTextureEvent( FStreamingRenderAsset* StreamingTexture, UStreamableRenderAsset* Texture, bool bForceMipLevelsToBeResident, const FRenderAssetStreamingManager* Manager );

/*-----------------------------------------------------------------------------
	FTexture2DMipMap
-----------------------------------------------------------------------------*/

void FTexture2DMipMap::Serialize(FArchive& Ar, UObject* Owner, int32 MipIdx)
{
	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

#if WITH_EDITORONLY_DATA
	BulkData.Serialize(Ar, Owner, MipIdx, false, FileRegionType);
#else
	BulkData.Serialize(Ar, Owner, MipIdx, false);
#endif

	Ar << SizeX;
	Ar << SizeY;
	Ar << SizeZ;

#if WITH_EDITORONLY_DATA
	if (!bCooked)
	{
		Ar << FileRegionType;
		Ar << DerivedDataKey;
	}
#endif // #if WITH_EDITORONLY_DATA
}

#if WITH_EDITORONLY_DATA
uint32 FTexture2DMipMap::StoreInDerivedDataCache(const FString& InDerivedDataKey, const FStringView& TextureName)
{
	int32 BulkDataSizeInBytes = BulkData.GetBulkDataSize();
	check(BulkDataSizeInBytes > 0);

	TArray<uint8> DerivedData;
	FMemoryWriter Ar(DerivedData, /*bIsPersistent=*/ true);
	Ar << BulkDataSizeInBytes;
	{
		void* BulkMipData = BulkData.Lock(LOCK_READ_ONLY);
		Ar.Serialize(BulkMipData, BulkDataSizeInBytes);
		BulkData.Unlock();
	}
	const uint32 Result = DerivedData.Num();
	GetDerivedDataCacheRef().Put(*InDerivedDataKey, DerivedData, TextureName, /*bPutEvenIfExists*/ true);
	DerivedDataKey = InDerivedDataKey;
	BulkData.RemoveBulkData();
	return Result;
}
#endif // #if WITH_EDITORONLY_DATA

/*-----------------------------------------------------------------------------
	UTexture2D
-----------------------------------------------------------------------------*/

bool UTexture2D::GetResourceMemSettings(int32 FirstMipIdx, int32& OutSizeX, int32& OutSizeY, int32& OutNumMips, uint32& OutTexCreateFlags)
{
	return false;
}

void UTexture2D::Serialize(FArchive& Ar)
{
	LLM_SCOPE(ELLMTag::TextureMetaData);

	Super::Serialize(Ar);

	FStripDataFlags StripDataFlags(Ar);

	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (Ar.IsCooking() || bCooked)
	{
		SerializeCookedPlatformData(Ar);
	}

#if WITH_EDITOR	
	if (Ar.IsLoading() && !Ar.IsTransacting() && !bCooked && !GetOutermost()->HasAnyPackageFlags(PKG_ReloadingForCooker))
	{
		// The composite texture may not have been loaded yet. We have to defer caching platform
		// data until post load.
		if (CompositeTexture == NULL || CompositeTextureMode == CTM_Disabled)
		{
			BeginCachePlatformData();
		}
	}
#endif // #if WITH_EDITOR
}

int32 UTexture2D::GetNumResidentMips() const
{
	if (Resource)
	{
		if (IsCurrentlyVirtualTextured())
		{
			/*
			For VT this is obviously a bit abstract. We could return:
			- 0 -> No mips are guaranteed to be resident
			- Mips that are currently fully resident -> Not sure what the use of that would be
			- Mips that are currently partially resident
			- Mips that may be made resident by the VT system

			=> We currently return the last value as it seems to best fit use of this function throughout editor and engine, namely to query the actual
			in-game	resolution of the texture as it's currently loaded. An other option would be "Mips that are partially resident" as that would cover
			somewhat the same but knowing this is additional burden on the VT system and interfaces.
			*/
			return static_cast<const FVirtualTexture2DResource*>(Resource)->GetNumMips();
		}
		else if (CachedSRRState.IsValid())
		{
			return CachedSRRState.NumResidentLODs;
		}
		else
		{
			return Resource->GetCurrentMipCount();
		}
	}
	return 0;
}

#if WITH_EDITOR
void UTexture2D::PostEditUndo()
{
	FPropertyChangedEvent Undo(NULL);
	PostEditChangeProperty(Undo);
}

void UTexture2D::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
#if WITH_EDITORONLY_DATA
	if (!Source.IsPowerOfTwo() && (PowerOfTwoMode == ETexturePowerOfTwoSetting::None))
	{
		// Force NPT textures to have no mipmaps.
		MipGenSettings = TMGS_NoMipmaps;
		NeverStream = true;
		if (VirtualTextureStreaming)
		{
			UE_LOG(LogTexture, Warning, TEXT("VirtualTextureStreaming not supported for \"%s\", texture size is not a power-of-2"), *GetName());
			VirtualTextureStreaming = false;
		}
	}

	// Make sure settings are correct for LUT textures.
	if(LODGroup == TEXTUREGROUP_ColorLookupTable)
	{
		MipGenSettings = TMGS_NoMipmaps;
		SRGB = false;
	}
#endif // #if WITH_EDITORONLY_DATA

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UTexture2D, AddressX)
		|| PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UTexture2D, AddressY))
	{
		// VT need to recompile shaders when address mode changes
		// Non-VT still needs to potentially update sampler state in the materials
		NotifyMaterials();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif // WITH_EDITOR

float UTexture2D::GetAverageBrightness(bool bIgnoreTrueBlack, bool bUseGrayscale)
{
	float AvgBrightness = -1.0f;
#if WITH_EDITOR
	TArray64<uint8> RawData;
	// use the source art if it exists
	if (Source.IsValid() && Source.GetFormat() == TSF_BGRA8)
	{
		Source.GetMipData(RawData, 0);
	}
	else
	{
		UE_LOG(LogTexture, Log, TEXT("No SourceArt available for %s"), *GetPathName());
	}

	if (RawData.Num() > 0)
	{
		int32 SizeX = Source.GetSizeX();
		int32 SizeY = Source.GetSizeY();
		double PixelSum = 0.0f;
		int32 Divisor = SizeX * SizeY;
		FColor* ColorData = (FColor*)RawData.GetData();
		FLinearColor CurrentColor;
		for (int32 Y = 0; Y < SizeY; Y++)
		{
			for (int32 X = 0; X < SizeX; X++)
			{
				if ((ColorData->R == 0) && (ColorData->G == 0) && (ColorData->B == 0) && bIgnoreTrueBlack)
				{
					ColorData++;
					Divisor--;
					continue;
				}

				if (SRGB == true)
				{
					CurrentColor = bUseLegacyGamma ? FLinearColor::FromPow22Color(*ColorData) : FLinearColor(*ColorData);
				}
				else
				{
					CurrentColor.R = float(ColorData->R) / 255.0f;
					CurrentColor.G = float(ColorData->G) / 255.0f;
					CurrentColor.B = float(ColorData->B) / 255.0f;
				}

				if (bUseGrayscale == true)
				{
					PixelSum += CurrentColor.R * 0.30f + CurrentColor.G * 0.59f + CurrentColor.B * 0.11f;
				}
				else
				{
					PixelSum += FMath::Max<float>(CurrentColor.R, FMath::Max<float>(CurrentColor.G, CurrentColor.B));
				}

				ColorData++;
			}
		}
		if (Divisor > 0)
		{
			AvgBrightness = PixelSum / Divisor;
		}
	}
#endif // #if WITH_EDITOR
	return AvgBrightness;
}

void UTexture2D::CancelPendingTextureStreaming()
{
	for( TObjectIterator<UTexture2D> It; It; ++It )
	{
		UTexture2D* CurrentTexture = *It;
		CurrentTexture->CancelPendingStreamingRequest();
	}

	// No need to call FlushResourceStreaming(), since calling CancelPendingMipChangeRequest has an immediate effect.
}

bool UTexture2D::IsReadyForAsyncPostLoad() const
{
	return !PlatformData || PlatformData->IsReadyForAsyncPostLoad();

}

void UTexture2D::PostLoad()
{
#if WITH_EDITOR
	ImportedSize = Source.GetLogicalSize();

	if (FApp::CanEverRender())
	{
		FinishCachePlatformData();
	}
#endif // #if WITH_EDITOR

	// Route postload, which will update bIsStreamable as UTexture::PostLoad calls UpdateResource.
	Super::PostLoad();
}

void UTexture2D::PreSave(const class ITargetPlatform* TargetPlatform)
{
	Super::PreSave(TargetPlatform);
#if WITH_EDITOR
	if( bTemporarilyDisableStreaming )
	{
		bTemporarilyDisableStreaming = false;
		UpdateResource();
	}
#endif
}
void UTexture2D::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	FIntPoint SourceSize(0, 0);
#if WITH_EDITOR
	SourceSize = Source.GetLogicalSize();
#endif

	const FString DimensionsStr = FString::Printf(TEXT("%dx%d"), SourceSize.X, SourceSize.Y);
	OutTags.Add( FAssetRegistryTag("Dimensions", DimensionsStr, FAssetRegistryTag::TT_Dimensional) );
	OutTags.Add( FAssetRegistryTag("HasAlphaChannel", HasAlphaChannel() ? TEXT("True") : TEXT("False"), FAssetRegistryTag::TT_Alphabetical) );
	OutTags.Add( FAssetRegistryTag("Format", GPixelFormats[GetPixelFormat()].Name, FAssetRegistryTag::TT_Alphabetical) );

	Super::GetAssetRegistryTags(OutTags);
}

void UTexture2D::UpdateResource()
{
	WaitForPendingInitOrStreaming();

#if WITH_EDITOR
	// Recache platform data if the source has changed.
	CachePlatformData();
	// clear all the cooked cached platform data if the source could have changed... 
	ClearAllCachedCookedPlatformData();
#else
	// Note that using TF_FirstMip disables texture streaming, because the mip data becomes lost.
	// Also, the cleanup of the platform data must go between UpdateCachedLODBias() and UpdateResource().
	const bool bLoadOnlyFirstMip = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetMipLoadOptions(this) == ETextureMipLoadOptions::OnlyFirstMip;
	if (bLoadOnlyFirstMip && PlatformData && PlatformData->Mips.Num() > 0 && FPlatformProperties::RequiresCookedData())
	{
		const int32 NumMipsInTail = PlatformData->GetNumMipsInTail();
		const int32 MipTailBaseIndex = FMath::Max(0, NumMipsInTail > 0 ? (PlatformData->Mips.Num() - NumMipsInTail) : (PlatformData->Mips.Num() - 1));

		const int32 FirstMip = FMath::Min(FMath::Max(0, GetCachedLODBias()), MipTailBaseIndex);
		if (FirstMip < MipTailBaseIndex)
		{
			// Remove any mips after the first mip.
			PlatformData->Mips.RemoveAt(FirstMip + 1, PlatformData->Mips.Num() - FirstMip - 1);
			PlatformData->OptData.NumMipsInTail = 0;
		}
		// Remove any mips before the first mip.
		PlatformData->Mips.RemoveAt(0, FirstMip);
		// Update the texture size for the memory usage metrics.
		PlatformData->SizeX = PlatformData->Mips[0].SizeX;
		PlatformData->SizeY = PlatformData->Mips[0].SizeY;
	}
#endif // #if WITH_EDITOR

	// Route to super.
	Super::UpdateResource();
}


#if WITH_EDITOR
void UTexture2D::PostLinkerChange()
{
	// Changing the linker requires re-creating the resource to make sure streaming behavior is right.
	if( !HasAnyFlags( RF_BeginDestroyed | RF_NeedLoad | RF_NeedPostLoad ) && !IsUnreachable() )
	{
		// Update the resource.
		UpdateResource();
	}
}
#endif

void UTexture2D::BeginDestroy()
{
	// Route BeginDestroy.
	Super::BeginDestroy();

	TrackTextureEvent( NULL, this, false, 0 );
}

FString UTexture2D::GetDesc()
{
	const int32 MaxResMipBias = GetNumMips() - GetNumMipsAllowed(false);
	return FString::Printf( TEXT("%s %dx%d [%s]"), 
		VirtualTextureStreaming ? TEXT("Virtual") : (NeverStream ? TEXT("NeverStreamed") : TEXT("Streamed")), 
		FMath::Max<int32>(GetSizeX() >> MaxResMipBias, 1), 
		FMath::Max<int32>(GetSizeY() >> MaxResMipBias, 1), 
		GPixelFormats[GetPixelFormat()].Name
		);
}

int32 UTexture2D::CalcTextureMemorySize( int32 MipCount ) const
{
	int32 Size = 0;
	if (PlatformData)
	{
		static TConsoleVariableData<int32>* CVarReducedMode = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextureReducedMemory"));
		check(CVarReducedMode);

		ETextureCreateFlags TexCreateFlags = (SRGB ? TexCreate_SRGB : TexCreate_None) | (bNoTiling ? TexCreate_NoTiling : TexCreate_None) | (bNotOfflineProcessed ? TexCreate_None : TexCreate_OfflineProcessed) | TexCreate_Streamable;
		const bool bCanUsePartiallyResidentMips = CanCreateWithPartiallyResidentMips(TexCreateFlags);

		const int32 SizeX = GetSizeX();
		const int32 SizeY = GetSizeY();
		const int32 NumMips = GetNumMips();
		const int32 FirstMip = FMath::Max(0, NumMips - MipCount);
		const EPixelFormat Format = GetPixelFormat();
		uint32 TextureAlign;

		// Must be consistent with the logic in FTexture2DResource::InitRHI
		if (IsStreamable() && bCanUsePartiallyResidentMips && (!CVarReducedMode->GetValueOnAnyThread() || MipCount > UTexture2D::GetMinTextureResidentMipCount()))
		{
			TexCreateFlags |= TexCreate_Virtual;
			Size = (int32)RHICalcVMTexture2DPlatformSize(SizeX, SizeY, Format, NumMips, FirstMip, 1, TexCreateFlags, TextureAlign);
		}
		else
		{
			const FIntPoint MipExtents = CalcMipMapExtent(SizeX, SizeY, Format, FirstMip);
			Size = (int32)RHICalcTexture2DPlatformSize(MipExtents.X, MipExtents.Y, Format, FMath::Max(1, MipCount), 1, TexCreateFlags, FRHIResourceCreateInfo(PlatformData->GetExtData()), TextureAlign);
		}
	}
	return Size;
}

int32 UTexture2D::GetNumMipsAllowed(bool bIgnoreMinResidency) const
{
	const int32 NumMips = GetNumMips();

	// Compute the number of mips that will be available after cooking, as some mips get cooked out.
	// See the logic around FirstMipToSerialize in TextureDerivedData.cpp, SerializePlatformData().
	const int32 LODBiasNoCinematics = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->CalculateLODBias(this, false);
	const int32 CookedMips = FMath::Clamp<int32>(NumMips - LODBiasNoCinematics, 1, GMaxTextureMipCount);
	const int32 MinResidentMipCount = GetMinTextureResidentMipCount();

	// If the data is already cooked, then mips bellow min resident can't be stripped out.
	// This would happen if the data is cooked with some texture group settings, but launched
	// with other settings, adding more constraints on the cooked data.
	if (bIgnoreMinResidency && !FPlatformProperties::RequiresCookedData())
	{
		return CookedMips;
	}
	else if (NumMips > MinResidentMipCount)
	{
		// In non cooked, the engine can not partially load the resident mips.
		return FMath::Max<int32>(CookedMips, MinResidentMipCount);
	}
	else
	{
		return NumMips;
	}
}


uint32 UTexture2D::CalcTextureMemorySizeEnum( ETextureMipCount Enum ) const
{
	if (IsCurrentlyVirtualTextured())
	{
		// Virtual textures "take no space". I.e. the space used by them (Caches translation tables, ...) should already be accounted for elsewhere.
		return 0;
	}

	if ( Enum == TMC_ResidentMips )
	{
		return CalcTextureMemorySize( GetNumResidentMips() );
	}
	else if( Enum == TMC_AllMipsBiased)
	{
		return CalcTextureMemorySize( GetNumMipsAllowed(false) );
	}
	else
	{
		return CalcTextureMemorySize( GetNumMips() );
	}
}


bool UTexture2D::GetSourceArtCRC(uint32& OutSourceCRC)
{
	bool bResult = false;
	TArray64<uint8> RawData;
#if WITH_EDITOR
	// use the source art if it exists
	if (Source.IsValid())
	{
		// Decompress source art.
		Source.GetMipData(RawData, 0);
	}
	else
	{
		UE_LOG(LogTexture, Log, TEXT("No SourceArt available for %s"), *GetPathName());
	}

	if (RawData.Num() > 0)
	{
		OutSourceCRC = FCrc::MemCrc_DEPRECATED((void*)(RawData.GetData()), RawData.Num());
		bResult = true;
	}
#endif // #if WITH_EDITOR
	return bResult;
}

bool UTexture2D::HasSameSourceArt(UTexture2D* InTexture)
{
	bool bResult = false;
#if WITH_EDITOR
	TArray64<uint8> RawData1;
	TArray64<uint8> RawData2;
	int32 SizeX = 0;
	int32 SizeY = 0;

	// Need to handle UDIM here?
	if ((Source.GetSizeX() == InTexture->Source.GetSizeX()) && 
		(Source.GetSizeY() == InTexture->Source.GetSizeY()) &&
		(Source.GetNumMips() == InTexture->Source.GetNumMips()) &&
		(Source.GetNumMips() == 1) &&
		(Source.GetFormat() == InTexture->Source.GetFormat()) &&
		(SRGB == InTexture->SRGB))
	{
		Source.GetMipData(RawData1, 0);
		InTexture->Source.GetMipData(RawData2, 0);
	}

	if ((RawData1.Num() > 0) && (RawData1.Num() == RawData2.Num()))
	{
		if (RawData1 == RawData2)
		{
			bResult = true;
		}
	}
#endif // #if WITH_EDITOR
	return bResult;
}

bool UTexture2D::HasAlphaChannel() const
{
	if (PlatformData && (PlatformData->PixelFormat != PF_DXT1))
	{
		return true;
	}
	return false;
}

FTextureResource* UTexture2D::CreateResource()
{
	if (IsCurrentlyVirtualTextured())
	{
		FVirtualTexture2DResource* ResourceVT = new FVirtualTexture2DResource(this, PlatformData->VTData, GetCachedLODBias());
		return ResourceVT;
 	}
	else if (PlatformData)
	{
		const EPixelFormat PixelFormat = GetPixelFormat();

		int32 NumMips = FMath::Min3<int32>(PlatformData->Mips.Num(), GMaxTextureMipCount, FStreamableRenderResourceState::MAX_LOD_COUNT);
#if !PLATFORM_SUPPORTS_TEXTURE_STREAMING // eg, Android
		NumMips = MobileReduceLoadedMips(NumMips);
#endif
	
		if (!NumMips)
		{
			UE_LOG(LogTexture, Error, TEXT("%s contains no miplevels! Please delete. (Format: %d)"), *GetFullName(), (int)PixelFormat);
		}
		else if (!GPixelFormats[PixelFormat].Supported)
		{
			UE_LOG(LogTexture, Error, TEXT("%s is %s [raw type %d] which is not supported."), *GetFullName(), GPixelFormats[PixelFormat].Name, static_cast<int32>(PixelFormat));
		}
		else if (NumMips == 1 && FMath::Max(GetSizeX(), GetSizeY()) > (int32)GetMax2DTextureDimension())
		{
			UE_LOG(LogTexture, Warning, TEXT("%s cannot be created, exceeds this rhi's maximum dimension (%d) and has no mip chain to fall back on."), *GetFullName(), GetMax2DTextureDimension());
		}
		else
		{
			// Should be as big as the mips we have already directly loaded into GPU mem
			const FStreamableRenderResourceState PostInitState = GetResourcePostInitState(PlatformData, !bTemporarilyDisableStreaming, ResourceMem ? ResourceMem->GetNumMips() : 0, NumMips);
			FTexture2DResource* Texture2DResource = new FTexture2DResource(this, PostInitState);
			// preallocated memory for the UTexture2D resource is now owned by this resource
			// and will be freed by the RHI resource or when the FTexture2DResource is deleted
			ResourceMem = nullptr;

			return Texture2DResource;
		}
	}

	return nullptr;
}

EMaterialValueType UTexture2D::GetMaterialType() const
{
	if (VirtualTextureStreaming)
	{
		return MCT_TextureVirtual;
	}
	return MCT_Texture2D;
}

void UTexture2D::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);

	if (IsCurrentlyVirtualTextured())
	{
		CumulativeResourceSize.AddUnknownMemoryBytes(PlatformData->VTData->GetMemoryFootprint());
	}
	else
	{
		if (CumulativeResourceSize.GetResourceSizeMode() == EResourceSizeMode::Exclusive)
		{
			// Use only loaded mips
			CumulativeResourceSize.AddDedicatedVideoMemoryBytes(CalcTextureMemorySize(GetNumResidentMips()));
		}
		else
		{
			// Use all possible mips
			CumulativeResourceSize.AddDedicatedVideoMemoryBytes(CalcTextureMemorySize(GetNumMipsAllowed(true)));
		}
	}
}

UTexture2D* UTexture2D::CreateTransient(int32 InSizeX, int32 InSizeY, EPixelFormat InFormat, const FName InName)
{
	LLM_SCOPE(ELLMTag::Textures);

	UTexture2D* NewTexture = NULL;
	if (InSizeX > 0 && InSizeY > 0 &&
		(InSizeX % GPixelFormats[InFormat].BlockSizeX) == 0 &&
		(InSizeY % GPixelFormats[InFormat].BlockSizeY) == 0)
	{
		NewTexture = NewObject<UTexture2D>(
			GetTransientPackage(),
			InName,
			RF_Transient
			);

		NewTexture->PlatformData = new FTexturePlatformData();
		NewTexture->PlatformData->SizeX = InSizeX;
		NewTexture->PlatformData->SizeY = InSizeY;
		NewTexture->PlatformData->PixelFormat = InFormat;

		// Allocate first mipmap.
		int32 NumBlocksX = InSizeX / GPixelFormats[InFormat].BlockSizeX;
		int32 NumBlocksY = InSizeY / GPixelFormats[InFormat].BlockSizeY;
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		NewTexture->PlatformData->Mips.Add(Mip);
		Mip->SizeX = InSizeX;
		Mip->SizeY = InSizeY;
		Mip->BulkData.Lock(LOCK_READ_WRITE);
		Mip->BulkData.Realloc(NumBlocksX * NumBlocksY * GPixelFormats[InFormat].BlockBytes);
		Mip->BulkData.Unlock();
	}
	else
	{
		UE_LOG(LogTexture, Warning, TEXT("Invalid parameters specified for UTexture2D::CreateTransient()"));
	}
	return NewTexture;
}

int32 UTexture2D::Blueprint_GetSizeX() const
{
#if WITH_EDITORONLY_DATA
	// When cooking, blueprint construction scripts are ran before textures get postloaded.
	// In that state, the texture size is 0. Here we compute the resolution once cooked.
	if (!GetSizeX())
	{
		const UTextureLODSettings* LODSettings = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings();
		const int32 CookedLODBias = LODSettings->CalculateLODBias(Source.SizeX, Source.SizeY, MaxTextureSize, LODGroup, LODBias, 0, MipGenSettings, IsCurrentlyVirtualTextured());
		return FMath::Max<int32>(Source.SizeX >> CookedLODBias, 1);
	}
#endif
	return GetSizeX();
}

int32 UTexture2D::Blueprint_GetSizeY() const
{
#if WITH_EDITORONLY_DATA
	// When cooking, blueprint construction scripts are ran before textures get postloaded.
	// In that state, the texture size is 0. Here we compute the resolution once cooked.
	if (!GetSizeY())
	{
		const UTextureLODSettings* LODSettings = UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings();
		const int32 CookedLODBias = LODSettings->CalculateLODBias(Source.SizeX, Source.SizeY, MaxTextureSize, LODGroup, LODBias, 0, MipGenSettings, IsCurrentlyVirtualTextured());
		return FMath::Max<int32>(Source.SizeY >> CookedLODBias, 1);
	}
#endif
	return GetSizeY();
}

void UTexture2D::UpdateTextureRegions(int32 MipIndex, uint32 NumRegions, const FUpdateTextureRegion2D* Regions, uint32 SrcPitch, uint32 SrcBpp, uint8* SrcData, TFunction<void(uint8* SrcData, const FUpdateTextureRegion2D* Regions)> DataCleanupFunc)
{
	if (IsCurrentlyVirtualTextured())
	{
		UE_LOG(LogTexture, Log, TEXT("UpdateTextureRegions called for %s which is virtual."), *GetPathName());
		return;
	}

	FTexture2DResource* Texture2DResource = Resource ? Resource->GetTexture2DResource() : nullptr;
	if (!bTemporarilyDisableStreaming && IsStreamable())
	{
		UE_LOG(LogTexture, Log, TEXT("UpdateTextureRegions called for %s without calling TemporarilyDisableStreaming"), *GetPathName());
	}
	else if (Texture2DResource)
	{
		struct FUpdateTextureRegionsData
		{
			FTexture2DResource* Texture2DResource;
			int32 MipIndex;
			uint32 NumRegions;
			const FUpdateTextureRegion2D* Regions;
			uint32 SrcPitch;
			uint32 SrcBpp;
			uint8* SrcData;
		};

		FUpdateTextureRegionsData* RegionData = new FUpdateTextureRegionsData;

		RegionData->Texture2DResource = Texture2DResource;
		RegionData->MipIndex = MipIndex;
		RegionData->NumRegions = NumRegions;
		RegionData->Regions = Regions;
		RegionData->SrcPitch = SrcPitch;
		RegionData->SrcBpp = SrcBpp;
		RegionData->SrcData = SrcData;

		ENQUEUE_RENDER_COMMAND(UpdateTextureRegionsData)(
			[RegionData, DataCleanupFunc](FRHICommandListImmediate& RHICmdList)
			{
				for (uint32 RegionIndex = 0; RegionIndex < RegionData->NumRegions; ++RegionIndex)
				{
					int32 CurrentFirstMip = RegionData->Texture2DResource->State.AssetLODBias;
					if (RegionData->MipIndex >= CurrentFirstMip)
					{
						// Some RHIs don't support source offsets. Offset source data pointer now and clear source offsets
						FUpdateTextureRegion2D RegionCopy = RegionData->Regions[RegionIndex];
						const uint8* RegionSourceData = RegionData->SrcData
							+ RegionCopy.SrcY * RegionData->SrcPitch
							+ RegionCopy.SrcX * RegionData->SrcBpp;
						RegionCopy.SrcX = 0;
						RegionCopy.SrcY = 0;

						RHIUpdateTexture2D(
							RegionData->Texture2DResource->TextureRHI->GetTexture2D(),
							RegionData->MipIndex - CurrentFirstMip,
							RegionCopy,
							RegionData->SrcPitch,
							RegionSourceData);
					}
				}

				// The deletion of source data may need to be deferred to the RHI thread after the updates occur
				RHICmdList.EnqueueLambda([RegionData, DataCleanupFunc](FRHICommandList&)
				{
					DataCleanupFunc(RegionData->SrcData, RegionData->Regions);
					delete RegionData;
				});
			});
	}
}

#if WITH_EDITOR
void UTexture2D::TemporarilyDisableStreaming()
{
	if( !bTemporarilyDisableStreaming )
	{
		bTemporarilyDisableStreaming = true;
		UpdateResource();
	}
}
#endif



float UTexture2D::GetGlobalMipMapLODBias()
{
	float BiasOffset = CVarSetMipMapLODBias.GetValueOnAnyThread(); // called from multiple threads.
	return FMath::Clamp(BiasOffset, -15.0f, 15.0f);
}

void UTexture2D::RefreshSamplerStates()
{
	if (Resource)
	{
		if (FTexture2DResource* Texture2DResource = Resource->GetTexture2DResource())
		{
			Texture2DResource->CacheSamplerStateInitializer(this);
			ENQUEUE_RENDER_COMMAND(RefreshSamplerStatesCommand)([Texture2DResource](FRHICommandList& RHICmdList)
			{
				Texture2DResource->RefreshSamplerStates();
			});
		}
	}
}

FVirtualTexture2DResource::FVirtualTexture2DResource(const UTexture2D* InOwner, FVirtualTextureBuiltData* InVTData, int32 InFirstMipToUse)
	: AllocatedVT(nullptr)
	, VTData(InVTData)
	, TextureOwner(InOwner)
{
	check(InOwner);
	check(InVTData);

	// Don't allow input mip bias to drop size below a single tile
	const uint32 SizeInTiles = FMath::Max(VTData->GetWidthInTiles(), VTData->GetHeightInTiles());
	const uint32 MaxMip = FMath::CeilLogTwo(SizeInTiles);
	FirstMipToUse = FMath::Min((int32)MaxMip, InFirstMipToUse);

	bSRGB = InOwner->SRGB;

	// Initialize this resource FeatureLevel, so it gets re-created on FeatureLevel changes
	SetFeatureLevel(GMaxRHIFeatureLevel);
}

FVirtualTexture2DResource::~FVirtualTexture2DResource()
{
}

void FVirtualTexture2DResource::InitRHI()
{
	check(TextureOwner);

	// We always create a sampler state if we're attached to a texture. This is used to sample the cache texture during actual rendering and the miptails editor resource.
	// If we're not attached to a texture it likely means we're light maps which have sampling handled differently.
	FSamplerStateInitializerRHI SamplerStateInitializer
	(
		// This will ensure nearest/linear/trilinear which does matter when sampling both the cache and the miptail
		(ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(TextureOwner),

		// This doesn't really matter when sampling the cache texture but it does when sampling the miptail texture
		TextureOwner->AddressX == TA_Wrap ? AM_Wrap : (TextureOwner->AddressX == TA_Clamp ? AM_Clamp : AM_Mirror),
		TextureOwner->AddressY == TA_Wrap ? AM_Wrap : (TextureOwner->AddressY == TA_Clamp ? AM_Clamp : AM_Mirror),
		AM_Wrap,

		// This doesn't really matter when sampling the cache texture (as it only has a level 0, so whatever the bias that is sampled) but it does when we sample miptail texture
		0 // VT currently ignores global mip bias ensure the miptail works the same -> UTexture2D::GetGlobalMipMapLODBias()
	);
	SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);

	const int32 MaxLevel = VTData->GetNumMips() - FirstMipToUse - 1;
	check(MaxLevel >= 0);

	const bool bContinuousUpdate = TextureOwner->IsVirtualTexturedWithContinuousUpdate();
	const bool bSinglePhysicalSpace = TextureOwner->IsVirtualTexturedWithSinglePhysicalSpace();

	FVTProducerDescription ProducerDesc;
	ProducerDesc.Name = TextureOwner->GetFName();
	ProducerDesc.bContinuousUpdate = bContinuousUpdate;
	ProducerDesc.Dimensions = 2;
	ProducerDesc.TileSize = VTData->TileSize;
	ProducerDesc.TileBorderSize = VTData->TileBorderSize;
	ProducerDesc.BlockWidthInTiles = FMath::DivideAndRoundUp<uint32>(GetNumTilesX(), VTData->WidthInBlocks);
	ProducerDesc.BlockHeightInTiles = FMath::DivideAndRoundUp<uint32>(GetNumTilesY(), VTData->HeightInBlocks);
	ProducerDesc.WidthInBlocks = VTData->WidthInBlocks;
	ProducerDesc.HeightInBlocks = VTData->HeightInBlocks;
	ProducerDesc.DepthInTiles = 1u;
	ProducerDesc.MaxLevel = MaxLevel;
	ProducerDesc.NumTextureLayers = VTData->GetNumLayers();
	ProducerDesc.NumPhysicalGroups = bSinglePhysicalSpace ? 1 : VTData->GetNumLayers();
	for (uint32 LayerIndex = 0u; LayerIndex < VTData->GetNumLayers(); ++LayerIndex)
	{
		ProducerDesc.LayerFormat[LayerIndex] = VTData->LayerTypes[LayerIndex];
		ProducerDesc.PhysicalGroupIndex[LayerIndex] = bSinglePhysicalSpace ? 0 : LayerIndex;
	}

	FUploadingVirtualTexture* VirtualTexture = new FUploadingVirtualTexture(VTData, FirstMipToUse);
	ProducerHandle = GetRendererModule().RegisterVirtualTextureProducer(ProducerDesc, VirtualTexture);

	// Only create the miptails mini-texture in-editor.
#if WITH_EDITOR
	InitializeEditorResources(VirtualTexture);
#endif
}

#if WITH_EDITOR
void FVirtualTexture2DResource::InitializeEditorResources(IVirtualTexture* InVirtualTexture)
{
	// Create a texture resource from the lowest resolution VT page data
	// this will then be used during asset tumbnails/hitproxies/...
	if (GIsEditor)
	{
		struct FPageToProduce
		{
			uint64 Handle;
			uint32 TileX;
			uint32 TileY;
		};

		// Choose a mip level for the thumbnail texture to ensure proper size
		const uint32 MaxMipLevel = VTData->GetNumMips() - 1u;
		const uint32 MaxTextureSize = FMath::Min<uint32>(GetMax2DTextureDimension(), 1024u);
		uint32 MipLevel = 0;
		uint32 MipWidth = GetSizeX();
		uint32 MipHeight = GetSizeY();
		while (((MipWidth > 128u && MipHeight > 128u) || MipWidth > MaxTextureSize || MipHeight > MaxTextureSize) && MipLevel < MaxMipLevel)
		{
			++MipLevel;
			MipWidth = FMath::DivideAndRoundUp(MipWidth, 2u);
			MipHeight = FMath::DivideAndRoundUp(MipHeight, 2u);
		}

		const EPixelFormat PixelFormat = VTData->LayerTypes[0];
		const uint32 MipScaleFactor = (1u << MipLevel);
		const uint32 MipWidthInTiles = FMath::DivideAndRoundUp(GetNumTilesX(), MipScaleFactor);
		const uint32 MipHeightInTiles = FMath::DivideAndRoundUp(GetNumTilesY(), MipScaleFactor);
		const uint32 TileSizeInPixels = GetTileSize();
		const uint32 LayerMask = 1u; // FVirtualTexture2DResource should only have a single layer

		TArray<FPageToProduce> PagesToProduce;
		PagesToProduce.Reserve(MipWidthInTiles * MipHeightInTiles);
		for (uint32 TileY = 0u; TileY < MipHeightInTiles; ++TileY)
		{
			for (uint32 TileX = 0u; TileX < MipWidthInTiles; ++TileX)
			{
				const uint32 vAddress = FMath::MortonCode2(TileX) | (FMath::MortonCode2(TileY) << 1);
				const FVTRequestPageResult RequestResult = InVirtualTexture->RequestPageData(ProducerHandle, LayerMask, MipLevel, vAddress, EVTRequestPagePriority::High);
				// High priority request should always generate data
				if (ensure(VTRequestPageStatus_HasData(RequestResult.Status)))
				{
					PagesToProduce.Add({ RequestResult.Handle, TileX, TileY });
				}
			}
		}

		ETextureCreateFlags TexCreateFlags = (TextureOwner->SRGB ? TexCreate_SRGB : TexCreate_None) | (TextureOwner->bNotOfflineProcessed ? TexCreate_None : TexCreate_OfflineProcessed);
		if (TextureOwner->bNoTiling)
		{
			TexCreateFlags |= TexCreate_NoTiling;
		}

		FRHIResourceCreateInfo CreateInfo;
		FTexture2DRHIRef Texture2DRHI = RHICreateTexture2D(MipWidthInTiles * TileSizeInPixels, MipHeightInTiles * TileSizeInPixels, PixelFormat, 1, 1, TexCreateFlags, CreateInfo);
		FRHICommandListImmediate& RHICommandList = FRHICommandListExecutor::GetImmediateCommandList();

		TArray<IVirtualTextureFinalizer*> Finalizers;
		for (const FPageToProduce& Page : PagesToProduce)
		{
			const uint32 vAddress = FMath::MortonCode2(Page.TileX) | (FMath::MortonCode2(Page.TileY) << 1);

			FVTProduceTargetLayer TargetLayer;
			TargetLayer.TextureRHI = Texture2DRHI;
			TargetLayer.pPageLocation = FIntVector(Page.TileX, Page.TileY, 0);

			IVirtualTextureFinalizer* Finalizer = InVirtualTexture->ProducePageData(RHICommandList,
				GMaxRHIFeatureLevel,
				EVTProducePageFlags::SkipPageBorders, // don't want to produce page borders, since we're laying out tiles in a regular texture
				ProducerHandle, LayerMask, MipLevel, vAddress,
				Page.Handle,
				&TargetLayer);
			if (Finalizer)
			{
				Finalizers.AddUnique(Finalizer);
			}
		}

		for (IVirtualTextureFinalizer* Finalizer : Finalizers)
		{
			Finalizer->Finalize(RHICommandList);
		}

		
		if (MipWidthInTiles * TileSizeInPixels != MipWidth || MipHeightInTiles * TileSizeInPixels != MipHeight)
		{
			// Logical dimensions of mip image may be smaller than tile size (in this case tile will contain mirrored/wrapped padding)
			// In this case, copy the proper sub-image from the tiled texture we produced into a new texture of the correct size
			check(MipWidth <= MipWidthInTiles * TileSizeInPixels);
			check(MipHeight <= MipHeightInTiles * TileSizeInPixels);

			FTexture2DRHIRef ResizedTexture2DRHI = RHICreateTexture2D(MipWidth, MipHeight, PixelFormat, 1, 1, TexCreateFlags, ERHIAccess::CopyDest, CreateInfo);
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.Size = FIntVector(MipWidth, MipHeight, 1);

			// Put the source texture in CopySrc mode. The destination texture is already in CopyDest mode because we created it that way.
			RHICommandList.Transition(FRHITransitionInfo(Texture2DRHI, ERHIAccess::SRVMask, ERHIAccess::CopySrc));
			RHICommandList.CopyTexture(Texture2DRHI, ResizedTexture2DRHI, CopyInfo);
			// Make the destination texture SRVMask again. We don't care about the source texture after this, so we won't bother transitioning it.
			RHICommandList.Transition(FRHITransitionInfo(ResizedTexture2DRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask));

			Texture2DRHI = MoveTemp(ResizedTexture2DRHI);
		}

		TextureRHI = Texture2DRHI;
		TextureRHI->SetName(TextureOwner->GetFName());
		RHIBindDebugLabelName(TextureRHI, *TextureOwner->GetName());
		RHIUpdateTextureReference(TextureOwner->TextureReference.TextureReferenceRHI, TextureRHI);

		bIgnoreGammaConversions = !TextureOwner->SRGB && TextureOwner->CompressionSettings != TC_HDR && TextureOwner->CompressionSettings != TC_HalfFloat;

		// re factored to ensure this is set earlier...make sure it's correct
		ensure(bSRGB == TextureOwner->SRGB);
		//bSRGB = TextureOwner->SRGB;
	}
}
#endif // WITH_EDITOR

void FVirtualTexture2DResource::ReleaseRHI()
{
	ReleaseAllocatedVT();

	GetRendererModule().ReleaseVirtualTextureProducer(ProducerHandle);
	ProducerHandle = FVirtualTextureProducerHandle();
}

class IAllocatedVirtualTexture* FVirtualTexture2DResource::AcquireAllocatedVT()
{
	check(IsInRenderingThread());
	if (!AllocatedVT)
	{
		FAllocatedVTDescription VTDesc;
		VTDesc.Dimensions = 2;
		VTDesc.TileSize = VTData->TileSize;
		VTDesc.TileBorderSize = VTData->TileBorderSize;
		VTDesc.NumTextureLayers = VTData->GetNumLayers();
		VTDesc.bShareDuplicateLayers = TextureOwner->IsVirtualTexturedWithSinglePhysicalSpace();

		for (uint32 LayerIndex = 0u; LayerIndex < VTDesc.NumTextureLayers; ++LayerIndex)
		{
			VTDesc.ProducerHandle[LayerIndex] = ProducerHandle; // use the same producer for each layer
			VTDesc.ProducerLayerIndex[LayerIndex] = LayerIndex;
		}
		AllocatedVT = GetRendererModule().AllocateVirtualTexture(VTDesc);
	}
	return AllocatedVT;
}

void FVirtualTexture2DResource::ReleaseAllocatedVT()
{
	if (AllocatedVT)
	{
		GetRendererModule().DestroyVirtualTexture(AllocatedVT);
		AllocatedVT = nullptr;
	}
}

uint32 FVirtualTexture2DResource::GetSizeX() const
{
	return FMath::Max(VTData->Width >> FirstMipToUse, 1u);
}

uint32 FVirtualTexture2DResource::GetSizeY() const
{
	return FMath::Max(VTData->Height >> FirstMipToUse, 1u);
}

EPixelFormat FVirtualTexture2DResource::GetFormat(uint32 LayerIndex) const
{
	return VTData->LayerTypes[LayerIndex];
}

FIntPoint FVirtualTexture2DResource::GetSizeInBlocks() const
{
	return FIntPoint(VTData->WidthInBlocks, VTData->HeightInBlocks);
}

uint32 FVirtualTexture2DResource::GetNumTilesX() const
{
	return FMath::Max(VTData->GetWidthInTiles() >> FirstMipToUse, 1u);
}

uint32 FVirtualTexture2DResource::GetNumTilesY() const
{
	return FMath::Max(VTData->GetHeightInTiles() >> FirstMipToUse, 1u);
}

uint32 FVirtualTexture2DResource::GetBorderSize() const
{
	return VTData->TileBorderSize;
}

uint32 FVirtualTexture2DResource::GetNumMips() const
{
	ensure((int32)VTData->GetNumMips() > FirstMipToUse);
	return VTData->GetNumMips() - FirstMipToUse;
}

uint32 FVirtualTexture2DResource::GetNumLayers() const
{
	return VTData->GetNumLayers();
}

uint32 FVirtualTexture2DResource::GetTileSize() const
{
	return VTData->TileSize;
}

uint32 FVirtualTexture2DResource::GetAllocatedvAddress() const
{
	if (AllocatedVT)
	{
		return AllocatedVT->GetVirtualAddress();
	}
	return ~0;
}

FIntPoint FVirtualTexture2DResource::GetPhysicalTextureSize(uint32 LayerIndex) const
{
	if (AllocatedVT)
	{
		const uint32 PhysicalTextureSize = AllocatedVT->GetPhysicalTextureSize(LayerIndex);
		return FIntPoint(PhysicalTextureSize, PhysicalTextureSize);
	}
	return FIntPoint(0, 0);
}

bool UTexture2D::StreamIn(int32 NewMipCount, bool bHighPrio)
{
	check(IsInGameThread());

	FTexture2DResource* Texture2DResource = Resource ? Resource->GetTexture2DResource() : nullptr;
	if (!HasPendingInitOrStreaming() && CachedSRRState.StreamIn(NewMipCount) && ensure(Texture2DResource))
	{
		FTextureMipDataProvider* CustomMipDataProvider = nullptr;
		if (GUseGenericStreamingPath != 2)
		{
			for (UAssetUserData* UserData : AssetUserData)
			{
				UTextureMipDataProviderFactory* CustomMipDataProviderFactory = Cast<UTextureMipDataProviderFactory>(UserData);
				if (CustomMipDataProviderFactory)
				{
					CustomMipDataProvider = CustomMipDataProviderFactory->AllocateMipDataProvider(this);
					if (CustomMipDataProvider)
					{
						break;
					}
				}
			}
		}

		if (!CustomMipDataProvider && GUseGenericStreamingPath != 1)
		{
	#if WITH_EDITORONLY_DATA
			if (FPlatformProperties::HasEditorOnlyData() && !GetOutermost()->bIsCookedForEditor)
			{
				if (GRHISupportsAsyncTextureCreation)
				{
					PendingUpdate = new FTexture2DStreamIn_DDC_AsyncCreate(this);
				}
				else
				{
					PendingUpdate = new FTexture2DStreamIn_DDC_AsyncReallocate(this);
				}
			}
			else
	#endif
			{
				// If the future texture is to be a virtual texture, use the virtual stream in path.
				if (Texture2DResource->bUsePartiallyResidentMips)
				{
					PendingUpdate = new FTexture2DStreamIn_IO_Virtual(this, bHighPrio);
				}
				// If the platform supports creating the new texture on an async thread, use that path.
				else if (GRHISupportsAsyncTextureCreation)
				{
					PendingUpdate = new FTexture2DStreamIn_IO_AsyncCreate(this, bHighPrio);
				}
				// Otherwise use the default path.
				else
				{
					PendingUpdate = new FTexture2DStreamIn_IO_AsyncReallocate(this, bHighPrio);
				}
			}
		}
		else // Generic path
		{
			FTextureMipAllocator* MipAllocator = nullptr;
			FTextureMipDataProvider* DefaultMipDataProvider = nullptr;

	#if WITH_EDITORONLY_DATA
			if (FPlatformProperties::HasEditorOnlyData() && !GetOutermost()->bIsCookedForEditor)
			{
				DefaultMipDataProvider = new FTexture2DMipDataProvider_DDC(this);
			}
			else 
#endif
			{
				DefaultMipDataProvider = new FTexture2DMipDataProvider_IO(this, bHighPrio);
			}

			// FTexture2DMipAllocator_Virtual?
			if (GRHISupportsAsyncTextureCreation)
			{
				MipAllocator = new FTexture2DMipAllocator_AsyncCreate(this);
			}
			else
			{
				MipAllocator = new FTexture2DMipAllocator_AsyncReallocate(this);
			}

			PendingUpdate = new FTextureStreamIn(this, MipAllocator, CustomMipDataProvider, DefaultMipDataProvider);
		}
		return !PendingUpdate->IsCancelled();
	}
	return false;
}

bool UTexture2D::StreamOut(int32 NewMipCount)
{
	check(IsInGameThread());

	FTexture2DResource* Texture2DResource = Resource ? Resource->GetTexture2DResource() : nullptr;
	if (!HasPendingInitOrStreaming() && CachedSRRState.StreamOut(NewMipCount) && ensure(Texture2DResource))
	{
		if (Texture2DResource->bUsePartiallyResidentMips)
		{
			PendingUpdate = new FTexture2DStreamOut_Virtual(this);
		}
		else
		{
			PendingUpdate = new FTexture2DStreamOut_AsyncReallocate(this);
		}
		return !PendingUpdate->IsCancelled();
	}
	return false;
}


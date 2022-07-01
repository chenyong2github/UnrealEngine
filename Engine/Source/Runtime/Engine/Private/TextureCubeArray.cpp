// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TextureCubeArray.cpp: UTextureCubeArray implementation.
=============================================================================*/

#include "Engine/TextureCubeArray.h"
#include "RenderUtils.h"
#include "TextureResource.h"
#include "EngineUtils.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Containers/ResourceArray.h"

class FTextureCubeArrayResource : public FTextureResource
{
	/** The FName of the LODGroup-specific stat	*/
	FName					LODGroupStatName;

public:
	/**
	 * Minimal initialization constructor.
	 * @param InOwner - The UTextureCube which this FTextureCubeResource represents.
	 */
	FTextureCubeArrayResource(UTextureCubeArray* InOwner)
		: Owner(InOwner)
		, TextureSize(0)
		, ProxiedResource(nullptr)
	{
		check(Owner);
		check(Owner->GetNumMips() > 0);

		TIndirectArray<FTexture2DMipMap>& Mips = InOwner->PlatformData->Mips;
		const int32 FirstMipTailIndex = Mips.Num() - FMath::Max(1, InOwner->PlatformData->GetNumMipsInTail());

		NumSlices = Owner->GetNumSlices();
		MipData.Empty(NumSlices * (FirstMipTailIndex + 1));

		for (int32 MipIndex = 0; MipIndex <= FirstMipTailIndex; MipIndex++)
		{
			FTexture2DMipMap& Mip = Mips[MipIndex];
			if (Mip.BulkData.GetBulkDataSize() <= 0)
			{
				UE_LOG(LogTexture, Error, TEXT("Corrupt texture [%s]! Missing bulk data for MipIndex=%d"), *InOwner->GetFullName(), MipIndex);
			}
			else
			{
				TextureSize += Mip.BulkData.GetBulkDataSize();
				uint32 MipSize = Mip.BulkData.GetBulkDataSize() / NumSlices;

				uint8* In = (uint8*)Mip.BulkData.Lock(LOCK_READ_ONLY);
				for (uint32 Face = 0; Face < NumSlices; ++Face)
				{
					void* MipMemory = FMemory::Malloc(MipSize);
					FMemory::Memcpy(MipMemory, In + MipSize * Face, MipSize);
					MipData.Add(MipMemory);
				}
				Mip.BulkData.Unlock();
			}
		}
		STAT(LODGroupStatName = TextureGroupStatFNames[InOwner->LODGroup]);
	}

	/**
	 * Minimal initialization constructor.
	 * @param InOwner           - The UTextureCube which this FTextureCubeResource represents.
	 * @param InProxiedResource - The resource to proxy.
	 */
	FTextureCubeArrayResource(UTextureCubeArray* InOwner, const FTextureCubeArrayResource* InProxiedResource)
		: Owner(InOwner)
		, NumSlices(0u)
		, TextureSize(0)
		, ProxiedResource(InProxiedResource)
	{
		check(Owner);
	}

	/**
	 * Destructor, freeing MipData in the case of resource being destroyed without ever
	 * having been initialized by the rendering thread via InitRHI.
	 */
	~FTextureCubeArrayResource()
	{
		// Make sure we're not leaking memory if InitRHI has never been called.
		for (void* Mip : MipData)
		{
			if (Mip)
			{
				FMemory::Free(Mip);
			}
		}
	}

	/**
	 * Called when the resource is initialized. This is only called by the rendering thread.
	 */
	virtual void InitRHI() override
	{
		if (ProxiedResource)
		{
			TextureCubeRHI = ProxiedResource->GetTextureCubeRHI();
			TextureRHI = TextureCubeRHI;
			SamplerStateRHI = ProxiedResource->SamplerStateRHI;
			RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, TextureRHI);
			return;
		}

		INC_DWORD_STAT_BY(STAT_TextureMemory, TextureSize);
		INC_DWORD_STAT_FNAME_BY(LODGroupStatName, TextureSize);

		const uint32 ArraySize = NumSlices / 6u;

		check(Owner->GetNumMips() > 0);

		// Create the RHI texture.
		const ETextureCreateFlags TexCreateFlags = (Owner->SRGB ? ETextureCreateFlags::SRGB : ETextureCreateFlags::None) | (Owner->bNotOfflineProcessed ? ETextureCreateFlags::None : ETextureCreateFlags::OfflineProcessed);
		const FString Name = Owner->GetPathName();

		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::CreateCubeArray(*Name)
			.SetExtent(Owner->GetSizeX())
			.SetArraySize(ArraySize)
			.SetFormat(Owner->GetPixelFormat())
			.SetNumMips(Owner->GetNumMips())
			.SetFlags(TexCreateFlags)
			.SetExtData(Owner->PlatformData->GetExtData());

		TextureCubeRHI = RHICreateTexture(Desc);
		TextureRHI = TextureCubeRHI;
		TextureRHI->SetName(Owner->GetFName());

		RHIBindDebugLabelName(TextureRHI, *Name);
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, TextureRHI);

		// Read the mip-levels into the RHI texture.
		const int32 FirstMipTailIndex = Owner->GetNumMips() - FMath::Max(1, Owner->PlatformData->GetNumMipsInTail());
		for (int32 MipIndex = 0; MipIndex <= FirstMipTailIndex; MipIndex++)
		{
			for (uint32 ArrayIndex = 0u; ArrayIndex < ArraySize; ++ArrayIndex)
			{
				for (uint32 FaceIndex = 0u; FaceIndex < 6u; ++FaceIndex)
				{
					uint32 DestStride;
					void* TheMipData = RHILockTextureCubeFace(TextureCubeRHI, FaceIndex, ArrayIndex, MipIndex, RLM_WriteOnly, DestStride, false);
					GetData(ArrayIndex, FaceIndex, MipIndex, TheMipData, DestStride);
					RHIUnlockTextureCubeFace(TextureCubeRHI, FaceIndex, ArrayIndex, MipIndex, false);
				}
			}
		}

		// Create the sampler state RHI resource.
		FSamplerStateInitializerRHI SamplerStateInitializer
		(
			(ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(Owner),
			AM_Clamp,
			AM_Clamp,
			AM_Clamp
		);
		SamplerStateRHI = GetOrCreateSamplerState(SamplerStateInitializer);

		// Set the greyscale format flag appropriately.
		EPixelFormat PixelFormat = Owner->GetPixelFormat();
		bGreyScaleFormat = (PixelFormat == PF_G8) || (PixelFormat == PF_BC4);
	}

	virtual void ReleaseRHI() override
	{
		DEC_DWORD_STAT_BY(STAT_TextureMemory, TextureSize);
		DEC_DWORD_STAT_FNAME_BY(LODGroupStatName, TextureSize);
		RHIUpdateTextureReference(Owner->TextureReference.TextureReferenceRHI, nullptr);
		TextureCubeRHI.SafeRelease();
		FTextureResource::ReleaseRHI();
	}

	/** Returns the width of the texture in pixels. */
	virtual uint32 GetSizeX() const override
	{
		if (ProxiedResource)
		{
			return ProxiedResource->GetSizeX();
		}

		return Owner->GetSizeX();
	}

	/** Returns the height of the texture in pixels. */
	virtual uint32 GetSizeY() const override
	{
		if (ProxiedResource)
		{
			return ProxiedResource->GetSizeY();
		}

		return Owner->GetSizeY();
	}

	/** Returns the depth of the texture in pixels. */
	virtual uint32 GetSizeZ() const
	{
		if (ProxiedResource)
		{
			return ProxiedResource->GetSizeZ();
		}

		return Owner->GetNumSlices();
	}

	FTextureCubeRHIRef GetTextureCubeRHI() const
	{
		return TextureCubeRHI;
	}

	virtual bool IsProxy() const override { return ProxiedResource != nullptr; }

	const FTextureCubeArrayResource* GetProxiedResource() const { return ProxiedResource; }
private:
	/** A reference to the texture's RHI resource as a cube-map texture. */
	FTextureCubeRHIRef TextureCubeRHI;

	/** Local copy/ cache of mip data. Only valid between creation and first call to InitRHI */
	TArray<void*> MipData;

	/** The UTextureCube which this resource represents. */
	const UTextureCubeArray* Owner;

	/** Number of 2D faces per mip, equal to array size  6 */
	uint32 NumSlices;

	// Cached texture size for stats. */
	int32	TextureSize;

	const FTextureCubeArrayResource* const ProxiedResource;
	/**
	 * Writes the data for a single mip-level into a destination buffer.
	 * @param FaceIndex		The index of the face of the mip-level to read.
	 * @param MipIndex		The index of the mip-level to read.
	 * @param Dest			The address of the destination buffer to receive the mip-level's data.
	 * @param DestPitch		Number of bytes per row
	 */
	void GetData(uint32 SliceIndex, int32 FaceIndex, int32 MipIndex, void* Dest, uint32 DestPitch)
	{
		// Mips are stored sequentially
		// For each mip, we store 6 faces per array index
		const uint32 Index = MipIndex * NumSlices + SliceIndex * 6u + FaceIndex;
		check(MipData[Index]);

		// for platforms that returned 0 pitch from Lock, we need to just use the bulk data directly, never do 
		// runtime block size checking, conversion, or the like
		if (DestPitch == 0)
		{
			FMemory::Memcpy(Dest, MipData[Index], Owner->PlatformData->Mips[MipIndex].BulkData.GetBulkDataSize() / Owner->GetNumSlices());
		}
		else
		{
			EPixelFormat PixelFormat = Owner->GetPixelFormat();
			uint32 NumRows = 0;
			uint32 SrcPitch = 0;
			uint32 BlockSizeX = GPixelFormats[PixelFormat].BlockSizeX;	// Block width in pixels
			uint32 BlockSizeY = GPixelFormats[PixelFormat].BlockSizeY;	// Block height in pixels
			uint32 BlockBytes = GPixelFormats[PixelFormat].BlockBytes;

			FIntPoint MipExtent = CalcMipMapExtent(Owner->GetSizeX(), Owner->GetSizeY(), PixelFormat, MipIndex);

			uint32 NumColumns = (MipExtent.X + BlockSizeX - 1) / BlockSizeX;	// Num-of columns in the source data (in blocks)
			NumRows = (MipExtent.Y + BlockSizeY - 1) / BlockSizeY;	// Num-of rows in the source data (in blocks)
			SrcPitch = NumColumns * BlockBytes;		// Num-of bytes per row in the source data

			SIZE_T MipSizeInBytes = CalcTextureMipMapSize(MipExtent.X, MipExtent.Y, PixelFormat, 0);

			if (SrcPitch == DestPitch)
			{
				// Copy data, not taking into account stride!
				FMemory::Memcpy(Dest, MipData[Index], MipSizeInBytes);
			}
			else
			{
				// Copy data, taking the stride into account!
				uint8* Src = (uint8*)MipData[Index];
				uint8* Dst = (uint8*)Dest;
				for (uint32 Row = 0; Row < NumRows; ++Row)
				{
					FMemory::Memcpy(Dst, Src, SrcPitch);
					Src += SrcPitch;
					Dst += DestPitch;
				}
				check((PTRINT(Src) - PTRINT(MipData[Index])) == PTRINT(MipSizeInBytes));
			}
		}

		FMemory::Free(MipData[Index]);
		MipData[Index] = nullptr;
	}
};

UTextureCubeArray::UTextureCubeArray(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	SRGB = true;
	MipGenSettings = TMGS_NoMipmaps;
#endif
}

FTextureResource* UTextureCubeArray::CreateResource()
{
	const FPixelFormatInfo& FormatInfo = GPixelFormats[GetPixelFormat()];
	if (GetNumMips() > 0 && FormatInfo.Supported)
	{
		return new FTextureCubeArrayResource(this);
	}
	else if (GetNumMips() == 0)
	{
		UE_LOG(LogTexture, Warning, TEXT("%s contains no miplevels! Please delete."), *GetFullName());
	}
	else if (!FormatInfo.Supported)
	{
		UE_LOG(LogTexture, Warning, TEXT("%s cannot be created, rhi does not support format %s."), *GetFullName(), FormatInfo.Name);
	}
	return nullptr;
}

void UTextureCubeArray::UpdateResource()
{
#if WITH_EDITOR
	// Re-cache platform data if the source has changed.
	CachePlatformData();
#endif // #if WITH_EDITOR

	Super::UpdateResource();
}

uint32 UTextureCubeArray::CalcTextureMemorySize(int32 MipCount) const
{
	uint32 Size = 0;
	if (PlatformData)
	{
		int32 SizeX = GetSizeX();
		int32 SizeY = GetSizeY();
		const int32 ArraySize = GetNumSlices() / 6;
		int32 NumMips = GetNumMips();
		EPixelFormat Format = GetPixelFormat();

		ensureMsgf(SizeX == SizeY, TEXT("Cubemap faces expected to be square.  Actual sizes are: %i, %i"), SizeX, SizeY);

		// Figure out what the first mip to use is.
		int32 FirstMip = FMath::Max(0, NumMips - MipCount);
		FIntPoint MipExtents = CalcMipMapExtent(SizeX, SizeY, Format, FirstMip);

		// TODO add RHICalcTextureCubeArrayPlatformSize
		uint32 TextureAlign = 0;
		uint64 TextureSize = RHICalcTextureCubePlatformSize(MipExtents.X, Format, FMath::Max(1, MipCount), TexCreate_None, FRHIResourceCreateInfo(PlatformData->GetExtData()), TextureAlign) * ArraySize;
		Size = (uint32)TextureSize;
	}
	return Size;
}

uint32 UTextureCubeArray::CalcTextureMemorySizeEnum(ETextureMipCount Enum) const
{
	if (Enum == TMC_ResidentMips || Enum == TMC_AllMipsBiased)
	{
		return CalcTextureMemorySize(GetNumMips() - GetCachedLODBias());
	}

	return CalcTextureMemorySize(GetNumMips());
}

#if WITH_EDITOR
ENGINE_API bool UTextureCubeArray::CheckArrayTexturesCompatibility()
{
	if (SourceTextures.Num() == 0)
	{
		return false;
	}

	for (int32 TextureIndex = 0; TextureIndex < SourceTextures.Num(); ++TextureIndex)
	{
		// Do not create array till all texture slots are filled.
		if (!SourceTextures[TextureIndex])
		{
			return false;
		}

		// Force the async texture build to complete
		// TODO - better way to do this?
		SourceTextures[TextureIndex]->GetPlatformData();
	}

	const FTextureSource& TextureSource = SourceTextures[0]->Source;
	const FString TextureName = SourceTextures[0]->GetName();
	const ETextureSourceFormat SourceFormat = TextureSource.GetFormat();
	const int32 SizeX = TextureSource.GetSizeX();
	const int32 SizeY = TextureSource.GetSizeY();
	const int32 NumSlices = TextureSource.GetNumSlices();

	ensure(NumSlices == 1 || NumSlices == 6); // either cubemap, or lat/long map

	bool bError = false;
	for (int32 TextureCmpIndex =1; TextureCmpIndex < SourceTextures.Num(); ++TextureCmpIndex)
	{
		const FTextureSource& TextureSourceCmp = SourceTextures[TextureCmpIndex]->Source;
		const FString TextureNameCmp = SourceTextures[TextureCmpIndex]->GetName();
		const ETextureSourceFormat SourceFormatCmp = TextureSourceCmp.GetFormat();

		if (TextureSourceCmp.GetSizeX() != SizeX || TextureSourceCmp.GetSizeY() != SizeY)
		{
			UE_LOG(LogTexture, Warning, TEXT("TextureCubeArray creation failed. Textures %s and %s have different sizes."), *TextureName, *TextureNameCmp);
			bError = true;
		}

		if (TextureSourceCmp.GetNumSlices() != NumSlices)
		{
			UE_LOG(LogTexture, Warning, TEXT("TextureCubeArray creation failed. Textures %s and %s have different number of faces (some are long/lat, some are not)."), *TextureName, *TextureNameCmp);
			bError = true;
		}

		if (SourceFormatCmp != SourceFormat)
		{
			UE_LOG(LogTexture, Warning, TEXT("TextureCubeArray creation failed. Textures %s and %s have incompatible pixel formats."), *TextureName, *TextureNameCmp);
			bError = true;
		}
	}

	return (!bError);
}


ENGINE_API bool UTextureCubeArray::UpdateSourceFromSourceTextures(bool bCreatingNewTexture)
{
	if (!CheckArrayTexturesCompatibility())
	{
		return false;
	}
	
	Modify();

	const FTextureSource& InitialSource = SourceTextures[0]->Source;
	// Format and format size.
	const EPixelFormat PixelFormat = SourceTextures[0]->GetPixelFormat();
	const ETextureSourceFormat Format = InitialSource.GetFormat();
	const int32 FormatDataSize = InitialSource.GetBytesPerPixel();
	// X,Y,Z size of the array.
	const int32 SizeX = InitialSource.GetSizeX();
	const int32 SizeY = InitialSource.GetSizeY();
	const int32 NumSlices = InitialSource.GetNumSlices();
	const uint32 ArraySize = SourceTextures.Num();
	// Only copy the first mip from the source textures to array texture.
	const uint32 NumMips = 1;

	// This should be false when texture is updated to avoid overriding user settings.
	if (bCreatingNewTexture)
	{
		CompressionSettings = SourceTextures[0]->CompressionSettings;
		MipGenSettings = TMGS_NoMipmaps;
		PowerOfTwoMode = ETexturePowerOfTwoSetting::None;
		LODGroup = SourceTextures[0]->LODGroup;
		SRGB = SourceTextures[0]->SRGB;
		NeverStream = true;
	}

	// Create the source texture for this UTexture.
	Source.Init(SizeX, SizeY, ArraySize * NumSlices, NumMips, Format);
	Source.bLongLatCubemap = (NumSlices == 1);

	// this path sets bLongLatCubemap for CubeArray
	//  most paths do not, so it is not reliable

	// We only copy the top level Mip map.
	TArray<uint8*, TInlineAllocator<MAX_TEXTURE_MIP_COUNT> > DestMipData;
	TArray<uint64, TInlineAllocator<MAX_TEXTURE_MIP_COUNT> > MipSizeBytes;
	DestMipData.AddZeroed(NumMips);
	MipSizeBytes.AddZeroed(NumMips);

	for (uint32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
	{
		DestMipData[MipIndex] = Source.LockMip(MipIndex);
		MipSizeBytes[MipIndex] = Source.CalcMipSize(MipIndex) / ArraySize;
	}

	TArray64<uint8> RefCubeData;
	for (int32 SourceTexIndex = 0; SourceTexIndex < SourceTextures.Num(); ++SourceTexIndex)
	{
		for (uint32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
		{
			const uint64 Size = MipSizeBytes[MipIndex];
			const uint64 CheckSize = SourceTextures[SourceTexIndex]->Source.CalcMipSize(MipIndex);
			check(Size == CheckSize);

			RefCubeData.Reset();
			SourceTextures[SourceTexIndex]->Source.GetMipData(RefCubeData, MipIndex);
			void* Dst = DestMipData[MipIndex] + Size * SourceTexIndex;
			FMemory::Memcpy(Dst, RefCubeData.GetData(), Size);
		}
	}

	for (uint32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
	{
		Source.UnlockMip(MipIndex);
	}

	SetLightingGuid();
	ValidateSettingsAfterImportOrEdit();
	UpdateResource();
	
	return true;
}

ENGINE_API void UTextureCubeArray::InvadiateTextureSource()
{
	Modify();

	if (PlatformData)
	{
		delete PlatformData;
		PlatformData = NULL;
	}

	Source = FTextureSource();
	Source.SetOwner(this);
	UpdateResource();
	
}
#endif

void UTextureCubeArray::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UTextureCubeArray::Serialize"), STAT_TextureCubeArray_Serialize, STATGROUP_LoadTime);

	Super::Serialize(Ar);

	FStripDataFlags StripFlags(Ar);
	bool bCooked = Ar.IsCooking();
	Ar << bCooked;

	if (bCooked || Ar.IsCooking())
	{
		SerializeCookedPlatformData(Ar);
	}

#if WITH_EDITOR
	if (Ar.IsLoading() && !Ar.IsTransacting() && !bCooked)
	{
		BeginCachePlatformData();
	}
#endif
}

void UTextureCubeArray::PostLoad()
{
#if WITH_EDITOR
	FinishCachePlatformData();	
#endif // #if WITH_EDITOR

	Super::PostLoad();
};

void UTextureCubeArray::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
#if WITH_EDITOR
	int32 SizeX = Source.GetSizeX();
	int32 SizeY = Source.GetSizeY();
	int32 ArraySize = Source.GetNumSlices() / 6;
#else
	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 ArraySize = 0;
#endif
	const FString Dimensions = FString::Printf(TEXT("%dx%d*%d"), SizeX, SizeY, ArraySize);
	OutTags.Add(FAssetRegistryTag("Dimensions", Dimensions, FAssetRegistryTag::TT_Dimensional));
	OutTags.Add(FAssetRegistryTag("Format", GPixelFormats[GetPixelFormat()].Name, FAssetRegistryTag::TT_Alphabetical));

	Super::GetAssetRegistryTags(OutTags);
}

FString UTextureCubeArray::GetDesc()
{
	return FString::Printf(TEXT("CubeArray: %dx%d*%d [%s]"),
		GetSizeX(),
		GetSizeY(),
		GetNumSlices() / 6,
		GPixelFormats[GetPixelFormat()].Name
	);
}

void UTextureCubeArray::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize)
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	CumulativeResourceSize.AddUnknownMemoryBytes(CalcTextureMemorySizeEnum(TMC_ResidentMips));
}

#if WITH_EDITOR
uint32 UTextureCubeArray::GetMaximumDimension() const
{
	return GetMaxCubeTextureDimension();

}

ENGINE_API void UTextureCubeArray::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UTextureCubeArray, SourceTextures))
	{
		// Empty SourceTextures, remove any resources if present.
		if (SourceTextures.Num() == 0)
		{
			InvadiateTextureSource();
		}
		// First entry into an empty texture array.
		else if (SourceTextures.Num() == 1)
		{
			UpdateSourceFromSourceTextures(true);
		}
		// Couldn't add to non-empty array (Error msg logged).
		else if (UpdateSourceFromSourceTextures(false) == false)
		{
			int32 ChangedIndex = PropertyChangedEvent.GetArrayIndex(PropertyName.ToString());
			int32 LastIndex = SourceTextures.Num() - 1;

			// But don't remove an empty texture, only an incompatible one.
			if (SourceTextures[LastIndex] != nullptr && ChangedIndex == LastIndex)
			{
				SourceTextures.RemoveAt(LastIndex);
			}
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // #if WITH_EDITOR

// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Texture2DArray.cpp: UTexture2DArray implementation.
=============================================================================*/

#include "Engine/Texture2DArray.h"
#include "RenderUtils.h"
#include "TextureResource.h"
#include "EngineUtils.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Containers/ResourceArray.h"
#include "Rendering/Texture2DArrayResource.h"

// Master switch to control whether streaming is enabled for texture2darray. 
bool GSupportsTexture2DArrayStreaming = false;

static TAutoConsoleVariable<int32> CVarAllowTexture2DArrayAssetCreation(
	TEXT("r.AllowTexture2DArrayCreation"),
	1,
	TEXT("Enable UTexture2DArray assets"),
	ECVF_Default
);

UTexture2DArray::UTexture2DArray(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
#if WITH_EDITORONLY_DATA
	SRGB = true;
	MipGenSettings = TMGS_NoMipmaps;
#endif
}

FTextureResource* UTexture2DArray::CreateResource()
{
	const FPixelFormatInfo& FormatInfo = GPixelFormats[GetPixelFormat()];
	if (GetNumMips() > 0 && FormatInfo.Supported)
	{
		return new FTexture2DArrayResource(this, GetResourcePostInitState(PlatformData, GSupportsTexture2DArrayStreaming));
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

void UTexture2DArray::UpdateResource()
{
#if WITH_EDITOR
	// Re-cache platform data if the source has changed.
	CachePlatformData();
#endif // #if WITH_EDITOR

	Super::UpdateResource();
}

uint32 UTexture2DArray::CalcTextureMemorySize(int32 MipCount) const
{
	const FPixelFormatInfo& FormatInfo = GPixelFormats[GetPixelFormat()];

	uint32 Size = 0;
	if (FormatInfo.Supported && PlatformData)
	{
		const EPixelFormat Format = GetPixelFormat();
		if (Format != PF_Unknown)
		{
			const ETextureCreateFlags Flags = (SRGB ? TexCreate_SRGB : TexCreate_None)  | (bNotOfflineProcessed ? TexCreate_None : TexCreate_OfflineProcessed) | (bNoTiling ? TexCreate_NoTiling : TexCreate_None);
			const int32 NumMips = GetNumMips();
			const int32 FirstMip = FMath::Max(0, NumMips - MipCount);

			// Must be consistent with the logic in FTexture2DResource::InitRHI
			const FIntPoint MipExtents = CalcMipMapExtent(GetSizeX(), GetSizeY(), Format, FirstMip);
			uint32 TextureAlign = 0;
			Size = (uint32)(GetNumSlices() * RHICalcTexture2DPlatformSize(MipExtents.X, MipExtents.Y, Format, FMath::Max(1, MipCount), 1, Flags, FRHIResourceCreateInfo(PlatformData->GetExtData()), TextureAlign));
		}
	}
	return Size;
}

uint32 UTexture2DArray::CalcTextureMemorySizeEnum(ETextureMipCount Enum) const
{
	if (Enum == TMC_ResidentMips || Enum == TMC_AllMipsBiased) 
	{
		return CalcTextureMemorySize(GetNumMips() - GetCachedLODBias());
	}

	return CalcTextureMemorySize(GetNumMips());
}

#if WITH_EDITOR

ENGINE_API bool UTexture2DArray::CheckArrayTexturesCompatibility()
{
	bool bError = false;

	for (int32 TextureIndex = 0; TextureIndex < SourceTextures.Num(); ++TextureIndex)
	{
		// Do not create array till all texture slots are filled.
		if (!SourceTextures[TextureIndex])
		{
			return false;
		}

		FTextureSource& TextureSource = SourceTextures[TextureIndex]->Source;
		// const int32 FormatDataSize = TextureSource.GetBytesPerPixel();
		const EPixelFormat PixelFormat = SourceTextures[TextureIndex]->GetPixelFormat();
		const int32 SizeX = TextureSource.GetSizeX();
		const int32 SizeY = TextureSource.GetSizeY();

		for (int32 TextureCmpIndex = TextureIndex + 1; TextureCmpIndex < SourceTextures.Num(); ++TextureCmpIndex)
		{
			// Do not create array till all texture slots are filled.
			if (!SourceTextures[TextureCmpIndex]) 
			{
				return false;
			}

			FTextureSource& TextureSourceCmp = SourceTextures[TextureCmpIndex]->Source;
			FString TextureName = SourceTextures[TextureIndex]->GetFName().ToString();
			FString TextureNameCmp = SourceTextures[TextureCmpIndex]->GetFName().ToString();
			const EPixelFormat PixelFormatCmp = SourceTextures[TextureCmpIndex]->GetPixelFormat();

			if (TextureSourceCmp.GetSizeX() != SizeX || TextureSourceCmp.GetSizeY() != SizeY)
			{
				UE_LOG(LogTexture, Warning, TEXT("Texture2DArray creation failed. Textures %s and %s have different sizes."), *TextureName, *TextureNameCmp);
				bError = true;
			}

			if (PixelFormatCmp != PixelFormat)
			{
				UE_LOG(LogTexture, Warning, TEXT("Texture2DArray creation failed. Textures %s and %s have incompatible pixel formats."), *TextureName, *TextureNameCmp);
				bError = true;
			}

			//if (TextureSourceCmp.GetBytesPerPixel() != FormatDataSize)
			//{
			//	UE_LOG(LogTexture, Warning, TEXT("Texture2DArray creation failed. Textures %s and %s have incompatible pixel formats."), *TextureName, *TextureNameCmp);
			//	bError = true;
			//}
		}
	}

	return (!bError);
}


ENGINE_API bool UTexture2DArray::UpdateSourceFromSourceTextures(bool bCreatingNewTexture)
{
	if (!CheckArrayTexturesCompatibility()) 
	{
		return false;
	}

	if (SourceTextures.Num() > 0)
	{
		FTextureSource& InitialSource = SourceTextures[0]->Source;
		// Format and format size.
		EPixelFormat PixelFormat = SourceTextures[0]->GetPixelFormat();
		ETextureSourceFormat Format = InitialSource.GetFormat();
		int32 FormatDataSize = InitialSource.GetBytesPerPixel();
		// X,Y,Z size of the array.
		int32 SizeX = SourceTextures[0]->GetSizeX();
		int32 SizeY = SourceTextures[0]->GetSizeY();
		uint32 NumSlices = SourceTextures.Num();
		// Only copy the first mip from the source textures to array texture.
		uint32 NumMips = 1;

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
		Source.Init(SizeX, SizeY, NumSlices, NumMips, Format);

		// We only copy the top level Mip map.
		uint8* DestMipData[MAX_TEXTURE_MIP_COUNT] = { 0 };
		int32 MipSizeBytes[MAX_TEXTURE_MIP_COUNT] = { 0 };
			
		for (uint32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
		{
			DestMipData[MipIndex] =  Source.LockMip(MipIndex);
			MipSizeBytes[MipIndex] = Source.CalcMipSize(MipIndex) / NumSlices;
		}

		for (int32 SourceTexIndex = 0; SourceTexIndex < SourceTextures.Num(); ++SourceTexIndex)
		{
			for (uint32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
			{
				TArray64<uint8> Ref2DData;
				SourceTextures[SourceTexIndex]->Source.GetMipData(Ref2DData, MipIndex);
				void* Dst = DestMipData[MipIndex] + MipSizeBytes[MipIndex] * SourceTexIndex;
				FMemory::Memcpy(Dst, Ref2DData.GetData(), MipSizeBytes[MipIndex]);
			}
		}

		for (uint32 MipIndex = 0; MipIndex < NumMips; ++MipIndex)
		{
			Source.UnlockMip(MipIndex);
		}

		UpdateMipGenSettings();
		SetLightingGuid();
		UpdateResource();
	}

	return true;
}

ENGINE_API void UTexture2DArray::InvadiateTextureSource()
{
	if (PlatformData) 
	{
		delete PlatformData;
		PlatformData = NULL;
	}

	Source.Init(0, 0, 0, 0, TSF_Invalid, nullptr);
	UpdateResource();
}
#endif

void UTexture2DArray::Serialize(FArchive& Ar)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("UTexture2DArray::Serialize"), STAT_Texture2DArray_Serialize, STATGROUP_LoadTime);
	
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

void UTexture2DArray::PostLoad() 
{
#if WITH_EDITOR
	FinishCachePlatformData();

#endif // #if WITH_EDITOR
	Super::PostLoad();
};

void UTexture2DArray::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
#if WITH_EDITOR
	int32 SizeX = Source.GetSizeX();
	int32 SizeY = Source.GetSizeY();
	int32 SizeZ = Source.GetNumSlices(); //GetSizeZ()
#else
	int32 SizeX = 0;
	int32 SizeY = 0;
	int32 SizeZ = 0;
#endif
	const FString Dimensions = FString::Printf(TEXT("%dx%dx%d"), SizeX, SizeY, SizeZ);
	OutTags.Add(FAssetRegistryTag("Dimensions", Dimensions, FAssetRegistryTag::TT_Dimensional));
	OutTags.Add(FAssetRegistryTag("Format", GPixelFormats[GetPixelFormat()].Name, FAssetRegistryTag::TT_Alphabetical));

	Super::GetAssetRegistryTags(OutTags);
}

FString UTexture2DArray::GetDesc() 
{
	return FString::Printf(TEXT("Array: %dx%dx%d [%s]"),
		GetSizeX(),
		GetSizeY(),
		GetNumSlices(),
		GPixelFormats[GetPixelFormat()].Name
	);
}

void UTexture2DArray::GetResourceSizeEx(FResourceSizeEx& CumulativeResourceSize) 
{
	Super::GetResourceSizeEx(CumulativeResourceSize);
	CumulativeResourceSize.AddUnknownMemoryBytes(CalcTextureMemorySizeEnum(TMC_ResidentMips));
}

#if WITH_EDITOR
uint32 UTexture2DArray::GetMaximumDimension() const
{
	return GetMax2DTextureDimension();

}

void UTexture2DArray::UpdateMipGenSettings()
{
	if (PowerOfTwoMode == ETexturePowerOfTwoSetting::None && !Source.IsPowerOfTwo())
	{
		// Force NPT textures to have no mip maps.
		MipGenSettings = TMGS_NoMipmaps;
		NeverStream = true;
	}
}

ENGINE_API void UTexture2DArray::PostEditChangeProperty(FPropertyChangedEvent & PropertyChangedEvent)
{	
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PowerOfTwoMode == ETexturePowerOfTwoSetting::None && (!Source.IsPowerOfTwo()))
	{
		// Force NPT textures to have no mip maps.
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UTexture2DArray, MipGenSettings)) 
		{
			UE_LOG(LogTexture, Warning, TEXT("Cannot use mip maps for non-power of two textures."));
		}

		MipGenSettings = TMGS_NoMipmaps;
		NeverStream = true;
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UTexture2DArray, SourceTextures))
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

	if (PropertyName == GET_MEMBER_NAME_CHECKED(UTexture2DArray, AddressX)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UTexture2DArray, AddressY)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UTexture2DArray, AddressZ))
	{
		UpdateResource();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

#endif // #if WITH_EDITOR

bool UTexture2DArray::StreamOut(int32 NewMipCount)
{
	return false;
}

bool UTexture2DArray::StreamIn(int32 NewMipCount, bool bHighPrio)
{
	return false;
}

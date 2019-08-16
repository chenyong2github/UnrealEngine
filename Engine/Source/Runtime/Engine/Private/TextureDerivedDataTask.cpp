// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TextureDerivedDataTask.cpp: Tasks to update texture DDC.
=============================================================================*/

#include "TextureDerivedDataTask.h"
#include "Misc/CommandLine.h"
#include "Stats/Stats.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Serialization/MemoryReader.h"
#include "UObject/Package.h"
#include "RenderUtils.h"
#include "TextureResource.h"
#include "Engine/Texture.h"
#include "Engine/Texture2DArray.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "VT/VirtualTextureBuiltData.h"

#if WITH_EDITOR

#include "DerivedDataCacheInterface.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITextureFormat.h"
#include "Engine/TextureCube.h"
#include "ProfilingDebugging/CookStats.h"
#include "VT/VirtualTextureDataBuilder.h"

class FTextureStatusMessageContext : public FScopedSlowTask
{
public:
	explicit FTextureStatusMessageContext(const FText& InMessage)
		: FScopedSlowTask(0, InMessage, IsInGameThread())
	{
		UE_LOG(LogTexture,Display,TEXT("%s"),*InMessage.ToString());
	}
};


void FTextureSourceData::Init(UTexture& InTexture, const FTextureBuildSettings* InBuildSettingsPerLayer, bool bAllowAsyncLoading)
{
	const int32 NumBlocks = InTexture.Source.GetNumBlocks();
	const int32 NumLayers = InTexture.Source.GetNumLayers();
	if (NumBlocks < 1 || NumLayers < 1)
	{
		UE_LOG(LogTexture, Warning, TEXT("Texture has no source data: %s"), *InTexture.GetPathName());
		return;
	}

	Layers.Reserve(NumLayers);
	for (int LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		FTextureSourceLayerData* LayerData = new(Layers) FTextureSourceLayerData();
		switch (InTexture.Source.GetFormat(LayerIndex))
		{
		case TSF_G8:		LayerData->ImageFormat = ERawImageFormat::G8;		break;
		case TSF_BGRA8:		LayerData->ImageFormat = ERawImageFormat::BGRA8;	break;
		case TSF_BGRE8:		LayerData->ImageFormat = ERawImageFormat::BGRE8;	break;
		case TSF_RGBA16:	LayerData->ImageFormat = ERawImageFormat::RGBA16;	break;
		case TSF_RGBA16F:	LayerData->ImageFormat = ERawImageFormat::RGBA16F;  break;
		default:
			UE_LOG(LogTexture, Fatal, TEXT("Texture %s has source art in an invalid format."), *InTexture.GetName());
			return;
		}

		FTextureFormatSettings FormatSettings;
		InTexture.GetLayerFormatSettings(LayerIndex, FormatSettings);
		LayerData->GammaSpace = FormatSettings.SRGB ? (InTexture.bUseLegacyGamma ? EGammaSpace::Pow22 : EGammaSpace::sRGB) : EGammaSpace::Linear;
	}

	Blocks.Reserve(NumBlocks);
	for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
	{
		FTextureSourceBlock SourceBlock;
		InTexture.Source.GetBlock(BlockIndex, SourceBlock);

		if (SourceBlock.NumMips > 0 && SourceBlock.NumSlices > 0)
		{
			FTextureSourceBlockData* BlockData = new(Blocks) FTextureSourceBlockData();
			BlockData->BlockX = SourceBlock.BlockX;
			BlockData->BlockY = SourceBlock.BlockY;
			BlockData->SizeX = SourceBlock.SizeX;
			BlockData->SizeY = SourceBlock.SizeY;
			BlockData->NumMips = SourceBlock.NumMips;
			BlockData->NumSlices = SourceBlock.NumSlices;

			if (InBuildSettingsPerLayer[0].MipGenSettings != TMGS_LeaveExistingMips)
			{
				BlockData->NumMips = 1;
			}

			if (!InBuildSettingsPerLayer[0].bCubemap && !InBuildSettingsPerLayer[0].bTextureArray && !InBuildSettingsPerLayer[0].bVolume)
			{
				BlockData->NumSlices = 1;
			}

			BlockData->MipsPerLayer.SetNum(NumLayers);

			SizeInBlocksX = FMath::Max(SizeInBlocksX, SourceBlock.BlockX + 1);
			SizeInBlocksY = FMath::Max(SizeInBlocksY, SourceBlock.BlockY + 1);
			BlockSizeX = FMath::Max(BlockSizeX, SourceBlock.SizeX);
			BlockSizeY = FMath::Max(BlockSizeY, SourceBlock.SizeY);
		}
	}

	for (FTextureSourceBlockData& Block : Blocks)
	{
		const int32 MipBiasX = FMath::CeilLogTwo(BlockSizeX / Block.SizeX);
		const int32 MipBiasY = FMath::CeilLogTwo(BlockSizeY / Block.SizeY);
		if (MipBiasX != MipBiasY)
		{
			UE_LOG(LogTexture, Warning, TEXT("Texture has blocks with mismatched aspect ratios"), *InTexture.GetPathName());
			return;
		}

		Block.MipBias = MipBiasX;
	}

	TextureName = InTexture.GetFName();

	if (bAllowAsyncLoading && !InTexture.Source.IsBulkDataLoaded())
	{
		// Prepare the async source to be later able to load it from file if required.
		AsyncSource = InTexture.Source; // This copies information required to make a safe IO load async.
	}

	bValid = true;
}

void FTextureSourceData::GetSourceMips(FTextureSource& Source, IImageWrapperModule* InImageWrapper)
{
	if (bValid)
	{
		if (Source.HasHadBulkDataCleared())
		{	// don't do any work we can't reload this
			UE_LOG(LogTexture, Error, TEXT("Unable to get texture source mips because its bulk data was released. %s"), *TextureName.ToString())
				return;
		}

		for (int32 BlockIndex = 0; BlockIndex < Blocks.Num(); ++BlockIndex)
		{
			FTextureSourceBlock SourceBlock;
			Source.GetBlock(BlockIndex, SourceBlock);

			FTextureSourceBlockData& BlockData = Blocks[BlockIndex];
			for (int32 LayerIndex = 0; LayerIndex < Layers.Num(); ++LayerIndex)
			{
				const FTextureSourceLayerData& LayerData = Layers[LayerIndex];
				if (!BlockData.MipsPerLayer[LayerIndex].Num()) // If we already got valid data, nothing to do.
				{
					int32 MipSizeX = SourceBlock.SizeX;
					int32 MipSizeY = SourceBlock.SizeY;
					for (int32 MipIndex = 0; MipIndex < BlockData.NumMips; ++MipIndex)
					{
						FImage* SourceMip = new(BlockData.MipsPerLayer[LayerIndex]) FImage(
							MipSizeX, MipSizeY,
							BlockData.NumSlices,
							LayerData.ImageFormat,
							LayerData.GammaSpace
						);

						if (!Source.GetMipData(SourceMip->RawData, BlockIndex, LayerIndex, MipIndex, InImageWrapper))
						{
							UE_LOG(LogTexture, Warning, TEXT("Cannot retrieve source data for mip %d of texture %s"), MipIndex, *TextureName.ToString());
							ReleaseMemory();
							bValid = false;
							break;
						}

						MipSizeX = FMath::Max(MipSizeX / 2, 1);
						MipSizeY = FMath::Max(MipSizeY / 2, 1);
					}
				}
			}
		}
	}
}

void FTextureSourceData::GetAsyncSourceMips(IImageWrapperModule* InImageWrapper)
{
	if (bValid && !Blocks[0].MipsPerLayer[0].Num() && AsyncSource.GetSizeOnDisk())
	{
		if (AsyncSource.LoadBulkDataWithFileReader())
		{
			GetSourceMips(AsyncSource, InImageWrapper);
		}
	}
}

void FTextureCacheDerivedDataWorker::BuildTexture()
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("TextureName"), FText::FromString(Texture.GetName()));
	Args.Add(TEXT("TextureFormatName"), FText::FromString(BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString()));
	Args.Add(TEXT("TextureResolutionX"), FText::FromString(FString::FromInt(TextureData.Blocks[0].MipsPerLayer[0][0].SizeX)));
	Args.Add(TEXT("TextureResolutionY"), FText::FromString(FString::FromInt(TextureData.Blocks[0].MipsPerLayer[0][0].SizeY)));
	FTextureStatusMessageContext StatusMessage(FText::Format(NSLOCTEXT("Engine", "BuildTextureStatus", "Building textures: {TextureName} ({TextureFormatName}, {TextureResolutionX}X{TextureResolutionY})"), Args));

	if (!ensure(Compressor))
	{
		UE_LOG(LogTexture, Warning, TEXT("Missing Compressor required to build texture %s"), *Texture.GetPathName());
		return;
	}

	const bool bForVirtualTextureStreamingBuild = (CacheFlags & ETextureCacheFlags::ForVirtualTextureStreamingBuild) != 0;	
	if (bForVirtualTextureStreamingBuild)
	{
		if (DerivedData->VTData == nullptr)
		{
			DerivedData->VTData = new FVirtualTextureBuiltData();
		}

		FVirtualTextureDataBuilder Builder(*DerivedData->VTData, Compressor, ImageWrapper);
		Builder.Build(TextureData, CompositeTextureData, &BuildSettingsPerLayer[0], true);

		DerivedData->SizeX = DerivedData->VTData->Width;
		DerivedData->SizeY = DerivedData->VTData->Height;
		DerivedData->PixelFormat = DerivedData->VTData->LayerTypes[0];
		DerivedData->NumSlices = 1;

		// Store it in the cache.
		// @todo: This will remove the streaming bulk data, which we immediately reload below!
		// Should ideally avoid this redundant work, but it only happens when we actually have 
		// to build the texture, which should only ever be once.
		this->BytesCached = PutDerivedDataInCache(DerivedData, KeySuffix);

		if (DerivedData->VTData->Chunks.Num())
		{
			const bool bInlineMips = (CacheFlags & ETextureCacheFlags::InlineMips) != 0;
			bSucceeded = !bInlineMips || DerivedData->TryInlineMipData(BuildSettingsPerLayer[0].LODBiasWithCinematicMips);
		}
		else
		{
			UE_LOG(LogTexture, Warning, TEXT("Failed to build %s derived data for %s"), *BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *Texture.GetPathName());
		}
	}
	else if (TextureData.Blocks.Num() &&
		TextureData.Blocks[0].MipsPerLayer.Num() &&
		TextureData.Blocks[0].MipsPerLayer[0].Num())
	{
		// Only support single Block/Layer here (Blocks and Layers are intended for VT support)
		ensure(TextureData.Blocks.Num() == 1);
		ensure(TextureData.Layers.Num() == 1);

		check(DerivedData->Mips.Num() == 0);
		DerivedData->SizeX = 0;
		DerivedData->SizeY = 0;
		DerivedData->PixelFormat = PF_Unknown;
		DerivedData->VTData = nullptr;

		// Compress the texture.
		TArray<FCompressedImage2D> CompressedMips;
		if (Compressor->BuildTexture(TextureData.Blocks[0].MipsPerLayer[0],
			((bool)Texture.CompositeTexture && CompositeTextureData.Blocks.Num() && CompositeTextureData.Blocks[0].MipsPerLayer.Num()) ? CompositeTextureData.Blocks[0].MipsPerLayer[0] : TArray<FImage>(),
			BuildSettingsPerLayer[0],
			CompressedMips))
		{
			check(CompressedMips.Num());

			// Build the derived data.
			const int32 MipCount = CompressedMips.Num();
			for (int32 MipIndex = 0; MipIndex < MipCount; ++MipIndex)
			{
				const FCompressedImage2D& CompressedImage = CompressedMips[MipIndex];
				FTexture2DMipMap* NewMip = new FTexture2DMipMap();
				DerivedData->Mips.Add(NewMip);
				NewMip->SizeX = CompressedImage.SizeX;
				NewMip->SizeY = CompressedImage.SizeY;
				NewMip->SizeZ = CompressedImage.SizeZ;
				check(NewMip->SizeZ == 1 || BuildSettingsPerLayer[0].bVolume || BuildSettingsPerLayer[0].bTextureArray); // Only volume & arrays can have SizeZ != 1
				NewMip->BulkData.Lock(LOCK_READ_WRITE);
				check(CompressedImage.RawData.GetTypeSize() == 1);
				void* NewMipData = NewMip->BulkData.Realloc(CompressedImage.RawData.Num());
				FMemory::Memcpy(NewMipData, CompressedImage.RawData.GetData(), CompressedImage.RawData.Num());
				NewMip->BulkData.Unlock();

				if (MipIndex == 0)
				{
					DerivedData->SizeX = CompressedImage.SizeX;
					DerivedData->SizeY = CompressedImage.SizeY;
					DerivedData->PixelFormat = (EPixelFormat)CompressedImage.PixelFormat;
					DerivedData->NumSlices = BuildSettingsPerLayer[0].bCubemap ? 6 : (BuildSettingsPerLayer[0].bVolume || BuildSettingsPerLayer[0].bTextureArray) ? CompressedImage.SizeZ : 1;
				}
				else
				{
					check(CompressedImage.PixelFormat == DerivedData->PixelFormat);
				}
			}

			// Store it in the cache.
			// @todo: This will remove the streaming bulk data, which we immediately reload below!
			// Should ideally avoid this redundant work, but it only happens when we actually have 
			// to build the texture, which should only ever be once.
			this->BytesCached = PutDerivedDataInCache(DerivedData, KeySuffix);
		}

		if (DerivedData->Mips.Num())
		{
			const bool bInlineMips = (CacheFlags & ETextureCacheFlags::InlineMips) != 0;
			bSucceeded = !bInlineMips || DerivedData->TryInlineMipData(BuildSettingsPerLayer[0].LODBiasWithCinematicMips);
		}
		else
		{
			UE_LOG(LogTexture, Warning, TEXT("Failed to build %s derived data for %s"), *BuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *Texture.GetPathName());
		}
	}
}

FTextureCacheDerivedDataWorker::FTextureCacheDerivedDataWorker(
	ITextureCompressorModule* InCompressor,
	FTexturePlatformData* InDerivedData,
	UTexture* InTexture,
	const FTextureBuildSettings* InSettingsPerLayer,
	uint32 InCacheFlags
	)
	: Compressor(InCompressor)
	, ImageWrapper(nullptr)
	, DerivedData(InDerivedData)
	, Texture(*InTexture)
	, CacheFlags(InCacheFlags)
	, bSucceeded(false)
{
	check(DerivedData);

	BuildSettingsPerLayer.SetNum(InTexture->Source.GetNumLayers());
	for (int32 LayerIndex = 0; LayerIndex < BuildSettingsPerLayer.Num(); ++LayerIndex)
	{
		BuildSettingsPerLayer[LayerIndex] = InSettingsPerLayer[LayerIndex];
	}

	// At this point, the texture *MUST* have a valid GUID.
	if (!Texture.Source.GetId().IsValid())
	{
		UE_LOG(LogTexture, Warning, TEXT("Building texture with an invalid GUID: %s"), *Texture.GetPathName());
		Texture.Source.ForceGenerateGuid();
	}
	check(Texture.Source.GetId().IsValid());

	// Dump any existing mips.
	DerivedData->Mips.Empty();
	if (DerivedData->VTData)
	{
		delete DerivedData->VTData;
		DerivedData->VTData = nullptr;
	}
	UTexture::GetPixelFormatEnum();
	GetTextureDerivedDataKeySuffix(Texture, InSettingsPerLayer, KeySuffix);
		
	const bool bAllowAsyncBuild = (CacheFlags & ETextureCacheFlags::AllowAsyncBuild) != 0;
	const bool bAllowAsyncLoading = (CacheFlags & ETextureCacheFlags::AllowAsyncLoading) != 0;
	const bool bForVirtualTextureStreamingBuild = (CacheFlags & ETextureCacheFlags::ForVirtualTextureStreamingBuild) != 0;

	// FVirtualTextureDataBuilder always wants to load ImageWrapper module
	// This is not strictly necessary, used only for debug output, but seems simpler to just always load this here, doesn't seem like it should be too expensive
	if (bAllowAsyncLoading || bForVirtualTextureStreamingBuild)
	{
		ImageWrapper = &FModuleManager::LoadModuleChecked<IImageWrapperModule>( FName("ImageWrapper") );
	}

	TextureData.Init(Texture, BuildSettingsPerLayer.GetData(), bAllowAsyncLoading);
	if (Texture.CompositeTexture && Texture.CompositeTextureMode != CTM_Disabled)
	{
		const FIntPoint CompositeSourceSize = Texture.CompositeTexture->Source.GetLogicalSize();
		if (FMath::IsPowerOfTwo(CompositeSourceSize.X) && FMath::IsPowerOfTwo(CompositeSourceSize.Y))
		{
			CompositeTextureData.Init(*Texture.CompositeTexture, BuildSettingsPerLayer.GetData(), bAllowAsyncLoading);
		}
	}

	// If the bulkdata is loaded and async build is allowed, get the source mips now (safe) to allow building the DDC if required.
	// If the bulkdata is not loaded, the DDC will be built in Finalize() unless async loading is enabled (which won't allow reuse of the source for later use).
	if (bAllowAsyncBuild)
	{
		if (TextureData.IsValid() && Texture.Source.IsBulkDataLoaded())
		{
			TextureData.GetSourceMips(Texture.Source, ImageWrapper);
		}
		if (CompositeTextureData.IsValid() && Texture.CompositeTexture && Texture.CompositeTexture->Source.IsBulkDataLoaded())
		{
			CompositeTextureData.GetSourceMips(Texture.CompositeTexture->Source, ImageWrapper);
		}
	}
}

void FTextureCacheDerivedDataWorker::DoWork()
{
	const bool bForceRebuild = (CacheFlags & ETextureCacheFlags::ForceRebuild) != 0;
	const bool bAllowAsyncBuild = (CacheFlags & ETextureCacheFlags::AllowAsyncBuild) != 0;
	const bool bAllowAsyncLoading = (CacheFlags & ETextureCacheFlags::AllowAsyncLoading) != 0;
	const bool bForVirtualTextureStreamingBuild = (CacheFlags & ETextureCacheFlags::ForVirtualTextureStreamingBuild) != 0;

	TArray<uint8> RawDerivedData;

	if (!bForceRebuild && GetDerivedDataCacheRef().GetSynchronous(*DerivedData->DerivedDataKey, RawDerivedData))
	{
		const bool bInlineMips = (CacheFlags & ETextureCacheFlags::InlineMips) != 0;
		const bool bForDDC = (CacheFlags & ETextureCacheFlags::ForDDCBuild) != 0;

		BytesCached = RawDerivedData.Num();
		FMemoryReader Ar(RawDerivedData, /*bIsPersistent=*/ true);
		DerivedData->Serialize(Ar, NULL);
		bSucceeded = true;
		// Load any streaming (not inline) mips that are necessary for our platform.
		if (bForDDC)
		{
			bSucceeded = DerivedData->TryLoadMips(0,NULL);
		}
		else if (bInlineMips)
		{
			bSucceeded = DerivedData->TryInlineMipData(BuildSettingsPerLayer[0].LODBiasWithCinematicMips);
		}
		else
		{
			if (bForVirtualTextureStreamingBuild)
			{
				bSucceeded =	DerivedData->VTData != nullptr &&
								DerivedData->VTData->IsInitialized() &&
								DerivedData->AreDerivedVTChunksAvailable();
			}
			else
			{
				bSucceeded = DerivedData->AreDerivedMipsAvailable();
			}
		}
		bLoadedFromDDC = true;

		// Reset everything derived data so that we can do a clean load from the source data
		if (!bSucceeded)
		{
			DerivedData->Mips.Empty();
			if (DerivedData->VTData)
			{
				delete DerivedData->VTData;
				DerivedData->VTData = nullptr;
			}
		}
	}
	
	if (!bSucceeded && bAllowAsyncBuild)
	{
		if (bAllowAsyncLoading)
		{
			TextureData.GetAsyncSourceMips(ImageWrapper);
			CompositeTextureData.GetAsyncSourceMips(ImageWrapper);
		}

		if (TextureData.Blocks[0].MipsPerLayer[0].Num() && (!CompositeTextureData.IsValid() || CompositeTextureData.Blocks[0].MipsPerLayer[0].Num()))
		{
			BuildTexture();
			bSucceeded = true;
		}
		else
		{
			bSucceeded = false;
		}
	}

	if (bSucceeded)
	{
		TextureData.ReleaseMemory();
		CompositeTextureData.ReleaseMemory();
	}
}

void FTextureCacheDerivedDataWorker::Finalize()
{
	check(IsInGameThread());
	// if we couldn't get from the DDC or didn't build synchronously, then we have to build now. 
	// This is a super edge case that should rarely happen.
	if (!bSucceeded)
	{
		TextureData.GetSourceMips(Texture.Source, ImageWrapper);
		if (Texture.CompositeTexture)
		{
			CompositeTextureData.GetSourceMips(Texture.CompositeTexture->Source, ImageWrapper);
		}
		BuildTexture();
	}
		
	if (BuildSettingsPerLayer[0].bVirtualStreamable) // Texture.VirtualTextureStreaming is more a hint that might be overruled by the buildsettings
	{
		check((DerivedData->VTData != nullptr) == Texture.VirtualTextureStreaming); 
	}
}

#endif // WITH_EDITOR

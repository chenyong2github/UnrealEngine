// Copyright Epic Games, Inc. All Rights Reserved.

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

#include "Algo/Accumulate.h"
#include "DerivedDataBuild.h"
#include "DerivedDataBuildAction.h"
#include "DerivedDataBuildInputResolver.h"
#include "DerivedDataBuildInputs.h"
#include "DerivedDataBuildOutput.h"
#include "DerivedDataBuildSession.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataSharedString.h"
#include "DerivedDataValue.h"
#include "Engine/TextureCube.h"
#include "Engine/VolumeTexture.h"
#include "GenericPlatform/GenericPlatformMath.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITextureFormat.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/FeedbackContext.h"
#include "Misc/Optional.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/BulkDataRegistry.h"
#include "TextureDerivedDataBuildUtils.h"
#include "VT/VirtualTextureChunkDDCCache.h"
#include "VT/VirtualTextureDataBuilder.h"
#include "TextureEncodingSettings.h"
#include <atomic>

static TAutoConsoleVariable<int32> CVarVTValidateCompressionOnLoad(
	TEXT("r.VT.ValidateCompressionOnLoad"),
	0,
	TEXT("Validates that VT data contains no compression errors when loading from DDC")
	TEXT("This is slow, but allows debugging corrupt VT data (and allows recovering from bad DDC)")
);

static TAutoConsoleVariable<int32> CVarVTValidateCompressionOnSave(
	TEXT("r.VT.ValidateCompressionOnSave"),
	0,
	TEXT("Validates that VT data contains no compression errors before saving to DDC")
	TEXT("This is slow, but allows debugging corrupt VT data")
);

void GetTextureDerivedDataKeyFromSuffix(const FString& KeySuffix, FString& OutKey);

class FTextureStatusMessageContext : public FScopedSlowTask
{
public:
	explicit FTextureStatusMessageContext(const FText& InMessage)
		: FScopedSlowTask(0, InMessage, IsInGameThread())
	{
		UE_LOG(LogTexture,Display,TEXT("%s"),*InMessage.ToString());
	}
};

static FText ComposeTextureBuildText(const FString& TexturePathName, int32 SizeX, int32 SizeY, int32 NumBlocks, int32 NumLayers, const FTextureBuildSettings& BuildSettings, ETextureEncodeSpeed InEncodeSpeed, int64 RequiredMemoryEstimate, bool bIsVT)
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("TextureName"), FText::FromString(TexturePathName));
	Args.Add(TEXT("TextureFormatName"), FText::FromString(BuildSettings.TextureFormatName.GetPlainNameString()));
	Args.Add(TEXT("IsVT"), FText::FromString( FString( bIsVT ? TEXT(" VT") : TEXT("") ) ) );
	Args.Add(TEXT("TextureResolutionX"), FText::FromString(FString::FromInt(SizeX)));
	Args.Add(TEXT("TextureResolutionY"), FText::FromString(FString::FromInt(SizeY)));
	Args.Add(TEXT("NumBlocks"), FText::FromString(FString::FromInt(NumBlocks)));
	Args.Add(TEXT("NumLayers"), FText::FromString(FString::FromInt(NumLayers)));
	Args.Add(TEXT("EstimatedMemory"), FText::FromString(FString::SanitizeFloat(double(RequiredMemoryEstimate) / (1024.0*1024.0), 3)));
	
	const TCHAR* SpeedText = TEXT("");
	switch (InEncodeSpeed)
	{
	case ETextureEncodeSpeed::Final: SpeedText = TEXT("Final"); break;
	case ETextureEncodeSpeed::Fast: SpeedText = TEXT("Fast"); break;
	case ETextureEncodeSpeed::FinalIfAvailable: SpeedText = TEXT("FinalIfAvailable"); break;
	}

	Args.Add(TEXT("Speed"), FText::FromString(FString(SpeedText)));

	return FText::Format(
		NSLOCTEXT("Engine", "BuildTextureStatus", "Building textures: {TextureName} ({TextureFormatName}{IsVT}, {TextureResolutionX}X{TextureResolutionY} X{NumBlocks}X{NumLayers}) (Required Memory Estimate: {EstimatedMemory} MB), EncodeSpeed: {Speed}"), 
		Args
	);
}

static FText ComposeTextureBuildText(const FString& TexturePathName, const FTextureSourceData& TextureData, const FTextureBuildSettings& BuildSettings, ETextureEncodeSpeed InEncodeSpeed, int64 RequiredMemoryEstimate, bool bIsVT)
{
	return ComposeTextureBuildText(TexturePathName, TextureData.Blocks[0].MipsPerLayer[0][0].SizeX, TextureData.Blocks[0].MipsPerLayer[0][0].SizeY, TextureData.Blocks.Num(), TextureData.Layers.Num(), BuildSettings, InEncodeSpeed, RequiredMemoryEstimate, bIsVT);
}

static FText ComposeTextureBuildText(const UTexture& Texture, const FTextureBuildSettings& BuildSettings, ETextureEncodeSpeed InEncodeSpeed, int64 RequiredMemoryEstimate, bool bIsVT)
{
	return ComposeTextureBuildText(Texture.GetPathName(), Texture.Source.GetSizeX(), Texture.Source.GetSizeY(), Texture.Source.GetNumBlocks(), Texture.Source.GetNumLayers(), BuildSettings, InEncodeSpeed, RequiredMemoryEstimate, bIsVT);
}

static bool ValidateTexture2DPlatformData(const FTexturePlatformData& TextureData, const UTexture2D& Texture, bool bFromDDC)
{
	// Temporarily disable as the size check reports false negatives on some platforms
#if 0
	bool bValid = true;
	for (int32 MipIndex = 0; MipIndex < TextureData.Mips.Num(); ++MipIndex)
	{
		const FTexture2DMipMap& MipMap = TextureData.Mips[MipIndex];
		const int64 BulkDataSize = MipMap.BulkData.GetBulkDataSize();
		if (BulkDataSize > 0)
		{
			const int64 ExpectedMipSize = CalcTextureMipMapSize(TextureData.SizeX, TextureData.SizeY, TextureData.PixelFormat, MipIndex);
			if (BulkDataSize != ExpectedMipSize)
			{
				//UE_LOG(LogTexture,Warning,TEXT("Invalid mip data. Texture will be rebuilt. MipIndex %d [%dx%d], Expected size %lld, BulkData size %lld, PixelFormat %s, LoadedFromDDC %d, Texture %s"), 
				//	MipIndex, 
				//	MipMap.SizeX, 
				//	MipMap.SizeY, 
				//	ExpectedMipSize, 
				//	BulkDataSize, 
				//	GPixelFormats[TextureData.PixelFormat].Name, 
				//	bFromDDC ? 1 : 0,
				//	*Texture.GetFullName());
				
				bValid = false;
			}
		}
	}

	return bValid;
#else
	return true;
#endif
}

void FTextureSourceData::Init(UTexture& InTexture, TextureMipGenSettings InMipGenSettings, bool bInCubeMap, bool bInTextureArray, bool bInVolumeTexture, bool bAllowAsyncLoading)
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
		case TSF_G16:		LayerData->ImageFormat = ERawImageFormat::G16;		break;
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

			if (InMipGenSettings != TMGS_LeaveExistingMips)
			{
				BlockData->NumMips = 1;
			}

			if (!bInCubeMap && !bInTextureArray && !bInVolumeTexture)
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
		AsyncSource = InTexture.Source.CopyTornOff(); // This copies information required to make a safe IO load async.
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

		const FTextureSource::FMipData ScopedMipData = Source.GetMipData(InImageWrapper);

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

						if (!ScopedMipData.GetMipData(SourceMip->RawData, BlockIndex, LayerIndex, MipIndex))
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
	if (bValid && !Blocks[0].MipsPerLayer[0].Num() && AsyncSource.HasPayloadData())
	{
		GetSourceMips(AsyncSource, InImageWrapper);
	}
}

namespace UE::TextureDerivedData
{

using namespace UE::DerivedData;

class FTextureBuildInputResolver final : public IBuildInputResolver
{
public:
	explicit FTextureBuildInputResolver(UTexture& InTexture)
		: Texture(InTexture)
	{
	}

	const FCompressedBuffer& FindSource(FCompressedBuffer& Buffer, FTextureSource& Source, const FGuid& BulkDataId)
	{
		if (Source.GetPersistentId() != BulkDataId)
		{
			return FCompressedBuffer::Null;
		}
		if (!Buffer)
		{
			Source.OperateOnLoadedBulkData([&Buffer](const FSharedBuffer& BulkDataBuffer)
			{
				Buffer = FCompressedBuffer::Compress(BulkDataBuffer);
			});
		}
		return Buffer;
	}

	void ResolveInputMeta(
		const FBuildDefinition& Definition,
		IRequestOwner& Owner,
		FOnBuildInputMetaResolved&& OnResolved) final
	{
		EStatus Status = EStatus::Ok;
		TArray<FBuildInputMetaByKey> Inputs;
		Definition.IterateInputBulkData([this, &Status, &Inputs](FUtf8StringView Key, const FGuid& BulkDataId)
		{
			const FCompressedBuffer& Buffer = Key == UTF8TEXTVIEW("Source")
				? FindSource(SourceBuffer, Texture.Source, BulkDataId)
				: FindSource(CompositeSourceBuffer, Texture.CompositeTexture->Source, BulkDataId);
			if (Buffer)
			{
				Inputs.Add({Key, Buffer.GetRawHash(), Buffer.GetRawSize()});
			}
			else
			{
				Status = EStatus::Error;
			}
		});
		OnResolved({Inputs, Status});
	}

	void ResolveInputData(
		const FBuildDefinition& Definition,
		IRequestOwner& Owner,
		FOnBuildInputDataResolved&& OnResolved,
		FBuildInputFilter&& Filter) final
	{
		EStatus Status = EStatus::Ok;
		TArray<FBuildInputDataByKey> Inputs;
		Definition.IterateInputBulkData([this, &Filter, &Status, &Inputs](FUtf8StringView Key, const FGuid& BulkDataId)
		{
			if (!Filter || Filter(Key))
			{
				const FCompressedBuffer& Buffer = Key == UTF8TEXTVIEW("Source")
					? FindSource(SourceBuffer, Texture.Source, BulkDataId)
					: FindSource(CompositeSourceBuffer, Texture.CompositeTexture->Source, BulkDataId);
				if (Buffer)
				{
					Inputs.Add({Key, Buffer});
				}
				else
				{
					Status = EStatus::Error;
				}
			}
		});
		OnResolved({Inputs, Status});
	}

private:
	UTexture& Texture;
	FCompressedBuffer SourceBuffer;
	FCompressedBuffer CompositeSourceBuffer;
};

} // UE::TextureDerivedData

// Synchronous DDC1 texture build function
void FTextureCacheDerivedDataWorker::BuildTexture(TArray<FTextureBuildSettings>& InBuildSettingsPerLayer, bool bReplaceExistingDDC)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCacheDerivedDataWorker::BuildTexture);

	const bool bHasValidMip0 = TextureData.Blocks.Num() && TextureData.Blocks[0].MipsPerLayer.Num() && TextureData.Blocks[0].MipsPerLayer[0].Num();
	const bool bForVirtualTextureStreamingBuild = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::ForVirtualTextureStreamingBuild);

	if (!ensure(Compressor))
	{
		UE_LOG(LogTexture, Warning, TEXT("Missing Compressor required to build texture %s"), *TexturePathName);
		return;
	}

	if (!bHasValidMip0)
	{
		return;
	}

	FTextureStatusMessageContext StatusMessage(
		ComposeTextureBuildText(TexturePathName, TextureData, InBuildSettingsPerLayer[0], (ETextureEncodeSpeed)InBuildSettingsPerLayer[0].RepresentsEncodeSpeedNoSend, RequiredMemoryEstimate, bForVirtualTextureStreamingBuild)
		);

	if (bForVirtualTextureStreamingBuild)
	{
		if (DerivedData->VTData == nullptr)
		{
			DerivedData->VTData = new FVirtualTextureBuiltData();
		}

		FVirtualTextureDataBuilder Builder(*DerivedData->VTData, TexturePathName, Compressor, ImageWrapper);
		Builder.Build(TextureData, CompositeTextureData, &InBuildSettingsPerLayer[0], true);

		DerivedData->SizeX = DerivedData->VTData->Width;
		DerivedData->SizeY = DerivedData->VTData->Height;
		DerivedData->PixelFormat = DerivedData->VTData->LayerTypes[0];
		DerivedData->SetNumSlices(1);

		bool bCompressionValid = true;
		if (CVarVTValidateCompressionOnSave.GetValueOnAnyThread())
		{
			bCompressionValid = DerivedData->VTData->ValidateData(TexturePathName, true);
		}

		if (ensureMsgf(bCompressionValid, TEXT("Corrupt Virtual Texture compression for %s, can't store to DDC"), *TexturePathName))
		{
			// Store it in the cache.
			// @todo: This will remove the streaming bulk data, which we immediately reload below!
			// Should ideally avoid this redundant work, but it only happens when we actually have 
			// to build the texture, which should only ever be once.
			this->BytesCached = PutDerivedDataInCache(DerivedData, KeySuffix, TexturePathName, InBuildSettingsPerLayer[0].bCubemap || InBuildSettingsPerLayer[0].bVolume || InBuildSettingsPerLayer[0].bTextureArray, bReplaceExistingDDC);

			if (DerivedData->VTData->Chunks.Num())
			{
				const bool bInlineMips = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::InlineMips);
				bSucceeded = !bInlineMips || DerivedData->TryInlineMipData(InBuildSettingsPerLayer[0].LODBiasWithCinematicMips, TexturePathName);
				if (!bSucceeded)
				{
					UE_LOG(LogTexture, Display, TEXT("Failed to put and then read back mipmap data from DDC for %s"), *TexturePathName);
				}
			}
			else
			{
				UE_LOG(LogTexture, Warning, TEXT("Failed to build %s derived data for %s"), *InBuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *TexturePathName);
			}
		}
	}
	else
	{
		// Only support single Block/Layer here (Blocks and Layers are intended for VT support)
		if (TextureData.Blocks.Num() > 1)
		{
			// This can happen if user attempts to import a UDIM without VT enabled
			UE_LOG(LogTexture, Log, TEXT("Texture %s was imported as UDIM with %d blocks but VirtualTexturing is not enabled, only the first block will be available"),
				*TexturePathName, TextureData.Blocks.Num());
		}
		if (TextureData.Layers.Num() > 1)
		{
			// This can happen if user attempts to use lightmaps or other layered VT without VT enabled
			UE_LOG(LogTexture, Log, TEXT("Texture %s has %d layers but VirtualTexturing is not enabled, only the first layer will be available"),
				*TexturePathName, TextureData.Layers.Num());
		}

		check(DerivedData->Mips.Num() == 0);
		DerivedData->SizeX = 0;
		DerivedData->SizeY = 0;
		DerivedData->PixelFormat = PF_Unknown;
		DerivedData->SetIsCubemap(false);
		DerivedData->VTData = nullptr;

		FOptTexturePlatformData OptData;

		// Compress the texture by calling texture compressor directly.
		TArray<FCompressedImage2D> CompressedMips;
		if (Compressor->BuildTexture(TextureData.Blocks[0].MipsPerLayer[0],
			((bool)Texture.CompositeTexture && CompositeTextureData.Blocks.Num() && CompositeTextureData.Blocks[0].MipsPerLayer.Num()) ? CompositeTextureData.Blocks[0].MipsPerLayer[0] : TArray<FImage>(),
			InBuildSettingsPerLayer[0],
			TexturePathName,
			CompressedMips,
			OptData.NumMipsInTail,
			OptData.ExtData))
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
				NewMip->FileRegionType = FFileRegion::SelectType(EPixelFormat(CompressedImage.PixelFormat));
				check(NewMip->SizeZ == 1 || InBuildSettingsPerLayer[0].bVolume || InBuildSettingsPerLayer[0].bTextureArray); // Only volume & arrays can have SizeZ != 1
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
					if (InBuildSettingsPerLayer[0].bVolume || InBuildSettingsPerLayer[0].bTextureArray)
					{
						DerivedData->SetNumSlices(CompressedImage.SizeZ);
					}
					else if (InBuildSettingsPerLayer[0].bCubemap)
					{
						DerivedData->SetNumSlices(6);
					}
					else
					{
						DerivedData->SetNumSlices(1);
					}
					DerivedData->SetIsCubemap(InBuildSettingsPerLayer[0].bCubemap);
				}
				else
				{
					check(CompressedImage.PixelFormat == DerivedData->PixelFormat);
				}
			}

			DerivedData->SetOptData(OptData);
				
			// Store it in the cache.
			// @todo: This will remove the streaming bulk data, which we immediately reload below!
			// Should ideally avoid this redundant work, but it only happens when we actually have 
			// to build the texture, which should only ever be once.
			this->BytesCached = PutDerivedDataInCache(DerivedData, KeySuffix, TexturePathName, InBuildSettingsPerLayer[0].bCubemap || (InBuildSettingsPerLayer[0].bVolume && !GSupportsVolumeTextureStreaming) || (InBuildSettingsPerLayer[0].bTextureArray && !GSupportsTexture2DArrayStreaming), bReplaceExistingDDC);
		}

		if (DerivedData->Mips.Num())
		{
			const bool bInlineMips = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::InlineMips);
			bSucceeded = !bInlineMips || DerivedData->TryInlineMipData(InBuildSettingsPerLayer[0].LODBiasWithCinematicMips, TexturePathName);
			if (!bSucceeded)
			{
				UE_LOG(LogTexture, Display, TEXT("Failed to put and then read back mipmap data from DDC for %s"), *TexturePathName);
			}
		}
		else
		{
			UE_LOG(LogTexture, Warning, TEXT("Failed to build %s derived data for %s"), *InBuildSettingsPerLayer[0].TextureFormatName.GetPlainNameString(), *TexturePathName);
		}
	}
}

FTextureCacheDerivedDataWorker::FTextureCacheDerivedDataWorker(
	ITextureCompressorModule* InCompressor,
	FTexturePlatformData* InDerivedData,
	UTexture* InTexture,
	const FTextureBuildSettings* InSettingsPerLayerFetchFirst,
	const FTextureBuildSettings* InSettingsPerLayerFetchOrBuild,
	const FTexturePlatformData::FTextureEncodeResultMetadata* InFetchFirstMetadata, // can be nullptr if not needed
	const FTexturePlatformData::FTextureEncodeResultMetadata* InFetchOrBuildMetadata, // can be nullptr if not needed
	ETextureCacheFlags InCacheFlags
	)
	: Compressor(InCompressor)
	, ImageWrapper(nullptr)
	, DerivedData(InDerivedData)
	, Texture(*InTexture)
	, TexturePathName(InTexture->GetPathName())
	, CacheFlags(InCacheFlags)
	, RequiredMemoryEstimate(InTexture->GetBuildRequiredMemory())
	, bSucceeded(false)
{
	check(DerivedData);

	if (InSettingsPerLayerFetchFirst)
	{
		BuildSettingsPerLayerFetchFirst.SetNum(InTexture->Source.GetNumLayers());
		for (int32 LayerIndex = 0; LayerIndex < BuildSettingsPerLayerFetchFirst.Num(); ++LayerIndex)
		{
			BuildSettingsPerLayerFetchFirst[LayerIndex] = InSettingsPerLayerFetchFirst[LayerIndex];
		}
		if (InFetchFirstMetadata)
		{
			FetchFirstMetadata = *InFetchFirstMetadata;
		}
	}
	
	BuildSettingsPerLayerFetchOrBuild.SetNum(InTexture->Source.GetNumLayers());
	for (int32 LayerIndex = 0; LayerIndex < BuildSettingsPerLayerFetchOrBuild.Num(); ++LayerIndex)
	{
		BuildSettingsPerLayerFetchOrBuild[LayerIndex] = InSettingsPerLayerFetchOrBuild[LayerIndex];
	}
	if (InFetchOrBuildMetadata)
	{
		FetchOrBuildMetadata = *InFetchOrBuildMetadata;
	}

	// FetchOrBuildDerivedDataKey needs to be assigned on the create thread.
	{
		FString LocalKeySuffix;
		GetTextureDerivedDataKeySuffix(Texture, BuildSettingsPerLayerFetchOrBuild.GetData(), LocalKeySuffix);
		FString DDK;
		GetTextureDerivedDataKeyFromSuffix(LocalKeySuffix, DDK);
		InDerivedData->FetchOrBuildDerivedDataKey.Emplace<FString>(DDK);
	}

	// At this point, the texture *MUST* have a valid GUID.
	if (!Texture.Source.GetId().IsValid())
	{
		UE_LOG(LogTexture, Warning, TEXT("Building texture with an invalid GUID: %s"), *TexturePathName);
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
		
	const bool bAllowAsyncBuild = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::AllowAsyncBuild);
	const bool bAllowAsyncLoading = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::AllowAsyncLoading);
	const bool bForVirtualTextureStreamingBuild = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::ForVirtualTextureStreamingBuild);

	// FVirtualTextureDataBuilder always wants to load ImageWrapper module
	// This is not strictly necessary, used only for debug output, but seems simpler to just always load this here, doesn't seem like it should be too expensive
	if (bAllowAsyncLoading || bForVirtualTextureStreamingBuild)
	{
		ImageWrapper = &FModuleManager::LoadModuleChecked<IImageWrapperModule>( FName("ImageWrapper") );
	}

	// All of these settings are fixed across build settings and are derived directly from the texture.
	// So we can just use layer 0 of whatever we have.
	TextureData.Init(Texture, (TextureMipGenSettings)BuildSettingsPerLayerFetchOrBuild[0].MipGenSettings, BuildSettingsPerLayerFetchOrBuild[0].bCubemap, BuildSettingsPerLayerFetchOrBuild[0].bTextureArray, BuildSettingsPerLayerFetchOrBuild[0].bVolume, bAllowAsyncLoading);
	if (Texture.CompositeTexture && Texture.CompositeTextureMode != CTM_Disabled)
	{
		bool bMatchingBlocks = Texture.CompositeTexture->Source.GetNumBlocks() == Texture.Source.GetNumBlocks();
		bool bMatchingAspectRatio = true;
		bool bOnlyPowerOfTwoSize = true;
		if (bMatchingBlocks)
		{
			for (int32 BlockIdx = 0; BlockIdx < Texture.Source.GetNumBlocks(); ++BlockIdx)
			{
				FTextureSourceBlock TextureBlock;
				Texture.Source.GetBlock(BlockIdx, TextureBlock);
				FTextureSourceBlock CompositeTextureBlock;
				Texture.CompositeTexture->Source.GetBlock(BlockIdx, CompositeTextureBlock);

				bMatchingBlocks = bMatchingBlocks && TextureBlock.BlockX == CompositeTextureBlock.BlockX && TextureBlock.BlockY == CompositeTextureBlock.BlockY;
				bMatchingAspectRatio = bMatchingAspectRatio && TextureBlock.SizeX * CompositeTextureBlock.SizeY == TextureBlock.SizeY * CompositeTextureBlock.SizeX;
				bOnlyPowerOfTwoSize = bOnlyPowerOfTwoSize && FMath::IsPowerOfTwo(TextureBlock.SizeX) && FMath::IsPowerOfTwo(TextureBlock.SizeY);
			}
		}

		if (!bMatchingBlocks)
		{
			// Only report the warning for textures with a single block
			// In the future, we should support composite textures if matching blocks are in a different order
			// Once that's working, then this warning should be reported in all cases
			if (Texture.Source.GetNumBlocks() == 1)
			{
				UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Composite texture resolution/UDIMs do not match. Composite texture will be ignored"), *TexturePathName);
			}
		}
		else if (!bOnlyPowerOfTwoSize)
		{
			UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Some blocks (UDIMs) have a non power of two size. Composite texture will be ignored"), *TexturePathName);
		}
		else if (!bMatchingAspectRatio)
		{
			UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Some blocks (UDIMs) have mismatched aspect ratio. Composite texture will be ignored"), *TexturePathName);
		}

		if (bMatchingBlocks && bMatchingAspectRatio && bOnlyPowerOfTwoSize)
		{
			// These are derived from the texture, and the composite texture must match.
			CompositeTextureData.Init(*Texture.CompositeTexture, (TextureMipGenSettings)BuildSettingsPerLayerFetchOrBuild[0].MipGenSettings, BuildSettingsPerLayerFetchOrBuild[0].bCubemap, BuildSettingsPerLayerFetchOrBuild[0].bTextureArray, BuildSettingsPerLayerFetchOrBuild[0].bVolume, bAllowAsyncLoading);
		}
	}
}

// DDC1 primary fetch/build work function
void FTextureCacheDerivedDataWorker::DoWork()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FTextureCacheDerivedDataWorker::DoWork);

	const bool bForceRebuild = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::ForceRebuild);
	const bool bAllowAsyncBuild = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::AllowAsyncBuild);
	const bool bAllowAsyncLoading = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::AllowAsyncLoading);
	const bool bForVirtualTextureStreamingBuild = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::ForVirtualTextureStreamingBuild);
	const bool bValidateVirtualTextureCompression = CVarVTValidateCompressionOnLoad.GetValueOnAnyThread() != 0;
	bool bInvalidVirtualTextureCompression = false;

	TArray64<uint8> RawDerivedData;

	// Can't have a fetch first if we are rebuilding
	if (bForceRebuild)
	{
		BuildSettingsPerLayerFetchFirst.Empty();
	}

	FString LocalDerivedDataKeySuffix;
	FString LocalDerivedDataKey;
	
	FString FetchOrBuildKeySuffix;
	GetTextureDerivedDataKeySuffix(Texture, BuildSettingsPerLayerFetchOrBuild.GetData(), FetchOrBuildKeySuffix);

	bool bUsedFetchFirst = false;
	if (BuildSettingsPerLayerFetchFirst.Num())
	{
		FString FetchFirstKeySuffix;
		GetTextureDerivedDataKeySuffix(Texture, BuildSettingsPerLayerFetchFirst.GetData(), FetchFirstKeySuffix);

		// If the suffixes are the same, then use fetchorbuild to avoid a get()
		if (FetchFirstKeySuffix != FetchOrBuildKeySuffix)
		{
			FString FetchFirstKey;
			GetTextureDerivedDataKeyFromSuffix(FetchFirstKeySuffix, FetchFirstKey);
		
			bLoadedFromDDC = GetDerivedDataCacheRef().GetSynchronous(*FetchFirstKey, RawDerivedData, TexturePathName);
			if (bLoadedFromDDC)
			{
				bUsedFetchFirst = true;
				LocalDerivedDataKey = MoveTemp(FetchFirstKey);
				LocalDerivedDataKeySuffix = MoveTemp(FetchFirstKeySuffix);
			}
		}
	}

	if (bLoadedFromDDC == false)
	{
		// Didn't get the initial fetch, so we're using fetch/build.
		LocalDerivedDataKeySuffix = MoveTemp(FetchOrBuildKeySuffix);
		GetTextureDerivedDataKeyFromSuffix(LocalDerivedDataKeySuffix, LocalDerivedDataKey);
		bLoadedFromDDC = GetDerivedDataCacheRef().GetSynchronous(*LocalDerivedDataKey, RawDerivedData, TexturePathName);
	}

	KeySuffix = LocalDerivedDataKeySuffix;
	DerivedData->DerivedDataKey.Emplace<FString>(LocalDerivedDataKey);
	TArray<FTextureBuildSettings>& ActiveBuildSettingsPerLayer = bUsedFetchFirst ? BuildSettingsPerLayerFetchFirst : BuildSettingsPerLayerFetchOrBuild;
	DerivedData->ResultMetadata = bUsedFetchFirst ? FetchFirstMetadata : FetchOrBuildMetadata;

	if (bLoadedFromDDC)
	{
		const bool bInlineMips = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::InlineMips);
		const bool bForDDC = EnumHasAnyFlags(CacheFlags, ETextureCacheFlags::ForDDCBuild);

		BytesCached = RawDerivedData.Num();
		FMemoryReaderView Ar(MakeMemoryView(RawDerivedData), /*bIsPersistent=*/ true);
		DerivedData->Serialize(Ar, NULL);
		bSucceeded = true;
		// Load any streaming (not inline) mips that are necessary for our platform.
		if (bForDDC)
		{
			bSucceeded = DerivedData->TryLoadMips(0, nullptr, TexturePathName);

			if (bForVirtualTextureStreamingBuild)
			{
				if (DerivedData->VTData != nullptr &&
					DerivedData->VTData->IsInitialized())
				{
					TArray<FString, TInlineAllocator<16>> ChunkKeys;
					for (const FVirtualTextureDataChunk& Chunk : DerivedData->VTData->Chunks)
					{
						if (!Chunk.DerivedDataKey.IsEmpty())
						{
							ChunkKeys.Add(Chunk.DerivedDataKey);
						}
					}
					GetDerivedDataCacheRef().TryToPrefetch(ChunkKeys, TexturePathName);
				}
			}

			if (!bSucceeded)
			{
				UE_LOG(LogTexture, Display, TEXT("Texture %s is missing mips. The texture will be rebuilt."), *TexturePathName);
			}
		}
		else if (bInlineMips)
		{
			bSucceeded = DerivedData->TryInlineMipData(ActiveBuildSettingsPerLayer[0].LODBiasWithCinematicMips, TexturePathName);

			if (!bSucceeded)
			{
				UE_LOG(LogTexture, Display, TEXT("Texture %s is missing inline mips. The texture will be rebuilt."), *TexturePathName);
			}
		}
		else
		{
			if (bForVirtualTextureStreamingBuild)
			{
				bSucceeded =	DerivedData->VTData != nullptr &&
								DerivedData->VTData->IsInitialized() &&
								DerivedData->AreDerivedVTChunksAvailable(TexturePathName);

				if (!bSucceeded)
				{
					UE_LOG(LogTexture, Display, TEXT("Texture %s is missing VT Chunks. The texture will be rebuilt."), *TexturePathName);
				}
			}
			else
			{
				bSucceeded = DerivedData->AreDerivedMipsAvailable(TexturePathName);
				if (!bSucceeded)
				{
					UE_LOG(LogTexture, Display, TEXT("Texture %s is missing derived mips. The texture will be rebuilt."), *TexturePathName);
				}

				if (bSucceeded && ActiveBuildSettingsPerLayer.Num() > 0)
				{
					// Code inspired by the texture compressor module as a hot fix for the bad data that might have been push into the ddc in 4.23 or 4.24 
					const bool bLongLatCubemap = DerivedData->IsCubemap() && DerivedData->GetNumSlices() == 1;
					int32 MaximumNumberOfMipMaps = TNumericLimits<int32>::Max();
					if (bLongLatCubemap)
					{
						MaximumNumberOfMipMaps = FMath::CeilLogTwo(FMath::Clamp<uint32>(uint32(1 << FMath::FloorLog2(DerivedData->SizeX / 2)), uint32(32), ActiveBuildSettingsPerLayer[0].MaxTextureResolution)) + 1;
					}
					else
					{
						MaximumNumberOfMipMaps = FMath::CeilLogTwo(FMath::Max3(DerivedData->SizeX, DerivedData->SizeY, ActiveBuildSettingsPerLayer[0].bVolume ? DerivedData->GetNumSlices() : 1)) + 1;
					}

					bSucceeded = DerivedData->Mips.Num() <= MaximumNumberOfMipMaps;

					if (!bSucceeded)
					{
						UE_LOG(LogTexture, Warning, TEXT("The data retrieved from the derived data cache for the texture %s was invalid. ")
							TEXT("The cached data has %d mips when a maximum of %d are expected. The texture will be rebuilt."),
							*TexturePathName, DerivedData->Mips.Num(), MaximumNumberOfMipMaps);
					}
				}
			}
		}

		if (bSucceeded && bForVirtualTextureStreamingBuild && CVarVTValidateCompressionOnLoad.GetValueOnAnyThread())
		{
			check(DerivedData->VTData);
			bSucceeded = DerivedData->VTData->ValidateData(TexturePathName, false);
			if (!bSucceeded)
			{
				UE_LOG(LogTexture, Display, TEXT("Texture %s has invalid cached VT data. The texture will be rebuilt."), *TexturePathName);
				bInvalidVirtualTextureCompression = true;
			}
		}
		
		// Reset everything derived data so that we can do a clean load from the source data
		if (!bSucceeded)
		{
			DerivedData->Mips.Empty();
			if (DerivedData->VTData)
			{
				delete DerivedData->VTData;
				DerivedData->VTData = nullptr;
			}
			
			bLoadedFromDDC = false;
		}
	}
	
	if (!bSucceeded && bAllowAsyncBuild)
	{
		bool bHasTextureSourceMips = false;
		if (TextureData.IsValid() && Texture.Source.IsBulkDataLoaded())
		{
			TextureData.GetSourceMips(Texture.Source, ImageWrapper);
			bHasTextureSourceMips = true;
		}

		bool bHasCompositeTextureSourceMips = false;
		if (CompositeTextureData.IsValid() && Texture.CompositeTexture && Texture.CompositeTexture->Source.IsBulkDataLoaded())
		{
			CompositeTextureData.GetSourceMips(Texture.CompositeTexture->Source, ImageWrapper);
			bHasCompositeTextureSourceMips = true;
		}

		if (bAllowAsyncLoading && !bHasTextureSourceMips)
		{
			TextureData.GetAsyncSourceMips(ImageWrapper);
			TextureData.AsyncSource.RemoveBulkData();
		}

		if (bAllowAsyncLoading && !bHasCompositeTextureSourceMips)
		{
			CompositeTextureData.GetAsyncSourceMips(ImageWrapper);
			CompositeTextureData.AsyncSource.RemoveBulkData();
		}

		if (TextureData.Blocks.Num() && TextureData.Blocks[0].MipsPerLayer.Num() && TextureData.Blocks[0].MipsPerLayer[0].Num() && 
			(!CompositeTextureData.IsValid() || (CompositeTextureData.Blocks.Num() && CompositeTextureData.Blocks[0].MipsPerLayer.Num() && CompositeTextureData.Blocks[0].MipsPerLayer[0].Num())))
		{
			// Replace any existing DDC data, if corrupt compression was detected
			const bool bReplaceExistingDDC = bInvalidVirtualTextureCompression;
			BuildTexture(ActiveBuildSettingsPerLayer, bReplaceExistingDDC);
			if (bInvalidVirtualTextureCompression && DerivedData->VTData)
			{
				// If we loaded data that turned out to be corrupt, flag it here so we can also recreate the VT data cached to local /DerivedDataCache/VT/ directory
				for (FVirtualTextureDataChunk& Chunk : DerivedData->VTData->Chunks)
				{
					Chunk.bCorruptDataLoadedFromDDC = true;
				}

			}

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

		// Populate the VT DDC Cache now if we're asynchronously loading to avoid too many high prio/synchronous request on the render thread
		if (!IsInGameThread() && DerivedData->VTData && !DerivedData->VTData->Chunks.Last().DerivedDataKey.IsEmpty())
		{
			GetVirtualTextureChunkDDCCache()->MakeChunkAvailable_Concurrent(&DerivedData->VTData->Chunks.Last());
		}
	}
}

void FTextureCacheDerivedDataWorker::Finalize()
{
	// if we couldn't get from the DDC or didn't build synchronously, then we have to build now. 
	// This is a super edge case that should rarely happen.
	// (update) this always happens with a ForceRebuildPlatformData, as its a synchronous build that
	//		uses this worker class. The DoWork never does work if async is not set unless its a clean
	//		get, so we land here. Note that we never _fetch_ here, it's only ever a full build, so
	//		we can ignore FetchFirst.
	if (!bSucceeded)
	{
		TextureData.GetSourceMips(Texture.Source, ImageWrapper);
		if (Texture.CompositeTexture)
		{
			CompositeTextureData.GetSourceMips(Texture.CompositeTexture->Source, ImageWrapper);
		}

		BuildTexture(BuildSettingsPerLayerFetchOrBuild);
	}
		
	if (bSucceeded && BuildSettingsPerLayerFetchOrBuild[0].bVirtualStreamable) // Texture.VirtualTextureStreaming is more a hint that might be overruled by the buildsettings
	{
		check((DerivedData->VTData != nullptr) == Texture.VirtualTextureStreaming); 
	}
}

//
// DDC2 texture fetch/build task.
//
class FTextureBuildTask final : public FTextureAsyncCacheDerivedDataTask
{
public:
	FTextureBuildTask(
		UTexture& Texture,
		FTexturePlatformData& InDerivedData,
		const UE::DerivedData::FUtf8SharedString& FunctionName,
		const FTextureBuildSettings* InSettingsFetchFirst, // can be nullptr
		const FTextureBuildSettings& InSettingsFetchOrBuild,
		const FTexturePlatformData::FTextureEncodeResultMetadata* InFetchFirstMetadata, // can be nullptr
		const FTexturePlatformData::FTextureEncodeResultMetadata* InFetchOrBuildMetadata, // can be nullptr
		EQueuedWorkPriority InPriority,
		ETextureCacheFlags Flags)
		: DerivedData(InDerivedData)
		, Priority(InPriority)
		, bCacheHit(false)
		, bInlineMips(EnumHasAnyFlags(Flags, ETextureCacheFlags::InlineMips))
		, FirstMipToLoad(InSettingsFetchOrBuild.LODBiasWithCinematicMips)
		, InputResolver(Texture)
	{
		using namespace UE::DerivedData;

		static bool bLoadedModules = LoadModules();

		FSharedString TexturePath;
		{
			TStringBuilder<256> TexturePathBuilder;
			Texture.GetPathName(nullptr, TexturePathBuilder);
			TexturePath = TexturePathBuilder.ToView();
		}

		IBuild& Build = GetBuild();
		IBuildInputResolver* GlobalResolver = GetGlobalBuildInputResolver();
		BuildSession = Build.CreateSession(TexturePath, GlobalResolver ? GlobalResolver : &InputResolver);

		EPriority OwnerPriority = EnumHasAnyFlags(Flags, ETextureCacheFlags::Async) ? ConvertPriority(Priority) : UE::DerivedData::EPriority::Blocking;
		Owner.Emplace(OwnerPriority);

		bool bUseCompositeTexture;
		if (!IsTextureValidForBuilding(Texture, Flags, bUseCompositeTexture))
		{
			return;
		}
		
		if (IsInGameThread() && OwnerPriority == EPriority::Blocking)
		{
			// this gets sent whether or not we are building the texture, and is a rare edge case for UI feedback.
			// We don't actually know whether we're using fetchfirst or actually building, so if we have two keys,
			// we can assume we're FinalIfAvailable.
			ETextureEncodeSpeed EncodeSpeed = (ETextureEncodeSpeed)InSettingsFetchOrBuild.RepresentsEncodeSpeedNoSend;
			if (InSettingsFetchFirst)
			{
				EncodeSpeed = ETextureEncodeSpeed::FinalIfAvailable;
			}

			StatusMessage.Emplace(ComposeTextureBuildText(Texture, InSettingsFetchOrBuild, EncodeSpeed, Texture.GetBuildRequiredMemory(), EnumHasAnyFlags(Flags, ETextureCacheFlags::ForVirtualTextureStreamingBuild)));
		}
		

		if (InSettingsFetchFirst &&
			EnumHasAnyFlags(Flags, ETextureCacheFlags::ForceRebuild))
		{
			// Can't fetch first if we are rebuilding.
			InSettingsFetchFirst = nullptr;
		}

		FBuildDefinition FetchOrBuildDefinition = CreateDefinition(Build, Texture, TexturePath, FunctionName, InSettingsFetchOrBuild, bUseCompositeTexture);
		DerivedData.FetchOrBuildDerivedDataKey.Emplace<FTexturePlatformData::FStructuredDerivedDataKey>(GetKey(FetchOrBuildDefinition, Texture, bUseCompositeTexture));
		
		bool bBuildKicked = false;
		if (InSettingsFetchFirst)
		{
			// If the keys are the same, ignore fetch first.
			FBuildDefinition FetchDefinition = CreateDefinition(Build, Texture, TexturePath, FunctionName, *InSettingsFetchFirst, bUseCompositeTexture);
			if (FetchDefinition.GetKey() != FetchOrBuildDefinition.GetKey())
			{
				bBuildKicked = true;
				if (InFetchFirstMetadata)
				{
					this->DerivedData.ResultMetadata = *InFetchFirstMetadata;
				}

				FBuildPolicyBuilder BuildPolicyBuilder(bInlineMips ? EBuildPolicy::Cache : (EBuildPolicy::CacheQuery | EBuildPolicy::SkipData));
				if (!bInlineMips)
				{
					BuildPolicyBuilder.AddValuePolicy(FValueId::FromName("Description"_ASV), EBuildPolicy::Cache);
					BuildPolicyBuilder.AddValuePolicy(FValueId::FromName("MipTail"_ASV), EBuildPolicy::Cache);
				}

				BuildSession.Get().Build(FetchDefinition, {}, BuildPolicyBuilder.Build(), *Owner,
					[this, 
					 FetchOrBuildDefinition = MoveTemp(FetchOrBuildDefinition), 
					 Flags,
					 FetchOrBuildMetadata = InFetchOrBuildMetadata ? *InFetchOrBuildMetadata : FTexturePlatformData::FTextureEncodeResultMetadata()
					](FBuildCompleteParams&& Params)
					{
						switch (Params.Status)
						{
						default:
						case EStatus::Ok:
							return EndBuild(MoveTemp(Params));
						case EStatus::Error:							
							this->DerivedData.ResultMetadata = FetchOrBuildMetadata;
							return BeginBuild(FetchOrBuildDefinition, Flags);
						}
					});
			}
		}

		if (bBuildKicked == false)
		{
			// we didn't use the fetch first path.
			if (InFetchOrBuildMetadata)
			{
				this->DerivedData.ResultMetadata = *InFetchOrBuildMetadata;
			}
			BeginBuild(FetchOrBuildDefinition, Flags);
		}
	}

	static UE::DerivedData::FBuildDefinition CreateDefinition(
		UE::DerivedData::IBuild& Build,
		UTexture& Texture,
		const UE::DerivedData::FSharedString& TexturePath,
		const UE::DerivedData::FUtf8SharedString& FunctionName,
		const FTextureBuildSettings& Settings,
		const bool bUseCompositeTexture)
	{
		UE::DerivedData::FBuildDefinitionBuilder DefinitionBuilder = Build.CreateDefinition(TexturePath, FunctionName);
		DefinitionBuilder.AddConstant(UTF8TEXTVIEW("Settings"),
			SaveTextureBuildSettings(Texture, Settings, 0, NUM_INLINE_DERIVED_MIPS, bUseCompositeTexture));
		DefinitionBuilder.AddInputBulkData(UTF8TEXTVIEW("Source"), Texture.Source.GetPersistentId());
		if (Texture.CompositeTexture && bUseCompositeTexture)
		{
			DefinitionBuilder.AddInputBulkData(UTF8TEXTVIEW("CompositeSource"), Texture.CompositeTexture->Source.GetPersistentId());
		}
		return DefinitionBuilder.Build();
	}

private:
	void BeginBuild(const UE::DerivedData::FBuildDefinition& Definition, ETextureCacheFlags Flags)
	{
		using namespace UE::DerivedData;

		FBuildPolicy BuildPolicy;
		if (EnumHasAnyFlags(Flags, ETextureCacheFlags::ForceRebuild))
		{
			BuildPolicy = EBuildPolicy::Default & ~EBuildPolicy::CacheQuery;
		}
		else if (bInlineMips)
		{
			BuildPolicy = EBuildPolicy::Default;
		}
		else
		{
			FBuildPolicyBuilder BuildPolicyBuilder(EBuildPolicy::Build | EBuildPolicy::CacheQuery | EBuildPolicy::CacheStoreOnBuild | EBuildPolicy::SkipData);
			BuildPolicyBuilder.AddValuePolicy(FValueId::FromName("Description"_ASV), EBuildPolicy::Default);
			BuildPolicyBuilder.AddValuePolicy(FValueId::FromName("MipTail"_ASV), EBuildPolicy::Default);
			BuildPolicy = BuildPolicyBuilder.Build();
		}

		BuildSession.Get().Build(Definition, {}, BuildPolicy, *Owner,
			[this](FBuildCompleteParams&& Params) { EndBuild(MoveTemp(Params)); });
	}

	void EndBuild(UE::DerivedData::FBuildCompleteParams&& Params)
	{
		using namespace UE::DerivedData;
		DerivedData.DerivedDataKey.Emplace<FCacheKeyProxy>(Params.CacheKey);
		bCacheHit = EnumHasAnyFlags(Params.BuildStatus, EBuildStatus::CacheQueryHit);
		BuildOutputSize = Algo::TransformAccumulate(Params.Output.GetValues(),
			[](const FValue& Value) { return Value.GetData().GetRawSize(); }, uint64(0));
		if (Params.Status != EStatus::Canceled)
		{
			WriteDerivedData(MoveTemp(Params.Output));
		}
		StatusMessage.Reset();
	}

	void Finalize(bool& bOutFoundInCache, uint64& OutProcessedByteCount) final
	{
		bOutFoundInCache = bCacheHit;
		OutProcessedByteCount = BuildOutputSize;
	}
public:

	EQueuedWorkPriority GetPriority() const final
	{
		return Priority;
	}

	bool SetPriority(EQueuedWorkPriority QueuedWorkPriority) final
	{
		Priority = QueuedWorkPriority;
		Owner->SetPriority(ConvertPriority(QueuedWorkPriority));
		return true;
	}

	bool Cancel() final
	{
		Owner->Cancel();
		return true;
	}

	void Wait() final
	{
		Owner->Wait();
	}

	bool WaitWithTimeout(float TimeLimitSeconds) final
	{
		const double TimeLimit = FPlatformTime::Seconds() + TimeLimitSeconds;
		if (Poll())
		{
			return true;
		}
		do
		{
			FPlatformProcess::Sleep(0.005);
			if (Poll())
			{
				return true;
			}
		}
		while (FPlatformTime::Seconds() < TimeLimit);
		return false;
	}

	bool Poll() const final
	{
		return Owner->Poll();
	}

	static bool IsTextureValidForBuilding(UTexture& Texture, ETextureCacheFlags Flags, bool& bOutUseCompositeTexture)
	{
		const int32 NumBlocks = Texture.Source.GetNumBlocks();
		const int32 NumLayers = Texture.Source.GetNumLayers();
		if (NumBlocks < 1 || NumLayers < 1)
		{
			UE_LOG(LogTexture, Error, TEXT("Texture has no source data: %s"), *Texture.GetPathName());
			return false;
		}

		for (int LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
		{
			switch (Texture.Source.GetFormat(LayerIndex))
			{
			case TSF_G8:
			case TSF_G16:
			case TSF_BGRA8:
			case TSF_BGRE8:
			case TSF_RGBA16:
			case TSF_RGBA16F:
				break;
			default:
				UE_LOG(LogTexture, Fatal, TEXT("Texture %s has source art in an invalid format."), *Texture.GetPathName());
				return false;
			}
		}

		const bool bCompositeTextureViable = Texture.CompositeTexture && Texture.CompositeTextureMode != CTM_Disabled;
		bool bMatchingBlocks = bCompositeTextureViable && (Texture.CompositeTexture->Source.GetNumBlocks() == Texture.Source.GetNumBlocks());
		bool bMatchingAspectRatio = bCompositeTextureViable;
		bool bOnlyPowerOfTwoSize = bCompositeTextureViable;

		int32 BlockSizeX = 0;
		int32 BlockSizeY = 0;
		TArray<FIntPoint> BlockSizes;
		BlockSizes.Reserve(NumBlocks);
		for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
		{
			FTextureSourceBlock SourceBlock;
			Texture.Source.GetBlock(BlockIndex, SourceBlock);
			if (SourceBlock.NumMips > 0 && SourceBlock.NumSlices > 0)
			{
				BlockSizes.Emplace(SourceBlock.SizeX, SourceBlock.SizeY);
				BlockSizeX = FMath::Max(BlockSizeX, SourceBlock.SizeX);
				BlockSizeY = FMath::Max(BlockSizeY, SourceBlock.SizeY);
			}

			if (bCompositeTextureViable)
			{
				FTextureSourceBlock CompositeTextureBlock;
				Texture.CompositeTexture->Source.GetBlock(BlockIndex, CompositeTextureBlock);

				bMatchingBlocks = bMatchingBlocks && SourceBlock.BlockX == CompositeTextureBlock.BlockX && SourceBlock.BlockY == CompositeTextureBlock.BlockY;
				bMatchingAspectRatio = bMatchingAspectRatio && SourceBlock.SizeX * CompositeTextureBlock.SizeY == SourceBlock.SizeY * CompositeTextureBlock.SizeX;
				bOnlyPowerOfTwoSize = bOnlyPowerOfTwoSize && FMath::IsPowerOfTwo(SourceBlock.SizeX) && FMath::IsPowerOfTwo(SourceBlock.SizeY);
			}
		}

		for (int32 BlockIndex = 0; BlockIndex < BlockSizes.Num(); ++BlockIndex)
		{
			const int32 MipBiasX = FMath::CeilLogTwo(BlockSizeX / BlockSizes[BlockIndex].X);
			const int32 MipBiasY = FMath::CeilLogTwo(BlockSizeY / BlockSizes[BlockIndex].Y);
			if (MipBiasX != MipBiasY)
			{
				UE_LOG(LogTexture, Error, TEXT("Texture %s has blocks with mismatched aspect ratios"), *Texture.GetPathName());
				return false;
			}
		}

		if (bCompositeTextureViable)
		{
			if (!bMatchingBlocks)
			{
				UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Composite texture resolution/UDIMs do not match. Composite texture will be ignored"), *Texture.GetPathName());
			}
			else if (!bOnlyPowerOfTwoSize)
			{
				UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Some blocks (UDIMs) have a non power of two size. Composite texture will be ignored"), *Texture.GetPathName());
			}
			else if (!bMatchingAspectRatio)
			{
				UE_LOG(LogTexture, Warning, TEXT("Issue while building %s : Some blocks (UDIMs) have mismatched aspect ratio. Composite texture will be ignored"), *Texture.GetPathName());
			}
		}

		bOutUseCompositeTexture = bMatchingBlocks && bMatchingAspectRatio && bOnlyPowerOfTwoSize;

		// TODO: Add validation equivalent to that found in FTextureCacheDerivedDataWorker::BuildTexture for virtual textures
		//		 if virtual texture support is added for this code path.
		if (!EnumHasAnyFlags(Flags, ETextureCacheFlags::ForVirtualTextureStreamingBuild))
		{
			// Only support single Block/Layer here (Blocks and Layers are intended for VT support)
			if (NumBlocks > 1)
			{
				// This can happen if user attempts to import a UDIM without VT enabled
				UE_LOG(LogTexture, Log, TEXT("Texture %s was imported as UDIM with %d blocks but VirtualTexturing is not enabled, only the first block will be available"),
					*Texture.GetPathName(), NumBlocks);
			}
			if (NumLayers > 1)
			{
				// This can happen if user attempts to use lightmaps or other layered VT without VT enabled
				UE_LOG(LogTexture, Log, TEXT("Texture %s has %d layers but VirtualTexturing is not enabled, only the first layer will be available"),
					*Texture.GetPathName(), NumLayers);
			}
		}

		return true;
	}

	static FTexturePlatformData::FStructuredDerivedDataKey GetKey(const UE::DerivedData::FBuildDefinition& BuildDefinition, const UTexture& Texture, bool bUseCompositeTexture)
	{
		FTexturePlatformData::FStructuredDerivedDataKey Key;
		Key.BuildDefinitionKey = BuildDefinition.GetKey().Hash;
		Key.SourceGuid = Texture.Source.GetId();
		if (bUseCompositeTexture && Texture.CompositeTexture)
		{
			Key.CompositeSourceGuid = Texture.CompositeTexture->Source.GetId();
		}
		return Key;
	}

private:

	static bool DeserializeTextureFromValues(FTexturePlatformData& DerivedData, const UE::DerivedData::FBuildOutput& Output, int32 FirstMipToLoad, bool bInlineMips)
	{
		using namespace UE::DerivedData;
		const FValueWithId& Value = Output.GetValue(FValueId::FromName("Description"_ASV));
		if (!Value)
		{
			UE_LOG(LogTexture, Error, TEXT("Missing texture description for build of '%s' by %s."),
				*WriteToString<128>(Output.GetName()), *WriteToString<32>(Output.GetFunction()));
			return false;
		}

		FCbObject TextureDescription(Value.GetData().Decompress());

		FCbFieldViewIterator SizeIt = TextureDescription["Size"_ASV].AsArrayView().CreateViewIterator();
		DerivedData.SizeX = SizeIt++->AsInt32();
		DerivedData.SizeY = SizeIt++->AsInt32();
		int32 NumSlices = SizeIt++->AsInt32();

		UEnum* PixelFormatEnum = UTexture::GetPixelFormatEnum();
		FUtf8StringView PixelFormatStringView = TextureDescription["PixelFormat"_ASV].AsString();
		FName PixelFormatName(PixelFormatStringView.Len(), PixelFormatStringView.GetData());
		DerivedData.PixelFormat = (EPixelFormat)PixelFormatEnum->GetValueByName(PixelFormatName);

		const bool bCubeMap = TextureDescription["bCubeMap"_ASV].AsBool();
		DerivedData.OptData.ExtData = TextureDescription["ExtData"_ASV].AsUInt32();
		DerivedData.OptData.NumMipsInTail = TextureDescription["NumMipsInTail"_ASV].AsUInt32();
		const bool bHasOptData = (DerivedData.OptData.NumMipsInTail != 0) || (DerivedData.OptData.ExtData != 0);
		static constexpr uint32 BitMask_CubeMap = 1u << 31u;
		static constexpr uint32 BitMask_HasOptData = 1u << 30u;
		static constexpr uint32 BitMask_NumSlices = BitMask_HasOptData - 1u;
		DerivedData.PackedData = (NumSlices & BitMask_NumSlices) | (bCubeMap ? BitMask_CubeMap : 0) | (bHasOptData ? BitMask_HasOptData : 0);

		int32 NumMips = TextureDescription["NumMips"_ASV].AsInt32();
		int32 NumStreamingMips = TextureDescription["NumStreamingMips"_ASV].AsInt32();

		FCbArrayView MipArrayView = TextureDescription["Mips"_ASV].AsArrayView();
		if (NumMips != MipArrayView.Num())
		{
			UE_LOG(LogTexture, Error, TEXT("Mismatched mip quantity (%d and %d) for build of '%s' by %s."),
				NumMips, MipArrayView.Num(), *WriteToString<128>(Output.GetName()), *WriteToString<32>(Output.GetFunction()));
			return false;
		}
		check(NumMips >= (int32)DerivedData.OptData.NumMipsInTail);
		check(NumMips >= NumStreamingMips);

		FSharedBuffer MipTailData;
		if (NumMips > NumStreamingMips)
		{
			const FValueWithId& MipTailValue = Output.GetValue(FValueId::FromName("MipTail"_ASV));
			if (!MipTailValue)
			{
				UE_LOG(LogTexture, Error, TEXT("Missing texture mip tail for build of '%s' by %s."),
					*WriteToString<128>(Output.GetName()), *WriteToString<32>(Output.GetFunction()));
				return false;
			}
			MipTailData = MipTailValue.GetData().Decompress();
		}

		int32 MipIndex = 0;
		DerivedData.Mips.Empty(NumMips);
		for (FCbFieldView MipFieldView : MipArrayView)
		{
			FCbObjectView MipObjectView = MipFieldView.AsObjectView();
			FTexture2DMipMap* NewMip = new FTexture2DMipMap();

			FCbFieldViewIterator MipSizeIt = MipObjectView["Size"_ASV].AsArrayView().CreateViewIterator();
			NewMip->SizeX = MipSizeIt++->AsInt32();
			NewMip->SizeY = MipSizeIt++->AsInt32();
			NewMip->SizeZ = MipSizeIt++->AsInt32();
			NewMip->FileRegionType = static_cast<EFileRegionType>(MipObjectView["FileRegion"_ASV].AsInt32());
			
			if (MipIndex >= NumStreamingMips)
			{
				uint64 MipSize = MipObjectView["NumBytes"_ASV].AsUInt64();
				FMemoryView MipView = MipTailData.GetView().Mid(MipObjectView["MipOffset"_ASV].AsUInt64(), MipSize);

				NewMip->BulkData.Lock(LOCK_READ_WRITE);
				void* MipAllocData = NewMip->BulkData.Realloc(int64(MipSize));
				MakeMemoryView(MipAllocData, MipSize).CopyFrom(MipView);
				NewMip->BulkData.Unlock();
				NewMip->SetPagedToDerivedData(false);
			}
			else if (bInlineMips && (MipIndex >= FirstMipToLoad))
			{
				const FValueWithId& StreamingMipValue = Output.GetValue(FTexturePlatformData::MakeMipId(MipIndex));
				if (!StreamingMipValue)
				{
					delete NewMip;
					UE_LOG(LogTexture, Error, TEXT("Missing texture streaming mip '%d' for build of '%s' by %s."),
						MipIndex, *WriteToString<128>(Output.GetName()), *WriteToString<32>(Output.GetFunction()));
					return false;
				}
				FSharedBuffer StreamingMipData = StreamingMipValue.GetData().Decompress();
				uint64 MipSize = StreamingMipData.GetSize();

				NewMip->BulkData.Lock(LOCK_READ_WRITE);
				void* MipAllocData = NewMip->BulkData.Realloc(int64(MipSize));
				MakeMemoryView(MipAllocData, MipSize).CopyFrom(StreamingMipData.GetView());
				NewMip->BulkData.Unlock();
				NewMip->SetPagedToDerivedData(false);
			}
			else
			{
				NewMip->SetPagedToDerivedData(true);
			}

			DerivedData.Mips.Add(NewMip);
			++MipIndex;
		}

		return true;
	}

	void WriteDerivedData(UE::DerivedData::FBuildOutput&& Output)
	{
		using namespace UE::DerivedData;

		const FSharedString& Name = Output.GetName();
		const FUtf8SharedString& Function = Output.GetFunction();

		for (const FBuildOutputMessage& Message : Output.GetMessages())
		{
			switch (Message.Level)
			{
			case EBuildOutputMessageLevel::Error:
				UE_LOG(LogTexture, Warning, TEXT("[Error] %s (Build of '%s' by %s.)"),
					*WriteToString<256>(Message.Message), *Name, *WriteToString<32>(Function));
				break;
			case EBuildOutputMessageLevel::Warning:
				UE_LOG(LogTexture, Warning, TEXT("%s (Build of '%s' by %s.)"),
					*WriteToString<256>(Message.Message), *Name, *WriteToString<32>(Function));
				break;
			case EBuildOutputMessageLevel::Display:
				UE_LOG(LogTexture, Display, TEXT("%s (Build of '%s' by %s.)"),
					*WriteToString<256>(Message.Message), *Name, *WriteToString<32>(Function));
				break;
			default:
				checkNoEntry();
				break;
			}
		}

		for (const FBuildOutputLog& Log : Output.GetLogs())
		{
			switch (Log.Level)
			{
			case EBuildOutputLogLevel::Error:
				UE_LOG(LogTexture, Warning, TEXT("[Error] %s: %s (Build of '%s' by %s.)"),
					*WriteToString<64>(Log.Category), *WriteToString<256>(Log.Message),
					*Name, *WriteToString<32>(Function));
				break;
			case EBuildOutputLogLevel::Warning:
				UE_LOG(LogTexture, Warning, TEXT("%s: %s (Build of '%s' by %s.)"),
					*WriteToString<64>(Log.Category), *WriteToString<256>(Log.Message),
					*Name, *WriteToString<32>(Function));
				break;
			default:
				checkNoEntry();
				break;
			}
		}

		if (Output.HasError())
		{
			UE_LOG(LogTexture, Warning, TEXT("Failed to build derived data for build of '%s' by %s."),
				*Name, *WriteToString<32>(Function));
			return;
		}

		DeserializeTextureFromValues(DerivedData, Output, FirstMipToLoad, bInlineMips);
	}

	static UE::DerivedData::EPriority ConvertPriority(EQueuedWorkPriority SourcePriority)
	{
		using namespace UE::DerivedData;
		switch (SourcePriority)
		{
		case EQueuedWorkPriority::Lowest:  return EPriority::Lowest;
		case EQueuedWorkPriority::Low:     return EPriority::Low;
		case EQueuedWorkPriority::Normal:  return EPriority::Normal;
		case EQueuedWorkPriority::High:    return EPriority::High;
		case EQueuedWorkPriority::Highest: return EPriority::Highest;
		default:                           return EPriority::Normal;
		}
	}

	static EQueuedWorkPriority ConvertPriority(UE::DerivedData::EPriority SourcePriority)
	{
		using namespace UE::DerivedData;
		switch (SourcePriority)
		{
		case EPriority::Lowest:   return EQueuedWorkPriority::Lowest;
		case EPriority::Low:      return EQueuedWorkPriority::Low;
		case EPriority::Normal:   return EQueuedWorkPriority::Normal;
		case EPriority::High:     return EQueuedWorkPriority::High;
		case EPriority::Highest:  return EQueuedWorkPriority::Highest;
		case EPriority::Blocking: return EQueuedWorkPriority::Blocking;
		default:                  return EQueuedWorkPriority::Normal;
		}
	}

	static bool LoadModules()
	{
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		FModuleManager::LoadModuleChecked<ITextureCompressorModule>(TEXTURE_COMPRESSOR_MODULENAME);
		return true;
	}

	FTexturePlatformData& DerivedData;
	TOptional<UE::DerivedData::FRequestOwner> Owner;
	UE::DerivedData::FOptionalBuildSession BuildSession;
	EQueuedWorkPriority Priority;
	bool bCacheHit;
	bool bInlineMips;
	int32 FirstMipToLoad;
	uint64 BuildOutputSize = 0;
	TOptional<FTextureStatusMessageContext> StatusMessage;
	UE::TextureDerivedData::FTextureBuildInputResolver InputResolver;
	FRWLock Lock;
}; // end DDC2 fetch/build task (FTextureBuildTask)

FTextureAsyncCacheDerivedDataTask* CreateTextureBuildTask(
	UTexture& Texture,
	FTexturePlatformData& DerivedData,
	const FTextureBuildSettings* SettingsFetch,
	const FTextureBuildSettings& SettingsFetchOrBuild,
	const FTexturePlatformData::FTextureEncodeResultMetadata* FetchMetadata,
	const FTexturePlatformData::FTextureEncodeResultMetadata* FetchOrBuildMetadata,
	EQueuedWorkPriority Priority,
	ETextureCacheFlags Flags)
{
	using namespace UE::DerivedData;
	if (FUtf8SharedString FunctionName = FindTextureBuildFunction(SettingsFetchOrBuild.TextureFormatName); !FunctionName.IsEmpty())
	{
		return new FTextureBuildTask(Texture, DerivedData, FunctionName, SettingsFetch, SettingsFetchOrBuild, FetchMetadata, FetchOrBuildMetadata, Priority, Flags);
	}
	return nullptr;
}

FTexturePlatformData::FStructuredDerivedDataKey CreateTextureDerivedDataKey(
	UTexture& Texture,
	ETextureCacheFlags CacheFlags,
	const FTextureBuildSettings& Settings)
{
	using namespace UE::DerivedData;

	if (FUtf8SharedString FunctionName = FindTextureBuildFunction(Settings.TextureFormatName); !FunctionName.IsEmpty())
	{
		IBuild& Build = GetBuild();

		TStringBuilder<256> TexturePath;
		Texture.GetPathName(nullptr, TexturePath);

		bool bUseCompositeTexture = false;
		if (FTextureBuildTask::IsTextureValidForBuilding(Texture, CacheFlags, bUseCompositeTexture))
		{
			FBuildDefinition Definition = FTextureBuildTask::CreateDefinition(Build, Texture, TexturePath.ToView(), FunctionName, Settings, bUseCompositeTexture);

			return FTextureBuildTask::GetKey(Definition, Texture, bUseCompositeTexture);
		}
	}
	return FTexturePlatformData::FStructuredDerivedDataKey();
}

#endif // WITH_EDITOR

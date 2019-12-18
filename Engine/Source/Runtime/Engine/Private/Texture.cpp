// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Engine/Texture.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FeedbackContext.h"
#include "HAL/LowLevelMemTracker.h"
#include "UObject/UObjectIterator.h"
#include "TextureResource.h"
#include "Engine/Texture2D.h"
#include "ContentStreaming.h"
#include "EngineUtils.h"
#include "Engine/AssetUserData.h"
#include "EditorSupportDelegates.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Interfaces/ITargetPlatform.h"
#include "Engine/TextureLODSettings.h"
#include "RenderUtils.h"

#if WITH_EDITORONLY_DATA
	#include "EditorFramework/AssetImportData.h"
#endif

#include "Engine/TextureCube.h"

static TAutoConsoleVariable<int32> CVarVirtualTextures(
	TEXT("r.VirtualTextures"),
	0,
	TEXT("Is virtual texture streaming enabled?"),
	ECVF_RenderThreadSafe | ECVF_ReadOnly);


DEFINE_LOG_CATEGORY(LogTexture);

#if STATS
DECLARE_STATS_GROUP(TEXT("Texture Group"), STATGROUP_TextureGroup, STATCAT_Advanced);

// Declare the stats for each Texture Group.
#define DECLARETEXTUREGROUPSTAT(Group) DECLARE_MEMORY_STAT(TEXT(#Group),STAT_##Group,STATGROUP_TextureGroup);
FOREACH_ENUM_TEXTUREGROUP(DECLARETEXTUREGROUPSTAT)
#undef DECLARETEXTUREGROUPSTAT


// Initialize TextureGroupStatFNames array with the FNames for each stats.
FName FTextureResource::TextureGroupStatFNames[TEXTUREGROUP_MAX] =
	{
		#define ASSIGNTEXTUREGROUPSTATNAME(Group) GET_STATFNAME(STAT_##Group),
		FOREACH_ENUM_TEXTUREGROUP(ASSIGNTEXTUREGROUPSTATNAME)
		#undef ASSIGNTEXTUREGROUPSTATNAME
	};
#endif

// This is used to prevent the PostEditChange to automatically update the material depedencies & material context, in some case we want to manually control this
// to be more efficient.
ENGINE_API bool GDisableAutomaticTextureMaterialUpdateDependencies = false;

UTexture::FOnTextureSaved UTexture::PreSaveEvent;

UTexture::UTexture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SRGB = true;
	Filter = TF_Default;
	MipLoadOptions = ETextureMipLoadOptions::Default;
#if WITH_EDITORONLY_DATA
	AdjustBrightness = 1.0f;
	AdjustBrightnessCurve = 1.0f;
	AdjustVibrance = 0.0f;
	AdjustSaturation = 1.0f;
	AdjustRGBCurve = 1.0f;
	AdjustHue = 0.0f;
	AdjustMinAlpha = 0.0f;
	AdjustMaxAlpha = 1.0f;
	MaxTextureSize = 0; // means no limitation
	MipGenSettings = TMGS_FromTextureGroup;
	CompositeTextureMode = CTM_NormalRoughnessToAlpha;
	CompositePower = 1.0f;
	bUseLegacyGamma = false;
	AlphaCoverageThresholds = FVector4(0, 0, 0, 0);
	PaddingColor = FColor::Black;
	ChromaKeyColor = FColorList::Magenta;
	ChromaKeyThreshold = 1.0f / 255.0f;
	VirtualTextureStreaming = 0;
	CompressionYCoCg = 0;
	
#endif // #if WITH_EDITORONLY_DATA

	if (FApp::CanEverRender() && !IsTemplate())
	{
		TextureReference.BeginInit_GameThread();
	}
}

void UTexture::ReleaseResource()
{
	if (Resource)
	{
		UTexture2D* Texture2D = Cast<UTexture2D>(this);
		check( !Texture2D  || !Texture2D->HasPendingUpdate() );

		// Free the resource.
		ReleaseResourceAndFlush(Resource);
		delete Resource;
		Resource = NULL;
	}
}

void UTexture::UpdateResource()
{
	// Release the existing texture resource.
	ReleaseResource();

	//Dedicated servers have no texture internals
	if( FApp::CanEverRender() && !HasAnyFlags(RF_ClassDefaultObject) )
	{
		// Create a new texture resource.
		Resource = CreateResource();
		if( Resource )
		{
			LLM_SCOPE(ELLMTag::Textures);
			BeginInitResource(Resource);
		}
	}
}

bool UTexture::IsPostLoadThreadSafe() const
{
	return false;
}

#if WITH_EDITOR
bool UTexture::CanEditChange(const UProperty* InProperty) const
{
	if (InProperty)
	{
		const FName PropertyName = InProperty->GetFName();

		if (PropertyName == GET_MEMBER_NAME_STRING_CHECKED(UTexture, AdjustVibrance))
		{
			return !HasHDRSource();
		}

		// Virtual Texturing is only supported for Texture2D 
		static const FName VirtualTextureStreamingName = GET_MEMBER_NAME_CHECKED(UTexture, VirtualTextureStreaming);
		if (PropertyName == VirtualTextureStreamingName)
		{
			return this->IsA<UTexture2D>();
		}
	}

	return true;
}

void UTexture::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	SetLightingGuid();

	// Determine whether any property that requires recompression of the texture, or notification to Materials has changed.
	bool RequiresNotifyMaterials = false;
	bool DeferCompressionWasEnabled = false;

	UProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	if( PropertyThatChanged )
	{
		static const FName CompressionSettingsName = GET_MEMBER_NAME_CHECKED(UTexture, CompressionSettings);
		static const FName LODGroupName = GET_MEMBER_NAME_CHECKED(UTexture, LODGroup);
		static const FName DeferCompressionName = GET_MEMBER_NAME_CHECKED(UTexture, DeferCompression);
		static const FName SrgbName = GET_MEMBER_NAME_CHECKED(UTexture, SRGB);
		static const FName VirtualTextureStreamingName = GET_MEMBER_NAME_CHECKED(UTexture, VirtualTextureStreaming);
#if WITH_EDITORONLY_DATA
		static const FName MaxTextureSizeName = GET_MEMBER_NAME_CHECKED(UTexture, MaxTextureSize);
		static const FName CompressionQualityName = GET_MEMBER_NAME_CHECKED(UTexture, CompressionQuality);
#endif //WITH_EDITORONLY_DATA

		const FName PropertyName = PropertyThatChanged->GetFName();

		if ((PropertyName == CompressionSettingsName) ||
			(PropertyName == LODGroupName) ||
			(PropertyName == SrgbName))
		{
			RequiresNotifyMaterials = true;

			if (PropertyName == LODGroupName)
			{
				if (LODGroup == TEXTUREGROUP_8BitData)
				{
					CompressionSettings = TC_VectorDisplacementmap;
					SRGB = false;
					Filter = TF_Default;
					MipGenSettings = TMGS_FromTextureGroup;
				}
				else if (LODGroup == TEXTUREGROUP_16BitData)
				{
					CompressionSettings = TC_HDR;
					SRGB = false;
					Filter = TF_Default;
					MipGenSettings = TMGS_FromTextureGroup;
				}
			}
		}
		else if (PropertyName == DeferCompressionName)
		{
			DeferCompressionWasEnabled = DeferCompression;
		}
#if WITH_EDITORONLY_DATA
		else if (PropertyName == CompressionQualityName)
		{
			RequiresNotifyMaterials = true;
		}
		else if (PropertyName == MaxTextureSizeName)
		{
			if (MaxTextureSize <= 0)
			{
				MaxTextureSize = 0;
			}
			else
			{
				MaxTextureSize = FMath::Min<int32>(FMath::RoundUpToPowerOfTwo(MaxTextureSize), GetMaximumDimension());
			}
		}
		else if (PropertyName == VirtualTextureStreamingName)
		{
			RequiresNotifyMaterials = true;
		}
#endif //WITH_EDITORONLY_DATA

		bool bPreventSRGB = (CompressionSettings == TC_Alpha || CompressionSettings == TC_Normalmap || CompressionSettings == TC_Masks || CompressionSettings == TC_HDR || CompressionSettings == TC_HDR_Compressed);
		if(bPreventSRGB && SRGB == true)
		{
			SRGB = false;
		}
	}
	else if (!GDisableAutomaticTextureMaterialUpdateDependencies)
	{
		// Update any material that uses this texture and must force a recompile of cache ressource
		TArray<UMaterial*> MaterialsToUpdate;
		TSet<UMaterial*> BaseMaterialsThatUseThisTexture;
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
		{
			UMaterialInterface* MaterialInterface = *It;
			if (DoesMaterialUseTexture(MaterialInterface, this))
			{
				UMaterial* Material = MaterialInterface->GetMaterial();
				bool MaterialAlreadyCompute = false;
				BaseMaterialsThatUseThisTexture.Add(Material, &MaterialAlreadyCompute);
				if (!MaterialAlreadyCompute)
				{
					if (Material->IsTextureForceRecompileCacheRessource(this))
					{
						MaterialsToUpdate.Add(Material);
						Material->UpdateMaterialShaderCacheAndTextureReferences();
					}
				}
			}
		}

		if (MaterialsToUpdate.Num())
		{
			FMaterialUpdateContext UpdateContext;

			for (UMaterial* MaterialToUpdate: MaterialsToUpdate)
			{
				UpdateContext.AddMaterial(MaterialToUpdate);
			}
		}
	}

	NumCinematicMipLevels = FMath::Max<int32>( NumCinematicMipLevels, 0 );

	// Don't update the texture resource if we've turned "DeferCompression" on, as this 
	// would cause it to immediately update as an uncompressed texture
	if( !DeferCompressionWasEnabled && (PropertyChangedEvent.ChangeType & EPropertyChangeType::Interactive) == 0 )
	{
		// Update the texture resource. This will recache derived data if necessary
		// which may involve recompressing the texture.
		UpdateResource();
	}

	// Notify any loaded material instances if changed our compression format
	if (RequiresNotifyMaterials)
	{
		NotifyMaterials();
	}
		
#if WITH_EDITORONLY_DATA
	// any texture that is referencing this texture as AssociatedNormalMap needs to be informed
	{
		TArray<UTexture*> TexturesThatUseThisTexture;

		for (TObjectIterator<UTexture> It; It; ++It)
		{
			UTexture* Tex = *It;

			if(Tex != this && Tex->CompositeTexture == this && Tex->CompositeTextureMode != CTM_Disabled)
			{
				TexturesThatUseThisTexture.Add(Tex);
			}
		}
		for (int32 i = 0; i < TexturesThatUseThisTexture.Num(); ++i)
		{
			TexturesThatUseThisTexture[i]->PostEditChange();
		}
	}
#endif

	for (UAssetUserData* Datum : AssetUserData)
	{
		if (Datum != nullptr)
		{
			Datum->PostEditChangeOwner();
		}
	}
}
#endif // WITH_EDITOR

void UTexture::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	FStripDataFlags StripFlags(Ar);

	/** Legacy serialization. */
#if WITH_EDITORONLY_DATA
	if (!StripFlags.IsEditorDataStripped())
	{
		Source.BulkData.Serialize(Ar, this);
	}

	if ( GetLinkerUE4Version() < VER_UE4_TEXTURE_LEGACY_GAMMA )
	{
		bUseLegacyGamma = true;
	}

	if (Ar.IsCooking() && VirtualTextureStreaming)
	{
		if (UseVirtualTexturing(GMaxRHIFeatureLevel, Ar.CookingTarget()) == false)
		{
			UE_LOG(LogTexture, Display, TEXT("%s is marked for virtual streaming but virtual texture streaming is not available."), *GetPathName());
		}
	}

#endif // #if WITH_EDITORONLY_DATA
}

void UTexture::PostInitProperties()
{
#if WITH_EDITORONLY_DATA
	if (!HasAnyFlags(RF_ClassDefaultObject | RF_NeedLoad))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}
#endif
	Super::PostInitProperties();
}

void UTexture::PostLoad()
{
	Super::PostLoad();

#if WITH_EDITORONLY_DATA
	if (AssetImportData == nullptr)
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}

	if (!SourceFilePath_DEPRECATED.IsEmpty())
	{
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(SourceFilePath_DEPRECATED));
		AssetImportData->SourceData = MoveTemp(Info);
	}
#endif

	if( !IsTemplate() )
	{
		// Update cached LOD bias.
		UpdateCachedLODBias();

		// The texture will be cached by the cubemap it is contained within on consoles.
		UTextureCube* CubeMap = Cast<UTextureCube>(GetOuter());
		if (CubeMap == NULL)
		{
			// Recreate the texture's resource.
			UpdateResource();
		}
	}
}

void UTexture::BeginDestroy()
{
	Super::BeginDestroy();
	if( !UpdateStreamingStatus() && (Resource || TextureReference.IsInitialized_GameThread()) )
	{
		// Send the rendering thread a release message for the texture's resource.
		if (Resource)
		{
			BeginReleaseResource(Resource);
		}
		if (TextureReference.IsInitialized_GameThread())
		{
			TextureReference.BeginRelease_GameThread();
		}
		ReleaseFence.BeginFence();
		// Keep track that we already kicked off the async release.
		bAsyncResourceReleaseHasBeenStarted = true;
	}
}

bool UTexture::IsReadyForFinishDestroy()
{
	bool bReadyForFinishDestroy = false;
	// Check whether super class is ready and whether we have any pending streaming requests in flight.
	if( Super::IsReadyForFinishDestroy() && !UpdateStreamingStatus() )
	{
		// Kick off async resource release if we haven't already.
		if( !bAsyncResourceReleaseHasBeenStarted && (Resource || TextureReference.IsInitialized_GameThread()) )
		{
			// Send the rendering thread a release message for the texture's resource.
			if (Resource)
			{
				BeginReleaseResource(Resource);
			}
			if (TextureReference.IsInitialized_GameThread())
			{
				TextureReference.BeginRelease_GameThread();
			}
			ReleaseFence.BeginFence();
			// Keep track that we already kicked off the async release.
			bAsyncResourceReleaseHasBeenStarted = true;
		}

		// Only allow FinishDestroy to be called once the texture resource has finished its rendering thread cleanup.
		if( !bAsyncResourceReleaseHasBeenStarted || ReleaseFence.IsFenceComplete() )
		{
			bReadyForFinishDestroy = true;
		}
	}
	return bReadyForFinishDestroy;
}

void UTexture::FinishDestroy()
{
	Super::FinishDestroy();

	check(!bAsyncResourceReleaseHasBeenStarted || ReleaseFence.IsFenceComplete());
	check(TextureReference.IsInitialized_GameThread() == false);

	if(Resource)
	{
		// Free the resource.
		delete Resource;
		Resource = NULL;
	}

	CleanupCachedRunningPlatformData();
#if WITH_EDITOR
	if (!GExitPurge)
	{
		ClearAllCachedCookedPlatformData();
	}
#endif
}

void UTexture::PreSave(const class ITargetPlatform* TargetPlatform)
{
	PreSaveEvent.Broadcast(this);

	Super::PreSave(TargetPlatform);

#if WITH_EDITOR
	if (DeferCompression)
	{
		GWarn->StatusUpdate( 0, 0, FText::Format( NSLOCTEXT("UnrealEd", "SavingPackage_CompressingTexture", "Compressing texture:  {0}"), FText::FromString(GetName()) ) );
		DeferCompression = false;
		UpdateResource();
	}

	bool bIsCooking = TargetPlatform != nullptr;
	if (!GEngine->IsAutosaving() && (!bIsCooking))
	{
		GWarn->StatusUpdate(0, 0, FText::Format(NSLOCTEXT("UnrealEd", "SavingPackage_CompressingSourceArt", "Compressing source art for texture:  {0}"), FText::FromString(GetName())));
		Source.Compress();
	}

#endif // #if WITH_EDITOR
}

#if WITH_EDITORONLY_DATA
void UTexture::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add( FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden) );
	}

	Super::GetAssetRegistryTags(OutTags);
}
#endif

float UTexture::GetAverageBrightness(bool bIgnoreTrueBlack, bool bUseGrayscale)
{
	// Indicate the action was not performed...
	return -1.0f;
}

/** Helper functions for text output of texture properties... */
#ifndef CASE_ENUM_TO_TEXT
#define CASE_ENUM_TO_TEXT(txt) case txt: return TEXT(#txt);
#endif

#ifndef TEXT_TO_ENUM
#define TEXT_TO_ENUM(eVal, txt)		if (FCString::Stricmp(TEXT(#eVal), txt) == 0)	return eVal;
#endif

const TCHAR* UTexture::GetTextureGroupString(TextureGroup InGroup)
{
	switch (InGroup)
	{
		FOREACH_ENUM_TEXTUREGROUP(CASE_ENUM_TO_TEXT)
	}

	return TEXT("TEXTUREGROUP_World");
}

const TCHAR* UTexture::GetMipGenSettingsString(TextureMipGenSettings InEnum)
{
	switch(InEnum)
	{
		default:
		FOREACH_ENUM_TEXTUREMIPGENSETTINGS(CASE_ENUM_TO_TEXT)
	}
}

TextureMipGenSettings UTexture::GetMipGenSettingsFromString(const TCHAR* InStr, bool bTextureGroup)
{
#define TEXT_TO_MIPGENSETTINGS(m) TEXT_TO_ENUM(m, InStr);
	FOREACH_ENUM_TEXTUREMIPGENSETTINGS(TEXT_TO_MIPGENSETTINGS)
#undef TEXT_TO_MIPGENSETTINGS

	// default for TextureGroup and Texture is different
	return bTextureGroup ? TMGS_SimpleAverage : TMGS_FromTextureGroup;
}

UEnum* UTexture::GetPixelFormatEnum()
{
	// Lookup the pixel format enum so that the pixel format can be serialized by name.
	static FName PixelFormatUnknownName(TEXT("PF_Unknown"));
	static UEnum* PixelFormatEnum = NULL;
	if (PixelFormatEnum == NULL)
	{
		check(IsInGameThread());
		UEnum::LookupEnumName(PixelFormatUnknownName, &PixelFormatEnum);
		check(PixelFormatEnum);
	}
	return PixelFormatEnum;
}

void UTexture::PostCDOContruct()
{
	GetPixelFormatEnum();
}


bool UTexture::ForceUpdateTextureStreaming()
{
	if (!IStreamingManager::HasShutdown())
	{
#if WITH_EDITOR
		for( TObjectIterator<UTexture2D> It; It; ++It )
		{
			UTexture* Texture = *It;

			// Update cached LOD bias.
			Texture->UpdateCachedLODBias();
		}
#endif // #if WITH_EDITOR

		// Make sure we iterate over all textures by setting it to high value.
		IStreamingManager::Get().SetNumIterationsForNextFrame( 100 );
		// Update resource streaming with updated texture LOD bias/ max texture mip count.
		IStreamingManager::Get().UpdateResourceStreaming( 0 );
		// Block till requests are finished.
		IStreamingManager::Get().BlockTillAllRequestsFinished();
	}

	return true;
}

void UTexture::AddAssetUserData(UAssetUserData* InUserData)
{
	if (InUserData != NULL)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if (ExistingData != NULL)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* UTexture::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != NULL && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return NULL;
}

void UTexture::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != NULL && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

const TArray<UAssetUserData*>* UTexture::GetAssetUserDataArray() const
{
	return &AssetUserData;
}


/*------------------------------------------------------------------------------
	Texture source data.
------------------------------------------------------------------------------*/

FTextureSource::FTextureSource()
	: LockedMipData(NULL)
	, NumLockedMips(0u)
#if WITH_EDITOR
	, bHasHadBulkDataCleared(false)
#endif
#if WITH_EDITORONLY_DATA
	, BaseBlockX(0)
	, BaseBlockY(0)
	, SizeX(0)
	, SizeY(0)
	, NumSlices(0)
	, NumMips(0)
	, NumLayers(1) // Default to 1 so old data has the correct value
	, bPNGCompressed(false)
	, bGuidIsHash(false)
	, Format(TSF_Invalid)
#endif // WITH_EDITORONLY_DATA
{
}

FTextureSourceBlock::FTextureSourceBlock()
	: BlockX(0)
	, BlockY(0)
	, SizeX(0)
	, SizeY(0)
	, NumSlices(0)
	, NumMips(0)
{
}

#if WITH_EDITOR

void FTextureSource::InitBlocked(const ETextureSourceFormat* InLayerFormats,
	const FTextureSourceBlock* InBlocks,
	int32 InNumLayers,
	int32 InNumBlocks,
	const uint8** InDataPerBlock)
{
	check(InNumBlocks > 0);
	check(InNumLayers > 0);

	RemoveSourceData();

	BaseBlockX = InBlocks[0].BlockX;
	BaseBlockY = InBlocks[0].BlockY;
	SizeX = InBlocks[0].SizeX;
	SizeY = InBlocks[0].SizeY;
	NumSlices = InBlocks[0].NumSlices;
	NumMips = InBlocks[0].NumMips;

	NumLayers = InNumLayers;
	Format = InLayerFormats[0];

	Blocks.Reserve(InNumBlocks - 1);
	for (int32 BlockIndex = 1; BlockIndex < InNumBlocks; ++BlockIndex)
	{
		Blocks.Add(InBlocks[BlockIndex]);
	}

	LayerFormat.SetNum(InNumLayers, true);
	for (int i = 0; i < InNumLayers; ++i)
	{
		LayerFormat[i] = InLayerFormats[i];
	}

	int64 TotalBytes = 0;
	for (int i = 0; i < InNumBlocks; ++i)
	{
		TotalBytes += CalcBlockSize(i);
	}

	BulkData.Lock(LOCK_READ_WRITE);
	uint8* DestData = (uint8*)BulkData.Realloc(TotalBytes);
	if (InDataPerBlock)
	{
		for (int i = 0; i < InNumBlocks; ++i)
		{
			const int64 BlockSize = CalcBlockSize(i);
			if (InDataPerBlock[i])
			{
				FMemory::Memcpy(DestData, InDataPerBlock[i], BlockSize);
			}
			DestData += BlockSize;
		}
	}
	BulkData.Unlock();
}

void FTextureSource::InitLayered(
	int32 NewSizeX,
	int32 NewSizeY,
	int32 NewNumSlices,
	int32 NewNumLayers,
	int32 NewNumMips,
	const ETextureSourceFormat* NewLayerFormat,
	const uint8* NewData)
{
	RemoveSourceData();
	SizeX = NewSizeX;
	SizeY = NewSizeY;
	NumLayers = NewNumLayers;
	NumSlices = NewNumSlices;
	NumMips = NewNumMips;
	Format = NewLayerFormat[0];
	LayerFormat.SetNum(NewNumLayers, true);
	for (int i = 0; i < NewNumLayers; ++i)
	{
		LayerFormat[i] = NewLayerFormat[i];
	}

	int64 TotalBytes = 0;
	for (int i = 0; i < NewNumLayers; ++i)
	{
		TotalBytes += CalcLayerSize(0, i);
	}

	BulkData.Lock(LOCK_READ_WRITE);
	uint8* DestData = (uint8*)BulkData.Realloc(TotalBytes);
	if (NewData)
	{
		FMemory::Memcpy(DestData, NewData, TotalBytes);
	}
	BulkData.Unlock();
}

void FTextureSource::Init(
		int32 NewSizeX,
		int32 NewSizeY,
		int32 NewNumSlices,
		int32 NewNumMips,
		ETextureSourceFormat NewFormat,
		const uint8* NewData
		)
{
	InitLayered(NewSizeX, NewSizeY, NewNumSlices, 1, NewNumMips, &NewFormat, NewData);
}

void FTextureSource::Init2DWithMipChain(
	int32 NewSizeX,
	int32 NewSizeY,
	ETextureSourceFormat NewFormat
	)
{
	int32 NewMipCount = FMath::Max(FMath::CeilLogTwo(NewSizeX),FMath::CeilLogTwo(NewSizeY)) + 1;
	Init(NewSizeX, NewSizeY, 1, NewMipCount, NewFormat);
}

void FTextureSource::InitLayered2DWithMipChain(
	int32 NewSizeX,
	int32 NewSizeY,
	int32 NewNumLayers,
	const ETextureSourceFormat* NewFormat)
{
	int32 NewMipCount = FMath::Max(FMath::CeilLogTwo(NewSizeX), FMath::CeilLogTwo(NewSizeY)) + 1;
	InitLayered(NewSizeX, NewSizeY, 1, NewNumLayers, NewMipCount, NewFormat);
}

void FTextureSource::InitCubeWithMipChain(
	int32 NewSizeX,
	int32 NewSizeY,
	ETextureSourceFormat NewFormat
	)
{
	int32 NewMipCount = FMath::Max(FMath::CeilLogTwo(NewSizeX),FMath::CeilLogTwo(NewSizeY)) + 1;
	Init(NewSizeX, NewSizeY, 6, NewMipCount, NewFormat);
}

void FTextureSource::Compress()
{
	if (CanPNGCompress())
	{
		uint8* BulkDataPtr = (uint8*)BulkData.Lock(LOCK_READ_WRITE);
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>( FName("ImageWrapper") );
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper( EImageFormat::PNG );
		// TODO: TSF_BGRA8 is stored as RGBA, so the R and B channels are swapped in the internal png. Should we fix this?
		ERGBFormat RawFormat = (Format == TSF_G8 || Format == TSF_G16) ? ERGBFormat::Gray : ERGBFormat::RGBA;
		if ( ImageWrapper.IsValid() && ImageWrapper->SetRaw( BulkDataPtr, BulkData.GetBulkDataSize(), SizeX, SizeY, RawFormat, (Format == TSF_G16 || Format == TSF_RGBA16) ? 16 : 8 ) )
		{
			const TArray64<uint8>& CompressedData = ImageWrapper->GetCompressed();
			if ( CompressedData.Num() > 0 )
			{
				BulkDataPtr = (uint8*)BulkData.Realloc(CompressedData.Num());
				FMemory::Memcpy(BulkDataPtr, CompressedData.GetData(), CompressedData.Num());
				BulkData.Unlock();
				bPNGCompressed = true;

				BulkData.StoreCompressedOnDisk(NAME_None);
			}
		}
	}
	else
	{
		//Can't PNG compress so just zlib compress the lot when its serialized out to disk. 
		BulkData.StoreCompressedOnDisk(NAME_Zlib);
	}
}

uint8* FTextureSource::LockMip(int32 BlockIndex, int32 LayerIndex, int32 MipIndex)
{
	uint8* MipData = NULL;
	if (BlockIndex < GetNumBlocks() && LayerIndex < NumLayers && MipIndex < NumMips)
	{
		if (LockedMipData == NULL)
		{
			LockedMipData = (uint8*)BulkData.Lock(LOCK_READ_WRITE);
			if (bPNGCompressed)
			{
				bool bCanPngCompressFormat = (Format == TSF_G8 || Format == TSF_G16 || Format == TSF_RGBA8 || Format == TSF_BGRA8 || Format == TSF_RGBA16);
				check(Blocks.Num() == 0 && NumLayers == 1 && NumSlices == 1 && bCanPngCompressFormat);
				if (MipIndex != 0)
				{
					return NULL;
				}

				IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>( FName("ImageWrapper") );
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper( EImageFormat::PNG );
				if ( ImageWrapper.IsValid() && ImageWrapper->SetCompressed( LockedMipData, BulkData.GetBulkDataSize() ) )
				{
					check( ImageWrapper->GetWidth() == SizeX );
					check( ImageWrapper->GetHeight() == SizeY );
					TArray64<uint8> RawData;
					// TODO: TSF_BGRA8 is stored as RGBA, so the R and B channels are swapped in the internal png. Should we fix this?
					ERGBFormat RawFormat = (Format == TSF_G8 || Format == TSF_G16) ? ERGBFormat::Gray : ERGBFormat::RGBA;
					if (ImageWrapper->GetRaw( RawFormat, (Format == TSF_G16 || Format == TSF_RGBA16) ? 16 : 8, RawData ) && RawData.Num() > 0)
					{
						LockedMipData = (uint8*)FMemory::Malloc(RawData.Num());
						FMemory::Memcpy(LockedMipData, RawData.GetData(), RawData.Num());
					}
					else
					{
						UE_LOG(LogTexture, Warning, TEXT("PNG decompression of source art failed"));
					}
				}
				else
				{
					UE_LOG(LogTexture, Log, TEXT("Only pngs are supported"));
				}
			}
		}

		MipData = LockedMipData + CalcMipOffset(BlockIndex, LayerIndex, MipIndex);
		++NumLockedMips;
	}
	return MipData;
}

void FTextureSource::UnlockMip(int32 BlockIndex, int32 LayerIndex, int32 MipIndex)
{
	check(BlockIndex < GetNumBlocks());
	check(LayerIndex < NumLayers);
	check(MipIndex < MAX_TEXTURE_MIP_COUNT);

	check(NumLockedMips > 0u);
	--NumLockedMips;
	if (NumLockedMips == 0u)
	{
		if (bPNGCompressed)
		{
			check(BlockIndex == 0);
			check(LayerIndex == 0);
			check(MipIndex == 0);
			int32 MipSize = CalcMipSize(0, 0, 0);
			uint8* UncompressedData = (uint8*)BulkData.Realloc(MipSize);
			FMemory::Memcpy(UncompressedData, LockedMipData, MipSize);
			FMemory::Free(LockedMipData);
			bPNGCompressed = false;
		}
		LockedMipData = NULL;
		BulkData.Unlock();
		ForceGenerateGuid();
	}
}

bool FTextureSource::GetMipData(TArray64<uint8>& OutMipData, int32 BlockIndex, int32 LayerIndex, int32 MipIndex, IImageWrapperModule* ImageWrapperModule)
{
	bool bSuccess = false;
	if (BlockIndex < GetNumBlocks() && LayerIndex < NumLayers && MipIndex < NumMips && BulkData.GetBulkDataSize() > 0)
	{
		void* RawSourceData = BulkData.Lock(LOCK_READ_ONLY);
		if (bPNGCompressed)
		{
			bool bCanPngCompressFormat = (Format == TSF_G8 || Format == TSF_G16 || Format == TSF_RGBA8 || Format == TSF_BGRA8 || Format == TSF_RGBA16);
			if (MipIndex == 0 && NumLayers == 1 && NumSlices == 1 && Blocks.Num() == 0 && bCanPngCompressFormat)
			{
				if (!ImageWrapperModule) // Optional if called from the gamethread, see FModuleManager::WarnIfItWasntSafeToLoadHere()
				{
					ImageWrapperModule = &FModuleManager::LoadModuleChecked<IImageWrapperModule>( FName("ImageWrapper") );
				}

				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule->CreateImageWrapper( EImageFormat::PNG );

				if ( ImageWrapper.IsValid() && ImageWrapper->SetCompressed( RawSourceData, BulkData.GetBulkDataSize() ) )
				{
					if (ImageWrapper->GetWidth() == SizeX
						&& ImageWrapper->GetHeight() == SizeY)
					{
						// TODO: TSF_BGRA8 is stored as RGBA, so the R and B channels are swapped in the internal png. Should we fix this?
						ERGBFormat RawFormat = (Format == TSF_G8 || Format == TSF_G16) ? ERGBFormat::Gray : ERGBFormat::RGBA;
						if (ImageWrapper->GetRaw( RawFormat, (Format == TSF_G16 || Format == TSF_RGBA16) ? 16 : 8, OutMipData ))
						{
							bSuccess = true;
						}
						else
						{
							UE_LOG(LogTexture, Warning, TEXT("PNG decompression of source art failed"));
							OutMipData.Empty();
						}
					}
					else
					{
						UE_LOG(LogTexture, Warning,
							TEXT("PNG decompression of source art failed. ")
							TEXT("Source image should be %dx%d but is %dx%d"),
							SizeX, SizeY,
							ImageWrapper->GetWidth(), ImageWrapper->GetHeight()
							);
					}
				}
				else
				{
					UE_LOG(LogTexture, Log, TEXT("Only pngs are supported"));
				}
			}
		}
		else
		{
			int64 MipOffset = CalcMipOffset(BlockIndex, LayerIndex, MipIndex);
			int64 MipSize = CalcMipSize(BlockIndex, LayerIndex, MipIndex);
			if (BulkData.GetBulkDataSize() >= MipOffset + MipSize)
			{
				OutMipData.Empty(MipSize);
				OutMipData.AddUninitialized(MipSize);
				FMemory::Memcpy(
					OutMipData.GetData(),
					(uint8*)RawSourceData + MipOffset,
					MipSize
					);
			}
			bSuccess = true;
		}
		BulkData.Unlock();
	}
	return bSuccess;
}

int64 FTextureSource::CalcMipSize(int32 BlockIndex, int32 LayerIndex, int32 MipIndex) const
{
	FTextureSourceBlock Block;
	GetBlock(BlockIndex, Block);
	check(MipIndex < Block.NumMips);

	const int64 MipSizeX = FMath::Max(Block.SizeX >> MipIndex, 1);
	const int64 MipSizeY = FMath::Max(Block.SizeY >> MipIndex, 1);
	const int64 BytesPerPixel = GetBytesPerPixel(LayerIndex);
	return MipSizeX * MipSizeY * Block.NumSlices * BytesPerPixel;
}

int32 FTextureSource::GetBytesPerPixel(ETextureSourceFormat Format)
{
	int32 BytesPerPixel = 0;
	switch (Format)
	{
	case TSF_G8:		BytesPerPixel = 1; break;
	case TSF_G16:		BytesPerPixel = 2; break;
	case TSF_BGRA8:		BytesPerPixel = 4; break;
	case TSF_BGRE8:		BytesPerPixel = 4; break;
	case TSF_RGBA16:	BytesPerPixel = 8; break;
	case TSF_RGBA16F:	BytesPerPixel = 8; break;
	default:			BytesPerPixel = 0; break;
	}
	return BytesPerPixel;
}

int32 FTextureSource::GetBytesPerPixel(int32 LayerIndex) const
{
	return GetBytesPerPixel(GetFormat(LayerIndex));
}

bool FTextureSource::IsPowerOfTwo(int32 BlockIndex) const
{
	FTextureSourceBlock Block;
	GetBlock(BlockIndex, Block);
	return FMath::IsPowerOfTwo(Block.SizeX) && FMath::IsPowerOfTwo(Block.SizeY);
}

bool FTextureSource::IsValid() const
{
	return SizeX > 0 && SizeY > 0 && NumSlices > 0 && NumLayers > 0 && NumMips > 0 &&
		Format != TSF_Invalid && BulkData.GetBulkDataSize() > 0;
}

void FTextureSource::GetBlock(int32 Index, FTextureSourceBlock& OutBlock) const
{
	check(Index < GetNumBlocks());
	if (Index == 0)
	{
		OutBlock.BlockX = BaseBlockX;
		OutBlock.BlockY = BaseBlockY;
		OutBlock.SizeX = SizeX;
		OutBlock.SizeY = SizeY;
		OutBlock.NumSlices = NumSlices;
		OutBlock.NumMips = NumMips;
	}
	else
	{
		OutBlock = Blocks[Index - 1];
	}
}

FIntPoint FTextureSource::GetLogicalSize() const
{
	const int32 NumBlocks = GetNumBlocks();
	int32 SizeInBlocksX = 0;
	int32 SizeInBlocksY = 0;
	int32 BlockSizeX = 0;
	int32 BlockSizeY = 0;
	for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
	{
		FTextureSourceBlock SourceBlock;
		GetBlock(BlockIndex, SourceBlock);
		SizeInBlocksX = FMath::Max(SizeInBlocksX, SourceBlock.BlockX + 1);
		SizeInBlocksY = FMath::Max(SizeInBlocksY, SourceBlock.BlockY + 1);
		BlockSizeX = FMath::Max(BlockSizeX, SourceBlock.SizeX);
		BlockSizeY = FMath::Max(BlockSizeY, SourceBlock.SizeY);
	}
	return FIntPoint(SizeInBlocksX * BlockSizeX, SizeInBlocksY * BlockSizeY);
}

FIntPoint FTextureSource::GetSizeInBlocks() const
{
	const int32 NumBlocks = GetNumBlocks();
	int32 SizeInBlocksX = 0;
	int32 SizeInBlocksY = 0;
	for (int32 BlockIndex = 0; BlockIndex < NumBlocks; ++BlockIndex)
	{
		FTextureSourceBlock SourceBlock;
		GetBlock(BlockIndex, SourceBlock);
		SizeInBlocksX = FMath::Max(SizeInBlocksX, SourceBlock.BlockX + 1);
		SizeInBlocksY = FMath::Max(SizeInBlocksY, SourceBlock.BlockY + 1);
	}
	return FIntPoint(SizeInBlocksX, SizeInBlocksY);
}

FString FTextureSource::GetIdString() const
{
	FString GuidString = Id.ToString();
	if (bGuidIsHash)
	{
		GuidString += TEXT("X");
	}
	return GuidString;
}

bool FTextureSource::CanPNGCompress() const
{
	bool bCanPngCompressFormat = (Format == TSF_G8 || Format == TSF_G16 || Format == TSF_RGBA8 || Format == TSF_BGRA8 || Format == TSF_RGBA16);

	if (!bPNGCompressed &&
		NumLayers == 1 &&
		NumMips == 1 &&
		NumSlices == 1 &&
		Blocks.Num() == 0 &&
		SizeX > 4 &&
		SizeY > 4 &&
		BulkData.GetBulkDataSize() > 0 &&
		bCanPngCompressFormat)
	{
		return true;
	}
	return false;
}

void FTextureSource::ForceGenerateGuid()
{
	Id = FGuid::NewGuid();
	bGuidIsHash = false;
}

void FTextureSource::ReleaseSourceMemory()
{
	bHasHadBulkDataCleared = true;
	if (BulkData.IsLocked())
	{
		BulkData.Unlock();
	}
	BulkData.RemoveBulkData();
}

void FTextureSource::RemoveSourceData()
{
	SizeX = 0;
	SizeY = 0;
	NumSlices = 0;
	NumLayers = 0;
	NumMips = 0;
	Format = TSF_Invalid;
	LayerFormat.Empty();
	Blocks.Empty();
	bPNGCompressed = false;
	LockedMipData = NULL;
	NumLockedMips = 0u;
	if (BulkData.IsLocked())
	{
		BulkData.Unlock();
	}
	BulkData.RemoveBulkData();
	ForceGenerateGuid();
}

int64 FTextureSource::CalcBlockSize(int32 BlockIndex) const
{
	int64 TotalSize = 0;
	for (int32 LayerIndex = 0; LayerIndex < GetNumLayers(); ++LayerIndex)
	{
		TotalSize += CalcLayerSize(BlockIndex, LayerIndex);
	}
	return TotalSize;
}

int64 FTextureSource::CalcLayerSize(int32 BlockIndex, int32 LayerIndex) const
{
	FTextureSourceBlock Block;
	GetBlock(BlockIndex, Block);

	int64 BytesPerPixel = GetBytesPerPixel(LayerIndex);
	int64 MipSizeX = Block.SizeX;
	int64 MipSizeY = Block.SizeY;

	int64 TotalSize = 0;
	for(int32 MipIndex = 0; MipIndex < Block.NumMips; ++MipIndex)
	{
		TotalSize += MipSizeX * MipSizeY * BytesPerPixel * Block.NumSlices;
		MipSizeX = FMath::Max<int64>(MipSizeX >> 1, 1);
		MipSizeY = FMath::Max<int64>(MipSizeY >> 1, 1);
	}
	return TotalSize;
}

int64 FTextureSource::CalcMipOffset(int32 BlockIndex, int32 LayerIndex, int32 MipIndex) const
{
	int64 MipOffset = 0;

	// Skip over the initial tiles
	for (int i = 0; i < BlockIndex; ++i)
	{
		MipOffset += CalcBlockSize(i);
	}

	// Skip over the initial layers within the tile
	for (int i = 0; i < LayerIndex; ++i)
	{
		MipOffset += CalcLayerSize(BlockIndex, i);
	}

	FTextureSourceBlock Block;
	GetBlock(BlockIndex, Block);
	check(MipIndex < Block.NumMips);
	
	int64 BytesPerPixel = GetBytesPerPixel(LayerIndex);
	int64 MipSizeX = Block.SizeX;
	int64 MipSizeY = Block.SizeY;

	while (MipIndex-- > 0)
	{
		MipOffset += MipSizeX * MipSizeY * BytesPerPixel * Block.NumSlices;
		MipSizeX = FMath::Max<int64>(MipSizeX >> 1, 1);
		MipSizeY = FMath::Max<int64>(MipSizeY >> 1, 1);
	}

	return MipOffset;
}

void FTextureSource::UseHashAsGuid()
{
	uint32 Hash[5];

	if (BulkData.GetBulkDataSize() > 0)
	{
		bGuidIsHash = true;
		void* Buffer = BulkData.Lock(LOCK_READ_ONLY);
		FSHA1::HashBuffer(Buffer, BulkData.GetBulkDataSize(), (uint8*)Hash);
		BulkData.Unlock();
		Id = FGuid(Hash[0] ^ Hash[4], Hash[1], Hash[2], Hash[3]);
	}
}

void FTextureSource::SetId(const FGuid& InId, bool bInGuidIsHash)
{
	Id = InId;
	bGuidIsHash = bInGuidIsHash;
}

uint32 UTexture::GetMaximumDimension() const
{
	return GetMax2DTextureDimension();
}

void UTexture::GetDefaultFormatSettings(FTextureFormatSettings& OutSettings) const
{
	OutSettings.CompressionSettings = CompressionSettings;
	OutSettings.CompressionNone = CompressionNone;
	OutSettings.CompressionNoAlpha = CompressionNoAlpha;
	OutSettings.CompressionYCoCg = CompressionYCoCg;
	OutSettings.SRGB = SRGB;
}

void UTexture::GetLayerFormatSettings(int32 LayerIndex, FTextureFormatSettings& OutSettings) const
{
	check(LayerIndex >= 0);
	if (LayerIndex < LayerFormatSettings.Num())
	{
		OutSettings = LayerFormatSettings[LayerIndex];
	}
	else
	{
		GetDefaultFormatSettings(OutSettings);
	}
}

void UTexture::SetLayerFormatSettings(int32 LayerIndex, const FTextureFormatSettings& InSettings)
{
	check(LayerIndex >= 0);
	if (LayerIndex == 0 && LayerFormatSettings.Num() == 0)
	{
		// Apply layer0 settings directly to texture properties
		CompressionSettings = InSettings.CompressionSettings;
		CompressionNone = InSettings.CompressionNone;
		CompressionNoAlpha = InSettings.CompressionNoAlpha;
		CompressionYCoCg = InSettings.CompressionYCoCg;
		SRGB = InSettings.SRGB;
	}
	else
	{
		if (LayerIndex >= LayerFormatSettings.Num())
		{
			FTextureFormatSettings DefaultSettings;
			GetDefaultFormatSettings(DefaultSettings);
			LayerFormatSettings.Reserve(LayerIndex + 1);
			while (LayerIndex >= LayerFormatSettings.Num())
			{
				LayerFormatSettings.Add(DefaultSettings);
			}
		}
		LayerFormatSettings[LayerIndex] = InSettings;
	}
}

#endif // #if WITH_EDITOR

FName GetDefaultTextureFormatName( const ITargetPlatform* TargetPlatform, const UTexture* Texture, int32 LayerIndex, const FConfigFile& EngineSettings, bool bSupportDX11TextureFormats, bool bSupportCompressedVolumeTexture, int32 BlockSize )
{
	FName TextureFormatName = NAME_None;

	/**
	 * IF you add a format to this function don't forget to update GetAllDefaultTextureFormatNames 
	 */

#if WITH_EDITOR
	// Supported texture format names.
	static FName NameDXT1(TEXT("DXT1"));
	static FName NameDXT3(TEXT("DXT3"));
	static FName NameDXT5(TEXT("DXT5"));
	static FName NameDXT5n(TEXT("DXT5n"));
	static FName NameAutoDXT(TEXT("AutoDXT"));
	static FName NameBC4(TEXT("BC4"));
	static FName NameBC5(TEXT("BC5"));
	static FName NameBGRA8(TEXT("BGRA8"));
	static FName NameXGXR8(TEXT("XGXR8"));
	static FName NameG8(TEXT("G8"));
	static FName NameG16(TEXT("G16"));
	static FName NameVU8(TEXT("VU8"));
	static FName NameRGBA16F(TEXT("RGBA16F"));
	static FName NameBC6H(TEXT("BC6H"));
	static FName NameBC7(TEXT("BC7"));

	check(TargetPlatform);

	static const auto CVarVirtualTexturesEnabled = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.VirtualTextures")); check(CVarVirtualTexturesEnabled);
	const bool bVirtualTextureStreaming = CVarVirtualTexturesEnabled->GetValueOnAnyThread() && TargetPlatform->SupportsFeature(ETargetPlatformFeatures::VirtualTextureStreaming) && Texture->VirtualTextureStreaming;

	FTextureFormatSettings FormatSettings;
	Texture->GetLayerFormatSettings(LayerIndex, FormatSettings);

	bool bNoCompression = FormatSettings.CompressionNone				// Code wants the texture uncompressed.
		|| (TargetPlatform->HasEditorOnlyData() && Texture->DeferCompression)	// The user wishes to defer compression, this is ok for the Editor only.
		|| (FormatSettings.CompressionSettings == TC_EditorIcon)
		|| (Texture->LODGroup == TEXTUREGROUP_ColorLookupTable)	// Textures in certain LOD groups should remain uncompressed.
		|| (Texture->LODGroup == TEXTUREGROUP_Bokeh)
		|| (Texture->LODGroup == TEXTUREGROUP_IESLightProfile)
		|| (Texture->GetMaterialType() == MCT_VolumeTexture && !bSupportCompressedVolumeTexture);

	if (!bNoCompression && Texture->PowerOfTwoMode == ETexturePowerOfTwoSetting::None)
	{
		uint32 SizeX = Texture->Source.GetSizeX();
		uint32 SizeY = Texture->Source.GetSizeY();
#if WITH_EDITORONLY_DATA
		const UTextureLODSettings& LODSettings = TargetPlatform->GetTextureLODSettings();
 		const uint32 LODBiasNoCinematics = FMath::Max<uint32>(LODSettings.CalculateLODBias(SizeX, SizeY, Texture->MaxTextureSize, Texture->LODGroup, Texture->LODBias, 0, Texture->MipGenSettings, bVirtualTextureStreaming), 0);
		SizeX = FMath::Max<uint32>(SizeX >> LODBiasNoCinematics, 1);
		SizeY = FMath::Max<uint32>(SizeY >> LODBiasNoCinematics, 1);
#endif
		// Don't compress textures smaller than the DXT block size.
		bNoCompression |= (SizeX < 4) || (SizeY < 4) || (SizeX % 4 != 0) || (SizeY % 4 != 0);
	}

	bool bUseDXT5NormalMap = false;

	FString UseDXT5NormalMapsString;

	if (EngineSettings.GetString(TEXT("SystemSettings"), TEXT("Compat.UseDXT5NormalMaps"), UseDXT5NormalMapsString))
	{
		bUseDXT5NormalMap = FCString::ToBool(*UseDXT5NormalMapsString);
	}

	const ETextureSourceFormat SourceFormat = Texture->Source.GetFormat(LayerIndex);

	// Determine the pixel format of the (un/)compressed texture
	if (bNoCompression)
	{
		if (Texture->HasHDRSource(LayerIndex))
		{
			TextureFormatName = NameRGBA16F;
		}
		else if (SourceFormat == TSF_G16)
		{
			TextureFormatName = NameG16;
		}
		else if (SourceFormat == TSF_G8 || FormatSettings.CompressionSettings == TC_Grayscale)
		{
			TextureFormatName = NameG8;
		}
		else if (FormatSettings.CompressionSettings == TC_Normalmap && bUseDXT5NormalMap)
		{
			TextureFormatName = NameXGXR8;
		}
		else
		{
			TextureFormatName = NameBGRA8;
		}
	}
	else if (FormatSettings.CompressionSettings == TC_HDR)
	{
		TextureFormatName = NameRGBA16F;
	}
	else if (FormatSettings.CompressionSettings == TC_Normalmap)
	{
		TextureFormatName = bUseDXT5NormalMap ? NameDXT5n : NameBC5;
	}
	else if (FormatSettings.CompressionSettings == TC_Displacementmap)
	{
		TextureFormatName = NameG8;
	}
	else if (FormatSettings.CompressionSettings == TC_VectorDisplacementmap)
	{
		TextureFormatName = NameBGRA8;
	}
	else if (FormatSettings.CompressionSettings == TC_Grayscale)
	{
		TextureFormatName = NameG8;
	}
	else if ( FormatSettings.CompressionSettings == TC_Alpha)
	{
		TextureFormatName = NameBC4;
	}
	else if (FormatSettings.CompressionSettings == TC_DistanceFieldFont)
	{
		TextureFormatName = NameG8;
	}
	else if ( FormatSettings.CompressionSettings == TC_HDR_Compressed )
	{
		TextureFormatName = NameBC6H;
	}
	else if ( FormatSettings.CompressionSettings == TC_BC7 )
	{
		TextureFormatName = NameBC7;
	}
	else if (FormatSettings.CompressionNoAlpha)
	{
		TextureFormatName = NameDXT1;
	}
	else if (Texture->bDitherMipMapAlpha)
	{
		TextureFormatName = NameDXT5;
	}
	else
	{
		TextureFormatName = NameAutoDXT;
	}

	// Some PC GPUs don't support sRGB read from G8 textures (e.g. AMD DX10 cards on ShaderModel3.0)
	// This solution requires 4x more memory but a lot of PC HW emulate the format anyway
	if ((TextureFormatName == NameG8) && FormatSettings.SRGB && !TargetPlatform->SupportsFeature(ETargetPlatformFeatures::GrayscaleSRGB))
	{
		TextureFormatName = NameBGRA8;
	}

	// fallback to non-DX11 formats if one was chosen, but we can't use it
	if (!bSupportDX11TextureFormats)
	{
		if (TextureFormatName == NameBC6H)
		{
			TextureFormatName = NameRGBA16F;
		}
		else if (TextureFormatName == NameBC7)
		{
			TextureFormatName = NameBGRA8;
		}
	}

#endif //WITH_EDITOR

	return TextureFormatName;
}

void GetDefaultTextureFormatNamePerLayer(TArray<FName>& OutFormatNames, const class ITargetPlatform* TargetPlatform, const class UTexture* Texture, const class FConfigFile& EngineSettings, bool bSupportDX11TextureFormats, bool bSupportCompressedVolumeTexture, int32 BlockSize)
{
#if WITH_EDITOR
	OutFormatNames.Reserve(Texture->Source.GetNumLayers());
	for (int32 LayerIndex = 0; LayerIndex < Texture->Source.GetNumLayers(); ++LayerIndex)
	{
		OutFormatNames.Add(GetDefaultTextureFormatName(TargetPlatform, Texture, LayerIndex, EngineSettings, bSupportDX11TextureFormats, bSupportCompressedVolumeTexture, BlockSize));
	}
#endif // WITH_EDITOR
}

void GetAllDefaultTextureFormats(const class ITargetPlatform* TargetPlatform, TArray<FName>& OutFormats, bool bSupportDX11TextureFormats)
{
#if WITH_EDITOR
	static FName NameDXT1(TEXT("DXT1"));
	static FName NameDXT3(TEXT("DXT3"));
	static FName NameDXT5(TEXT("DXT5"));
	static FName NameDXT5n(TEXT("DXT5n"));
	static FName NameAutoDXT(TEXT("AutoDXT"));
	static FName NameBC4(TEXT("BC4"));
	static FName NameBC5(TEXT("BC5"));
	static FName NameBGRA8(TEXT("BGRA8"));
	static FName NameXGXR8(TEXT("XGXR8"));
	static FName NameG8(TEXT("G8"));
	static FName NameVU8(TEXT("VU8"));
	static FName NameRGBA16F(TEXT("RGBA16F"));
	static FName NameBC6H(TEXT("BC6H"));
	static FName NameBC7(TEXT("BC7"));

	OutFormats.Add(NameDXT1);
	OutFormats.Add(NameDXT3);
	OutFormats.Add(NameDXT5);
	OutFormats.Add(NameDXT5n);
	OutFormats.Add(NameAutoDXT);
	OutFormats.Add(NameBC4);
	OutFormats.Add(NameBC5);
	OutFormats.Add(NameBGRA8);
	OutFormats.Add(NameXGXR8);
	OutFormats.Add(NameG8);
	OutFormats.Add(NameVU8);
	OutFormats.Add(NameRGBA16F);
	if (bSupportDX11TextureFormats)
	{
		OutFormats.Add(NameBC6H);
		OutFormats.Add(NameBC7);
	}
#endif
}

#if WITH_EDITOR

void UTexture::NotifyMaterials()
{
	// Create a material update context to safely update materials.
	{
		FMaterialUpdateContext UpdateContext;

		// Notify any material that uses this texture
		TSet<UMaterial*> BaseMaterialsThatUseThisTexture;
		for (TObjectIterator<UMaterialInterface> It; It; ++It)
		{
			UMaterialInterface* MaterialInterface = *It;
			if (DoesMaterialUseTexture(MaterialInterface, this))
			{
				UpdateContext.AddMaterialInterface(MaterialInterface);
				// This is a bit tricky. We want to make sure all materials using this texture are
				// updated. Materials are always updated. Material instances may also have to be
				// updated and if they have static permutations their children must be updated
				// whether they use the texture or not! The safe thing to do is to add the instance's
				// base material to the update context causing all materials in the tree to update.
				BaseMaterialsThatUseThisTexture.Add(MaterialInterface->GetMaterial());
			}
		}

		// Go ahead and update any base materials that need to be.
		for (TSet<UMaterial*>::TConstIterator It(BaseMaterialsThatUseThisTexture); It; ++It)
		{
			(*It)->PostEditChange();
		}
	}
}

#endif //WITH_EDITOR
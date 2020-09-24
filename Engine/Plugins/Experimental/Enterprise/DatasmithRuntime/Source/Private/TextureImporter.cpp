// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneImporter.h"

#include "IESLoader.h"
#include "DatasmithRuntimeUtils.h"
#include "LogCategory.h"

#include "DatasmithMaterialElements.h"
#include "DatasmithUtils.h"
#include "IDatasmithSceneElements.h"

#include "Async/Async.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureLightProfile.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

namespace DatasmithRuntime
{
	using FDataCleanupFunc = TFunction<void(uint8*, const FUpdateTextureRegion2D*)>;

	bool GetTextureData(const TCHAR* Filename, FTextureData& TextureData)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DatasmithRuntime::GetTextureData);

		TArray<uint8> Buffer;
		if (!(FFileHelper::LoadFileToArray(Buffer, Filename) && Buffer.Num() > 0))
		{
			return false;
		}

		// checks for .IES extension to avoid wasting loading large assets just to reject them during header parsing
		FIESLoadHelper IESLoadHelper(Buffer.GetData(), Buffer.Num());
		Buffer.Empty();

		if(IESLoadHelper.IsValid())
		{
			TArray<uint8> RAWData;
			TextureData.TextureMultiplier = IESLoadHelper.ExtractInRGBA16F(RAWData);

			TextureData.Width = IESLoadHelper.GetWidth();
			TextureData.Height = IESLoadHelper.GetHeight();
			TextureData.Brightness = IESLoadHelper.GetBrightness();
			TextureData.BytesPerPixel = 8; // RGBA16F
			TextureData.Pitch = TextureData.Width * TextureData.BytesPerPixel;

			TextureData.ImageData = (uint8*)FMemory::Malloc(RAWData.Num() * sizeof(uint8), 0x20);
			FMemory::Memcpy(TextureData.ImageData, RAWData.GetData(), RAWData.Num() * sizeof(uint8));

			return true;
		}

		return false;
	}

	bool CreateImageTexture(FTextureData& TextureData, IDatasmithTextureElement* TextureElement, FDataCleanupFunc& DataCleanupFunc)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CreateImageTexture);

		// Texture2D setup
		UTexture2D* Texture2D = TextureData.GetObject<UTexture2D>();
			
		if (Texture2D == nullptr)
		{
			Texture2D = UTexture2D::CreateTransient(TextureData.Width, TextureData.Height, (EPixelFormat)TextureData.Requirements);
			check(Texture2D);

#ifdef ASSET_DEBUG
			FString BaseName = FPaths::GetBaseFilename(TextureElement->GetFile());
			FString TextureName = BaseName + TEXT("_LU_") + FString::FromInt(TextureData.ElementId);
			TextureName = FDatasmithUtils::SanitizeObjectName(TextureName);
			UPackage* Package = CreatePackage(*FPaths::Combine( TEXT("/Engine/Transient/LU"), TextureName));
			Texture2D->Rename(*TextureName, Package, REN_DontCreateRedirectors | REN_NonTransactional);
			Texture2D->SetFlags(RF_Public);
#endif

			TextureData.Object = TStrongObjectPtr<UObject>(Texture2D);
		}

#if WITH_EDITORONLY_DATA
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(TextureElement->GetFile()));
		Texture2D->AssetImportData->SourceData = MoveTemp(Info);

		const float RGBCurve = TextureElement->GetRGBCurve();
		if (FMath::IsNearlyEqual(RGBCurve, 1.0f) == false && RGBCurve > 0.f)
		{
			Texture2D->AdjustRGBCurve = RGBCurve;
		}
#endif

		Texture2D->SRGB = TextureElement->GetSRGB() == EDatasmithColorSpace::sRGB;

		// Ensure there's no compression (we're editing pixel-by-pixel)
		Texture2D->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;

		// Update the texture with these new settings
		Texture2D->UpdateResource();

		// The content of the texture has changed, update it
		if (TextureData.ImageData != nullptr)
		{
			TextureData.Region = FUpdateTextureRegion2D(0, 0, 0, 0, TextureData.Width, TextureData.Height);

			Texture2D->UpdateTextureRegions(0, 1, &TextureData.Region, TextureData.Pitch, TextureData.BytesPerPixel, TextureData.ImageData, DataCleanupFunc );
		}

		TextureData.bCompleted = true;

		return true;
	}

	bool CreateIESTexture(FTextureData& TextureData, IDatasmithTextureElement* TextureElement, FDataCleanupFunc& DataCleanupFunc)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CreateIESTexture);

		UTextureLightProfile* Texture = TextureData.GetObject<UTextureLightProfile>();

		if (Texture == nullptr)
		{
			Texture = NewObject<UTextureLightProfile>();
			check (Texture);

#ifdef ASSET_DEBUG
			FString BaseName = FPaths::GetBaseFilename(TextureElement->GetFile());
			FString TextureName = BaseName + TEXT("_LU_") + FString::FromInt(TextureData.ElementId);
			TextureName = FDatasmithUtils::SanitizeObjectName(TextureName);
			UPackage* Package = CreatePackage(*FPaths::Combine( TEXT("/Engine/Transient/LU"), TextureName));
			Texture->Rename(*TextureName, Package, REN_DontCreateRedirectors | REN_NonTransactional);
			Texture->SetFlags(RF_Public);
#endif

			TextureData.Object = TStrongObjectPtr<UObject>(Texture);
		}

		// TextureData.ImageData should not be null
		ensure(TextureData.ImageData);

#if WITH_EDITORONLY_DATA
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(TextureElement->GetFile()));
		Texture->AssetImportData->SourceData = MoveTemp(Info);
#endif

#if WITH_EDITOR
		Texture->Source.Init(
			TextureData.Width,
			TextureData.Height,
			/*NumSlices=*/ 1,
			1,
			TSF_RGBA16F,
			TextureData.ImageData
		);

		DataCleanupFunc(nullptr, nullptr);
#endif

		Texture->LODGroup = TEXTUREGROUP_IESLightProfile;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->CompressionSettings = TC_HDR;
#if WITH_EDITORONLY_DATA
		Texture->MipGenSettings = TMGS_NoMipmaps;
#endif
		Texture->Brightness = TextureData.Brightness;
		Texture->TextureMultiplier = TextureData.TextureMultiplier;

		// Update the texture with these new settings
		Texture->UpdateResource();

#if !WITH_EDITOR
		TextureData.Region = FUpdateTextureRegion2D(0, 0, 0, 0, TextureData.Width, TextureData.Height);

		Texture->UpdateTextureRegions(0, 1, &TextureData.Region, TextureData.Pitch, TextureData.BytesPerPixel, TextureData.ImageData, DataCleanupFunc );
#endif

		return true;
	}

	EActionResult::Type FSceneImporter::CreateTexture(FSceneGraphId ElementId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::CreateTexture);

		FTextureData& TextureData = TextureDataList[ElementId];

		//#ue_liveupdate: We should not come to this point
		if (TextureData.Width == 0 || TextureData.Height == 0 || TextureData.ImageData == nullptr)
		{
			ensure(false);
			return EActionResult::Failed;
		}

		IDatasmithTextureElement* TextureElement = static_cast<IDatasmithTextureElement*>(Elements[ TextureData.ElementId ].Get());

		FDataCleanupFunc DataCleanupFunc;
		DataCleanupFunc = [this, ElementId](uint8* SrcData, const FUpdateTextureRegion2D* Regions) -> void
		{
			FTextureData& TextureData = this->TextureDataList[ElementId];

			FMemory::Free(TextureData.ImageData);
			TextureData.ImageData = nullptr;
		};

		EActionResult::Type Result = EActionResult::Unknown;

		if (TextureElement->GetTextureMode() == EDatasmithTextureMode::Ies)
		{
			Result = CreateIESTexture(TextureData, TextureElement, DataCleanupFunc) ? EActionResult::Succeeded : EActionResult::Failed;
		}
		else
		{
			Result = CreateImageTexture(TextureData, TextureElement, DataCleanupFunc) ? EActionResult::Succeeded : EActionResult::Failed;
		}

		TextureData.bCompleted = true;

		ActionCounter.Increment();

		return Result;
	}

	bool FSceneImporter::LoadTexture(FSceneGraphId ElementId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::LoadTexture);

		FTextureData& TextureData = TextureDataList[ElementId];

		IDatasmithTextureElement* TextureElement = static_cast<IDatasmithTextureElement*>(Elements[ ElementId ].Get());

		// If image file does not exist, add scene's resource path if valid
		if (!FPaths::FileExists(TextureElement->GetFile()) && FPaths::DirectoryExists(SceneElement->GetResourcePath()))
		{
			TextureElement->SetFile( *FPaths::Combine(SceneElement->GetResourcePath(), TextureElement->GetFile()) );
		}

		TextureData.Hash = TextureElement->GetFileHash();
		if (!TextureData.Hash.IsValid())
		{
			TextureData.Hash = FMD5Hash::HashFile(TextureElement->GetFile());
		}

		bool bSuccessfulLoad = false;
		FString TextureName(TextureElement->GetName());

		if (TextureElement->GetTextureMode() == EDatasmithTextureMode::Ies)
		{
			bSuccessfulLoad = GetTextureData(TextureElement->GetFile(), TextureData);
		}
		else
		{
			const bool bCreateNormal = ( TextureElement->GetTextureMode() == EDatasmithTextureMode::Bump );
			bSuccessfulLoad = GetTextureData(TextureElement->GetFile(), EDSResizeTextureMode::NearestPowerOfTwo, GMaxTextureDimensions, bCreateNormal, TextureData);
		}

		if (!bSuccessfulLoad)
		{
			if (TextureData.ImageData)
			{
				FMemory::Free(TextureData.ImageData);
			}
			TextureData.ImageData = nullptr;
			UE_LOG(LogDatasmithRuntime, Warning, TEXT("Cannot load image file %s for texture %s"), TextureElement->GetFile(), TextureElement->GetLabel());

			TextureData.bCompleted = true;
			TextureData.Object.Reset();
		}
		else
		{
			FActionTaskFunction CreateTaskFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
			{
				return this->CreateTexture(Referencer.GetId());
			};

			AddToQueue(NONASYNC_QUEUE, { CreateTaskFunc, {EDataType::Texture, ElementId, 0 } });
			TasksToComplete |= EDatasmithRuntimeWorkerTask::TextureAssign;
		}

		return true;
	}

	void FSceneImporter::ProcessTextureData(FSceneGraphId TextureId)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FSceneImporter::ProcessTextureData);

		// Textures are added in two steps. Make sure the associated FTextureData is created
		if (!TextureDataList.Contains(TextureId))
		{
			TextureDataList.Emplace(TextureId, TextureId);
		}

		FTextureData& TextureData = TextureDataList[TextureId];

		if (TextureData.bProcessed)
		{
			return;
		}

		FActionTaskFunction LoadTaskFunc = [this](UObject* Object, const FReferencer& Referencer) -> EActionResult::Type
		{
			Async(
#if WITH_EDITOR
				EAsyncExecution::LargeThreadPool,
#else
				EAsyncExecution::ThreadPool,
#endif
				// #ue_liveupdate: LightmapWeights, what about incremental addition of meshes?
				[this, ElementId = Referencer.GetId()]()->bool
			{
				return this->LoadTexture(ElementId);
			},
				[this]()->void
			{
				this->ActionCounter.Increment();
			}
			);

			return EActionResult::Succeeded;
		};

		AddToQueue(TEXTURE_QUEUE, { LoadTaskFunc, {EDataType::Texture, TextureId, 0 } });
		TasksToComplete |= EDatasmithRuntimeWorkerTask::TextureLoad;

		TextureData.bProcessed = true;

		TextureElementSet.Add(TextureId);
	}
} // End of namespace DatasmithRuntime


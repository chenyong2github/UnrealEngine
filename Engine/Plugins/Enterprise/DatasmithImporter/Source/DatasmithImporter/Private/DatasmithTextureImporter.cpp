// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithTextureImporter.h"

#include "DatasmithMaterialExpressions.h"
#include "IDatasmithSceneElements.h"
#include "DatasmithImportContext.h"
#include "Utility/DatasmithImporterUtils.h"
#include "Utility/DatasmithTextureResize.h"

#include "AssetRegistryModule.h"

#include "Async/AsyncWork.h"

#include "Editor.h"
#include "EditorFramework/AssetImportData.h"

#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureLightProfile.h"

#include "Factories/TextureFactory.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"

#include "Logging/LogMacros.h"

#include "Misc/FileHelper.h"

#include "Modules/ModuleManager.h"

#include "ObjectTools.h"

#define LOCTEXT_NAMESPACE "DatasmithTextureImport"

const uint32 MaxTextureSize = 4096;

namespace
{
	static bool ResizeTexture(const TCHAR* Filename, const TCHAR* ResizedFilename, bool bCreateNormal, FDatasmithImportContext& ImportContext)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ResizeTexture);

		EDSTextureUtilsError ErrorCode = FDatasmithTextureResize::ResizeTexture(Filename, ResizedFilename, EDSResizeTextureMode::NearestPowerOfTwo, MaxTextureSize, bCreateNormal);

		switch (ErrorCode)
		{
		case EDSTextureUtilsError::FileNotFound:
		{
			ImportContext.LogError( FText::Format( LOCTEXT( "FileNotFound", "Unable to find Texture file {0}."), FText::FromString( Filename ) ) );
			return false;
		}
		case EDSTextureUtilsError::InvalidFileType:
		{
			ImportContext.LogError( FText::Format( LOCTEXT( "InvalidFileType", "Cannot determine type of image file {0}."), FText::FromString( Filename ) ) );
			return false;
		}
		case EDSTextureUtilsError::FileReadIssue:
		{
			ImportContext.LogError( FText::Format( LOCTEXT( "FileReadIssue", "Cannot open image file {0}."), FText::FromString( Filename ) ) );
			return false;
		}
		case EDSTextureUtilsError::InvalidData:
		{
			ImportContext.LogError( FText::Format( LOCTEXT( "InvalidData", "Image file {0} contains invalid data."), FText::FromString( Filename ) ) );
			return false;
		}
		case EDSTextureUtilsError::FreeImageNotFound:
		{
			ImportContext.LogError( LOCTEXT( "FreeImageNotFound", "FreeImage.dll couldn't be found. Texture resizing won't be done.") );
			break;
		}
		default:
			break;
		}

		return true;
	}
}


FDatasmithTextureImporter::FDatasmithTextureImporter(FDatasmithImportContext& InImportContext)
	: ImportContext(InImportContext)
	, TextureFact( NewObject< UTextureFactory >() )
{
	TextureFact->SuppressImportOverwriteDialog();

	TempDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("DatasmithTextureImport"));
	IFileManager::Get().MakeDirectory(*TempDir);
}

FDatasmithTextureImporter::~FDatasmithTextureImporter()
{
	// Clean up all transient files created during the process
	IFileManager::Get().DeleteDirectory(*TempDir, false, true);
}

bool FDatasmithTextureImporter::ResizeTextureElement(const TSharedPtr<IDatasmithTextureElement>& TextureElement, FString& ResizedFilename)
{
	FString Filename = TextureElement->GetFile();

	if (Filename.IsEmpty() || !FPaths::FileExists(Filename))
	{
		UE_LOG(LogDatasmithImport, Warning, TEXT("Unable to find Texture file %s"), *Filename);
		return false;
	}

	FString Extension;
	if (!FDatasmithTextureResize::GetBestTextureExtension(*Filename, Extension))
	{
		return false;
	}
	// Convert HDR image to EXR if not used as environment map
	else if (Extension == TEXT(".hdr") && TextureElement->GetTextureMode() != EDatasmithTextureMode::Other)
	{
		Extension = TEXT(".exr");
	}

	ResizedFilename = FPaths::Combine(TempDir, LexToString(FGuid::NewGuid()) + Extension);

	const bool bGenerateNormalMap = ( TextureElement->GetTextureMode() == EDatasmithTextureMode::Bump );

	const bool bResult = ResizeTexture(*Filename, *ResizedFilename, bGenerateNormalMap, ImportContext);

	if ( bResult && bGenerateNormalMap )
	{
		TextureElement->SetTextureMode( EDatasmithTextureMode::Normal );
	}

	return bResult;
}

bool FDatasmithTextureImporter::GetTextureData(const TSharedPtr<IDatasmithTextureElement>& TextureElement, TArray<uint8>& TextureData, FString& Extension)
{
	const FString Filename = TextureElement->GetFile();
	if (!Filename.IsEmpty())
	{
		// load from a file path

		FString ImageFileName;
		if (!ResizeTextureElement(TextureElement, ImageFileName))
		{
			return false;
		}

		// try opening from absolute path
		if (!(FFileHelper::LoadFileToArray(TextureData, *ImageFileName) && TextureData.Num() > 0))
		{
			ImportContext.LogWarning(FText::Format(LOCTEXT("UnableToFindTexture", "Unable to find Texture file {0}."), FText::FromString(Filename)));
			return false;
		}

		Extension         = FPaths::GetExtension(ImageFileName).ToLower();
	}
	else
	{
		// load from memory source
		EDatasmithTextureFormat TextureFormat;
		uint32                  TextureSize;

		const uint8* PtrTextureData = TextureElement->GetData(TextureSize, TextureFormat);

		if (PtrTextureData == nullptr || !TextureSize)
		{
			return false;
		}

		TextureData.SetNumUninitialized(TextureSize);
		FPlatformMemory::Memcpy(TextureData.GetData(), PtrTextureData, TextureSize);

		switch (TextureFormat)
		{
			case EDatasmithTextureFormat::PNG:
				Extension = TEXT("png");
				break;
			case EDatasmithTextureFormat::JPEG:
				Extension = TEXT("jpeg");
				break;
			default:
				check(false);
		}
	}

	return true;
}


UTexture* FDatasmithTextureImporter::CreateTexture(const TSharedPtr<IDatasmithTextureElement>& TextureElement, const TArray<uint8>& TextureData, const FString& Extension)
{
	const FString TextureLabel = TextureElement->GetLabel();
	const FString TextureName = TextureLabel.Len() > 0 ? ImportContext.AssetsContext.TextureNameProvider.GenerateUniqueName(TextureLabel) : TextureElement->GetName();

	// Verify that the texture could be created in final package
	FText FailReason;
	if (!FDatasmithImporterUtils::CanCreateAsset<UTexture2D>(ImportContext.AssetsContext.TexturesFinalPackage.Get(), TextureName, FailReason))
	{
		ImportContext.LogError(FailReason);
		return nullptr;
	}

	TextureFact->bFlipNormalMapGreenChannel = false;

	// Make sure to set the proper LODGroup as it's used to determine the CompressionSettings when using TEXTUREGROUP_WorldNormalMap
	switch (TextureElement->GetTextureMode())
	{
	case EDatasmithTextureMode::Diffuse:
		TextureFact->MipGenSettings = TMGS_Sharpen5;
		TextureFact->LODGroup = TEXTUREGROUP_World;
		break;
	case EDatasmithTextureMode::Specular:
		TextureFact->LODGroup = TEXTUREGROUP_WorldSpecular;
		break;
	case EDatasmithTextureMode::Bump:
	case EDatasmithTextureMode::Normal:
		TextureFact->LODGroup = TEXTUREGROUP_WorldNormalMap;
		TextureFact->CompressionSettings = TC_Normalmap;
		break;
	case EDatasmithTextureMode::NormalGreenInv:
		TextureFact->LODGroup = TEXTUREGROUP_WorldNormalMap;
		TextureFact->CompressionSettings = TC_Normalmap;
		TextureFact->bFlipNormalMapGreenChannel = true;
		break;
	case EDatasmithTextureMode::Displace:
		TextureFact->CompressionSettings = TC_Displacementmap;
		TextureFact->LODGroup = TEXTUREGROUP_World;
		break;
	}

	const float RGBCurve = TextureElement->GetRGBCurve();

	const uint8* PtrTextureData    = TextureData.GetData();
	const uint8* PtrTextureDataEnd = PtrTextureData + TextureData.Num();
	const FString Filename = TextureElement->GetFile();
	UPackage* TextureOuter = ImportContext.AssetsContext.TexturesImportPackage.Get();

	// This has to be called explicitly each time we create a texture since the flag gets reset in FactoryCreateBinary
	TextureFact->SuppressImportOverwriteDialog();
	UTexture2D* Texture =
		(UTexture2D*)TextureFact->FactoryCreateBinary(UTexture2D::StaticClass(), TextureOuter, *TextureName, ImportContext.ObjectFlags /*& ~RF_Public*/, nullptr,
															*Extension, PtrTextureData, PtrTextureDataEnd, GWarn);
	if (Texture != nullptr)
	{
		static_assert(TextureAddress::TA_Wrap == (int)EDatasmithTextureAddress::Wrap && TextureAddress::TA_Mirror == (int)EDatasmithTextureAddress::Mirror, "Texture Address enum doesn't match!" );

		FMD5Hash Hash = TextureElement->CalculateElementHash(false);

		TextureFilter TexFilter = TextureFilter::TF_Default;

		switch ( TextureElement->GetTextureFilter() )
		{
		case EDatasmithTextureFilter::Nearest:
			TexFilter = TextureFilter::TF_Nearest;
			break;
		case EDatasmithTextureFilter::Bilinear:
			TexFilter = TextureFilter::TF_Bilinear;
			break;
		case EDatasmithTextureFilter::Trilinear:
			TexFilter = TextureFilter::TF_Trilinear;
			break;
		case EDatasmithTextureFilter::Default:
		default:
			TexFilter = TextureFilter::TF_Default;
			break;
		}

		Texture->Filter = TexFilter;
		Texture->AddressX = (TextureAddress)TextureElement->GetTextureAddressX();
		Texture->AddressY = (TextureAddress)TextureElement->GetTextureAddressY();

		// Update import data
		Texture->AssetImportData->Update(Filename, &Hash);

		// Notify the asset registry
		FAssetRegistryModule::AssetCreated(Texture);

		if (FMath::IsNearlyEqual(RGBCurve, 1.0f) == false && RGBCurve > 0.f)
		{
			Texture->AdjustRGBCurve = RGBCurve;
			Texture->UpdateResource();
		}

		Texture->MarkPackageDirty();
	}

	return Texture;
}

#undef LOCTEXT_NAMESPACE

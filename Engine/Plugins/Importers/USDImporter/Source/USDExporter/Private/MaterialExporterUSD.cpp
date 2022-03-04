// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialExporterUSD.h"

#include "MaterialExporterUSDOptions.h"
#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDMemory.h"
#include "USDOptionsWindow.h"
#include "USDShadeConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "AssetExportTask.h"
#include "HAL/FileManager.h"
#include "IMaterialBakingModule.h"
#include "MaterialOptions.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/Paths.h"

UMaterialExporterUsd::UMaterialExporterUsd()
{
#if USE_USD_SDK
	for ( const FString& Extension : UnrealUSDWrapper::GetNativeFileFormats() )
	{
		// USDZ is not supported for writing for now
		if ( Extension.Equals( TEXT( "usdz" ) ) )
		{
			continue;
		}

		FormatExtension.Add(Extension);
		FormatDescription.Add(TEXT("Universal Scene Description file"));
	}
	SupportedClass = UMaterialInterface::StaticClass();
	bText = false;
#endif // #if USE_USD_SDK
}

bool UMaterialExporterUsd::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
#if USE_USD_SDK
	UMaterialInterface* Material = Cast< UMaterialInterface >( Object );
	if ( !Material )
	{
		return false;
	}

	UMaterialExporterUSDOptions* Options = nullptr;
	if ( ExportTask )
	{
		Options = Cast<UMaterialExporterUSDOptions>( ExportTask->Options );
	}
	if ( !Options && ( !ExportTask || !ExportTask->bAutomated ) )
	{
		Options = GetMutableDefault<UMaterialExporterUSDOptions>();
		if ( Options )
		{
			Options->MaterialBakingOptions.TexturesDir.Path = FPaths::Combine( FPaths::GetPath( UExporter::CurrentFilename ), TEXT( "Textures" ) );

			const bool bIsImport = false;
			const bool bContinue = SUsdOptionsWindow::ShowOptions( *Options, bIsImport );
			if ( !bContinue )
			{
				return false;
			}
		}
	}

	return UMaterialExporterUsd::ExportMaterial( *Material, Options->MaterialBakingOptions, FFilePath{ UExporter::CurrentFilename } );
#else
	return false;
#endif // #if USE_USD_SDK
}

bool UMaterialExporterUsd::ExportMaterial( const UMaterialInterface& Material, const FUsdMaterialBakingOptions& Options, const FFilePath& FilePath )
{
#if USE_USD_SDK
	UE::FUsdStage UsdStage = UnrealUSDWrapper::NewStage( *FilePath.FilePath );
	if ( !UsdStage )
	{
		return false;
	}

	FString RootPrimPath = TEXT( "/" ) + UsdUtils::SanitizeUsdIdentifier( *Material.GetName() );

	UE::FUsdPrim RootPrim = UsdStage.DefinePrim( UE::FSdfPath( *RootPrimPath ), TEXT( "Material" ) );
	if ( !RootPrim )
	{
		return false;
	}

	UsdStage.SetDefaultPrim( RootPrim );

	UsdUtils::SetUnrealSurfaceOutput( RootPrim, Material.GetPathName() );

	UnrealToUsd::ConvertMaterialToBakedSurface(
		Material,
		Options.Properties,
		Options.DefaultTextureSize,
		Options.TexturesDir,
		RootPrim
	);

	UsdStage.GetRootLayer().Save();

	return true;
#else
	return false;
#endif // USE_USD_SDK
}

bool UMaterialExporterUsd::ExportMaterialsForStage(
	const TArray<UMaterialInterface*>& Materials,
	const FUsdMaterialBakingOptions& Options,
	const UE::FUsdStage& Stage,
	bool bIsAssetLayer,
	bool bUsePayload,
	bool bRemoveUnrealMaterials )
{
#if USE_USD_SDK
	if ( !Stage )
	{
		return false;
	}

	if ( Materials.Num() == 0 )
	{
		return true;
	}

	const UE::FSdfLayer RootLayer = Stage.GetRootLayer();
	const FString RootLayerPath = RootLayer.GetRealPath();
	const FString ExtensionNoDot = FPaths::GetExtension( RootLayerPath );

	// If we have multiple materials *within this mesh* that want to be emitted to the same filepath we'll append
	// a suffix, but we will otherwise overwrite any unrelated existing files that were there before we began the export.
	// This allows the workflow of repeatedly exporting over the same files to update the results
	TSet<FString> UsedFilePathsWithoutExt;

	TMap<FString, FString> MaterialPathNameToFilePath;
	for ( const UMaterialInterface* Material : Materials )
	{
		if ( !Material )
		{
			continue;
		}

		// "/Game/ContentFolder/Blue.Blue"
		FString MaterialPathName = Material->GetPathName();

		// "C:/MyFolder/Export/Blue"
		FString MaterialFilePath = FPaths::Combine( FPaths::GetPath( UExporter::CurrentFilename ), FPaths::GetBaseFilename( MaterialPathName ) );

		// "C:/MyFolder/Export/Blue_4"
		FString FinalPathNoExt = UsdUtils::GetUniqueName( MaterialFilePath, UsedFilePathsWithoutExt );

		// "C:/MyFolder/Export/Blue_4.usda"
		FString FinalPath = FString::Printf( TEXT( "%s.%s" ), *FinalPathNoExt, *ExtensionNoDot );

		if ( UMaterialExporterUsd::ExportMaterial( *Material, Options, FFilePath{ FinalPath } ) )
		{
			UsedFilePathsWithoutExt.Add( FinalPathNoExt );
			MaterialPathNameToFilePath.Add( MaterialPathName, FinalPath );
		}
	}

	UsdUtils::ReplaceUnrealMaterialsWithBaked(
		Stage,
		RootLayer,
		MaterialPathNameToFilePath,
		bIsAssetLayer,
		bUsePayload,
		bRemoveUnrealMaterials
	);

	return true;
#else
	return false;
#endif // USE_USD_SDK
}

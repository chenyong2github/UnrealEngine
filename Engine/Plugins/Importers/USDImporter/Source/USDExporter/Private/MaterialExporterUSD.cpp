// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaterialExporterUSD.h"

#include "MaterialExporterUSDOptions.h"
#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDMemory.h"
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
	for ( const FString& Extension : UnrealUSDWrapper::GetAllSupportedFileFormats() )
	{
		// USDZ is not supported for writing for now
		if ( Extension.Equals( TEXT( "usdz" ) ) )
		{
			continue;
		}

		FormatExtension.Add(Extension);
		FormatDescription.Add(TEXT("USD file"));
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

	// Get export options
	UMaterialExporterUSDOptions* ExportOptions = nullptr;
	{
		if ( ExportTask )
		{
			ExportOptions = Cast<UMaterialExporterUSDOptions>( ExportTask->Options );
		}

		if ( !ExportOptions )
		{
			ExportOptions = DuplicateObject( GetMutableDefault<UMaterialExporterUSDOptions>(), GetTransientPackage() );
			ExportOptions->TexturesDir.Path = FPaths::Combine( FPaths::GetPath( UExporter::CurrentFilename ), TEXT( "Textures" ) );
		}

		if ( !ExportTask || !ExportTask->bAutomated )
		{
			// Use the baking module's options window because it has some useful customizations for FPropertyEntry properties.
			IMaterialBakingModule& Module = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>( TEXT( "MaterialBaking" ) );

			// Sadly we have to set this property here or else there is some custom logic in SMaterialOptions::OnConfirm that could cause baking to fail
			UMaterialOptions* UnusedOptions = GetMutableDefault<UMaterialOptions>();
			if( UnusedOptions->LODIndices.Num() == 0)
			{
				UnusedOptions->LODIndices.Add( 0 );
			}

			const int32 NumLODs = 1;
			TArray<TWeakObjectPtr<UObject>> SettingsObjects = { ExportOptions };
			bool bProceed = Module.SetupMaterialBakeSettings( SettingsObjects, NumLODs );
			if ( !bProceed )
			{
				return true; // True because the operation was canceled, so we technically succeeded
			}
		}
	}

	UE::FUsdStage UsdStage = UnrealUSDWrapper::NewStage( *UExporter::CurrentFilename );
	if ( !UsdStage )
	{
		return false;
	}

	FString RootPrimPath = ( TEXT( "/" ) + UsdUtils::SanitizeUsdIdentifier( *Material->GetName() ) );

	UE::FUsdPrim RootPrim = UsdStage.DefinePrim( UE::FSdfPath( *RootPrimPath ), TEXT( "Material" ) );
	if ( !RootPrim )
	{
		return false;
	}

	UsdStage.SetDefaultPrim( RootPrim );

	UnrealToUsd::ConvertMaterialToBakedSurface( *Material, ExportOptions->Properties, ExportOptions->DefaultTextureSize, ExportOptions->TexturesDir, RootPrim );

	UsdStage.GetRootLayer().Save();

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

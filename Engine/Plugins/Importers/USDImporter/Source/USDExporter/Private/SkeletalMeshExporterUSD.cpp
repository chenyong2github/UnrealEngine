// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshExporterUSD.h"

#include "SkeletalMeshExporterUSDOptions.h"
#include "USDConversionUtils.h"
#include "USDMemory.h"
#include "USDOptionsWindow.h"
#include "USDSkeletalDataConversion.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "AssetExportTask.h"
#include "Engine/SkeletalMesh.h"

USkeletalMeshExporterUsd::USkeletalMeshExporterUsd()
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
	SupportedClass = USkeletalMesh::StaticClass();
	bText = false;
#endif // #if USE_USD_SDK
}

bool USkeletalMeshExporterUsd::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
#if USE_USD_SDK
	USkeletalMesh* SkeletalMesh = CastChecked< USkeletalMesh >( Object );
	if ( !SkeletalMesh )
	{
		return false;
	}

	USkeletalMeshExporterUSDOptions* Options = nullptr;
	if ( ExportTask )
	{
		Options = Cast<USkeletalMeshExporterUSDOptions>( ExportTask->Options );
	}
	if ( !Options && ( !ExportTask || !ExportTask->bAutomated ) )
	{
		Options = GetMutableDefault<USkeletalMeshExporterUSDOptions>();
		if ( Options )
		{
			const bool bIsImport = false;
			const bool bContinue = SUsdOptionsWindow::ShowOptions( *Options, bIsImport );
			if ( !bContinue )
			{
				return false;
			}
		}
	}

	// If bUsePayload is true, we'll intercept the filename so that we write the mesh data to
	// "C:/MyFolder/file_payload.usda" and create an "asset" file "C:/MyFolder/file.usda" that uses it
	// as a payload, pointing at the default prim
	FString PayloadFilename = UExporter::CurrentFilename;
	if ( Options && Options->Inner.bUsePayload )
	{
		FString PathPart;
		FString FilenamePart;
		FString ExtensionPart;
		FPaths::Split( PayloadFilename, PathPart, FilenamePart, ExtensionPart );

		if ( FormatExtension.Contains( Options->Inner.PayloadFormat ) )
		{
			ExtensionPart = Options->Inner.PayloadFormat;
		}

		PayloadFilename = FPaths::Combine( PathPart, FilenamePart + TEXT( "_payload." ) + ExtensionPart );
	}

	UE::FUsdStage UsdStage = UnrealUSDWrapper::NewStage( *PayloadFilename );
	if ( !UsdStage )
	{
		return false;
	}

	if ( Options )
	{
		UsdUtils::SetUsdStageMetersPerUnit( UsdStage, Options->Inner.StageOptions.MetersPerUnit );
		UsdUtils::SetUsdStageUpAxis( UsdStage, Options->Inner.StageOptions.UpAxis );
	}

	FString RootPrimPath = ( TEXT( "/" ) + UsdUtils::SanitizeUsdIdentifier( *SkeletalMesh->GetName() ) );

	FScopedUsdAllocs Allocs;

	UE::FUsdPrim RootPrim = UsdStage.DefinePrim( UE::FSdfPath( *RootPrimPath ), TEXT("SkelRoot") );
	if ( !RootPrim )
	{
		return false;
	}

	UsdStage.SetDefaultPrim( RootPrim );

	// Using payload: Convert mesh data through the asset stage (that references the payload) so that we can
	// author mesh data on the payload layer and material data on the asset layer
	if ( Options && Options->Inner.bUsePayload )
	{
		if ( UE::FUsdStage AssetStage = UnrealUSDWrapper::NewStage( *UExporter::CurrentFilename ) )
		{
			UsdUtils::SetUsdStageMetersPerUnit( AssetStage, Options->Inner.StageOptions.MetersPerUnit );
			UsdUtils::SetUsdStageUpAxis( AssetStage, Options->Inner.StageOptions.UpAxis );

			if ( UE::FUsdPrim AssetRootPrim = AssetStage.DefinePrim( UE::FSdfPath( *RootPrimPath ) ) )
			{
				AssetStage.SetDefaultPrim( AssetRootPrim );

				UsdUtils::AddPayload( AssetRootPrim, *PayloadFilename );
			}

			UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, RootPrim, UsdUtils::GetDefaultTimeCode(), &AssetStage );

			AssetStage.GetRootLayer().Save();
		}
	}
	// Not using payload: Just author everything on the current edit target of the payload (== asset) layer
	else
	{
		UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, RootPrim );
	}

	UsdStage.GetRootLayer().Save();

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

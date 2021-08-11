// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshExporterUSD.h"

#include "EngineAnalytics.h"
#include "StaticMeshExporterUSDOptions.h"
#include "USDClassesModule.h"
#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDMemory.h"
#include "USDOptionsWindow.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "AssetExportTask.h"
#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"

namespace UE
{
	namespace StaticMeshExporterUSD
	{
		namespace Private
		{
			void SendAnalytics( UObject* Asset, UStaticMeshExporterUSDOptions* Options, bool bAutomated, double ElapsedSeconds, double NumberOfFrames, const FString& Extension )
			{
				if ( !Asset )
				{
					return;
				}

				if ( FEngineAnalytics::IsAvailable() )
				{
					FString ClassName = Asset->GetClass()->GetName();

					TArray<FAnalyticsEventAttribute> EventAttributes;

					EventAttributes.Emplace( TEXT( "AssetType" ), ClassName );

					if ( Options )
					{
						EventAttributes.Emplace( TEXT( "UsePayload" ), LexToString( Options->bUsePayload ) );
						EventAttributes.Emplace( TEXT( "PayloadFormat" ), Options->PayloadFormat );
						EventAttributes.Emplace( TEXT( "LowestMeshLOD" ), LexToString( Options->LowestMeshLOD ) );
						EventAttributes.Emplace( TEXT( "HighestMeshLOD" ), LexToString( Options->HighestMeshLOD ) );
					}

					IUsdClassesModule::SendAnalytics( MoveTemp( EventAttributes ), FString::Printf( TEXT( "Export.%s" ), *ClassName ), bAutomated, ElapsedSeconds, NumberOfFrames, Extension );
				}
			}
		}
	}
}

bool UStaticMeshExporterUsd::IsUsdAvailable()
{
#if USE_USD_SDK
	return true;
#else
	return false;
#endif
}

UStaticMeshExporterUsd::UStaticMeshExporterUsd()
{
#if USE_USD_SDK
	for ( const FString& Extension : UnrealUSDWrapper::GetAllSupportedFileFormats() )
	{
		// USDZ is not supported for writing for now
		if ( Extension.Equals( TEXT( "usdz" ) ) )
		{
			continue;
		}

		FormatExtension.Add( Extension );
		FormatDescription.Add( TEXT( "USD file" ) );
	}
	SupportedClass = UStaticMesh::StaticClass();
	bText = false;
#endif // #if USE_USD_SDK
}

bool UStaticMeshExporterUsd::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
#if USE_USD_SDK
	UStaticMesh* StaticMesh = CastChecked< UStaticMesh >( Object );
	if ( !StaticMesh )
	{
		return false;
	}

	UStaticMeshExporterUSDOptions* Options = nullptr;
	if ( ExportTask )
	{
		Options = Cast<UStaticMeshExporterUSDOptions>( ExportTask->Options );
	}
	if ( !Options && ( !ExportTask || !ExportTask->bAutomated ) )
	{
		Options = GetMutableDefault<UStaticMeshExporterUSDOptions>();
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

	double StartTime = FPlatformTime::Cycles64();

	// If bUsePayload is true, we'll intercept the filename so that we write the mesh data to
	// "C:/MyFolder/file_payload.usda" and create an "asset" file "C:/MyFolder/file.usda" that uses it
	// as a payload, pointing at the default prim
	FString PayloadFilename = UExporter::CurrentFilename;
	if ( Options && Options->bUsePayload )
	{
		FString PathPart;
		FString FilenamePart;
		FString ExtensionPart;
		FPaths::Split( PayloadFilename, PathPart, FilenamePart, ExtensionPart );

		if ( FormatExtension.Contains( Options->PayloadFormat ) )
		{
			ExtensionPart = Options->PayloadFormat;
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
		UsdUtils::SetUsdStageMetersPerUnit( UsdStage, Options->StageOptions.MetersPerUnit );
		UsdUtils::SetUsdStageUpAxis( UsdStage, Options->StageOptions.UpAxis );
	}

	FString RootPrimPath = ( TEXT( "/" ) + UsdUtils::SanitizeUsdIdentifier( *StaticMesh->GetName() ) );

	UE::FUsdPrim RootPrim = UsdStage.DefinePrim( UE::FSdfPath( *RootPrimPath ) );
	if ( !RootPrim )
	{
		return false;
	}

	UsdStage.SetDefaultPrim( RootPrim );

	// Using payload: Convert mesh data through the asset stage (that references the payload) so that we can
	// author mesh data on the payload layer and material data on the asset layer
	if ( Options && Options->bUsePayload )
	{
		if ( UE::FUsdStage AssetStage = UnrealUSDWrapper::NewStage( *UExporter::CurrentFilename ) )
		{
			UsdUtils::SetUsdStageMetersPerUnit( AssetStage, Options->StageOptions.MetersPerUnit );
			UsdUtils::SetUsdStageUpAxis( AssetStage, Options->StageOptions.UpAxis );

			if ( UE::FUsdPrim AssetRootPrim = AssetStage.DefinePrim( UE::FSdfPath( *RootPrimPath ) ) )
			{
				AssetStage.SetDefaultPrim( AssetRootPrim );

				UsdUtils::AddPayload( AssetRootPrim, *PayloadFilename );
			}

			UnrealToUsd::ConvertStaticMesh( StaticMesh, RootPrim, UsdUtils::GetDefaultTimeCode(), &AssetStage, Options->LowestMeshLOD, Options->HighestMeshLOD );

			AssetStage.GetRootLayer().Save();
		}
	}
	// Not using payload: Just author everything on the current edit target of the payload (== asset) layer
	else
	{
		int32 LowestLOD = 0;
		int32 HighestLOD = MAX_MESH_LOD_COUNT - 1;
		if ( Options )
		{
			LowestLOD = Options->LowestMeshLOD;
			HighestLOD = Options->HighestMeshLOD;
		}

		UnrealToUsd::ConvertStaticMesh( StaticMesh, RootPrim, UsdUtils::GetDefaultTimeCode(), nullptr, LowestLOD, HighestLOD );
	}

	UsdStage.GetRootLayer().Save();

	// Analytics
	{
		bool bAutomated = ExportTask ? ExportTask->bAutomated : false;
		double ElapsedSeconds = FPlatformTime::ToSeconds64( FPlatformTime::Cycles64() - StartTime );
		FString Extension = FPaths::GetExtension( UExporter::CurrentFilename );
		double NumberOfFrames = UsdStage.GetEndTimeCode() - UsdStage.GetStartTimeCode();

		UE::StaticMeshExporterUSD::Private::SendAnalytics( Object, Options, bAutomated, ElapsedSeconds, NumberOfFrames, Extension );
	}

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

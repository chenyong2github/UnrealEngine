// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshExporterUSD.h"

#include "EngineAnalytics.h"
#include "SkeletalMeshExporterUSDOptions.h"
#include "USDClassesModule.h"
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

namespace UE
{
	namespace SkeletalMeshExporterUSD
	{
		namespace Private
		{
			void SendAnalytics( UObject* Asset, USkeletalMeshExporterUSDOptions* Options, bool bAutomated, double ElapsedSeconds, double NumberOfFrames, const FString& Extension )
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
						EventAttributes.Emplace( TEXT( "UsePayload" ), LexToString( Options->Inner.bUsePayload ) );
						EventAttributes.Emplace( TEXT( "PayloadFormat" ), Options->Inner.PayloadFormat );
						EventAttributes.Emplace( TEXT( "LowestMeshLOD" ), LexToString( Options->Inner.LowestMeshLOD ) );
						EventAttributes.Emplace( TEXT( "HighestMeshLOD" ), LexToString( Options->Inner.HighestMeshLOD ) );
					}

					IUsdClassesModule::SendAnalytics( MoveTemp( EventAttributes ), FString::Printf( TEXT( "Export.%s" ), *ClassName ), bAutomated, ElapsedSeconds, NumberOfFrames, Extension );
				}
			}
		}
	}
}

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

	double StartTime = FPlatformTime::Cycles64();

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

			UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, RootPrim, UsdUtils::GetDefaultTimeCode(), &AssetStage, Options->Inner.LowestMeshLOD, Options->Inner.HighestMeshLOD );

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
			LowestLOD = Options->Inner.LowestMeshLOD;
			HighestLOD = Options->Inner.HighestMeshLOD;
		}

		UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, RootPrim, UsdUtils::GetDefaultTimeCode(), nullptr, LowestLOD, HighestLOD );
	}

	UsdStage.GetRootLayer().Save();

	// Analytics
	{
		bool bAutomated = ExportTask ? ExportTask->bAutomated : false;
		double ElapsedSeconds = FPlatformTime::ToSeconds64( FPlatformTime::Cycles64() - StartTime );
		FString Extension = FPaths::GetExtension( UExporter::CurrentFilename );
		double NumberOfFrames = UsdStage.GetEndTimeCode() - UsdStage.GetStartTimeCode();

		UE::SkeletalMeshExporterUSD::Private::SendAnalytics( Object, Options, bAutomated, ElapsedSeconds, NumberOfFrames, Extension );
	}

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

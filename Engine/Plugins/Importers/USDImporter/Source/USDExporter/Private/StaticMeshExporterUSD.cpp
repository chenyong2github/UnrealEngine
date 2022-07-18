// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshExporterUSD.h"

#include "EngineAnalytics.h"
#include "MaterialExporterUSD.h"
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
						EventAttributes.Emplace( TEXT( "MetersPerUnit" ), LexToString( Options->StageOptions.MetersPerUnit ) );
						EventAttributes.Emplace( TEXT( "UpAxis" ), Options->StageOptions.UpAxis == EUsdUpAxis::YAxis ? TEXT( "Y" ) : TEXT( "Z" ) );
						EventAttributes.Emplace( TEXT( "UsePayload" ), LexToString( Options->MeshAssetOptions.bUsePayload ) );
						if ( Options->MeshAssetOptions.bUsePayload )
						{
							EventAttributes.Emplace( TEXT( "PayloadFormat" ), Options->MeshAssetOptions.PayloadFormat );
						}
						EventAttributes.Emplace( TEXT( "BakeMaterials" ), Options->MeshAssetOptions.bBakeMaterials );
						if ( Options->MeshAssetOptions.bBakeMaterials )
						{
							FString BakedPropertiesString;
							{
								const UEnum* PropertyEnum = StaticEnum<EMaterialProperty>();
								for ( const FPropertyEntry& PropertyEntry : Options->MeshAssetOptions.MaterialBakingOptions.Properties )
								{
									FString PropertyString = PropertyEnum->GetNameByValue( PropertyEntry.Property ).ToString();
									PropertyString.RemoveFromStart( TEXT( "MP_" ) );
									BakedPropertiesString += PropertyString + TEXT( ", " );
								}

								BakedPropertiesString.RemoveFromEnd( TEXT( ", " ) );
							}

							EventAttributes.Emplace( TEXT( "RemoveUnrealMaterials" ), Options->MeshAssetOptions.bRemoveUnrealMaterials );
							EventAttributes.Emplace( TEXT( "BakedProperties" ), BakedPropertiesString );
							EventAttributes.Emplace( TEXT( "DefaultTextureSize" ), Options->MeshAssetOptions.MaterialBakingOptions.DefaultTextureSize.ToString() );
						}
						EventAttributes.Emplace( TEXT( "LowestMeshLOD" ), LexToString( Options->MeshAssetOptions.LowestMeshLOD ) );
						EventAttributes.Emplace( TEXT( "HighestMeshLOD" ), LexToString( Options->MeshAssetOptions.HighestMeshLOD ) );
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
	for ( const FString& Extension : UnrealUSDWrapper::GetNativeFileFormats() )
	{
		// USDZ is not supported for writing for now
		if ( Extension.Equals( TEXT( "usdz" ) ) )
		{
			continue;
		}

		FormatExtension.Add( Extension );
		FormatDescription.Add( TEXT( "Universal Scene Description file" ) );
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
			Options->MeshAssetOptions.MaterialBakingOptions.TexturesDir.Path = FPaths::Combine( FPaths::GetPath( UExporter::CurrentFilename ), TEXT( "Textures" ) );

			const bool bIsImport = false;
			const bool bContinue = SUsdOptionsWindow::ShowImportExportOptions( *Options, bIsImport );
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
	if ( Options && Options->MeshAssetOptions.bUsePayload )
	{
		FString PathPart;
		FString FilenamePart;
		FString ExtensionPart;
		FPaths::Split( PayloadFilename, PathPart, FilenamePart, ExtensionPart );

		if ( FormatExtension.Contains( Options->MeshAssetOptions.PayloadFormat ) )
		{
			ExtensionPart = Options->MeshAssetOptions.PayloadFormat;
		}

		PayloadFilename = FPaths::Combine( PathPart, FilenamePart + TEXT( "_payload." ) + ExtensionPart );
	}

	// UsdStage is the payload stage when exporting with payloads, or just the single stage otherwise
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

	// Asset stage always the stage where we write the material assignments
	UE::FUsdStage AssetStage;

	// Using payload: Convert mesh data through the asset stage (that references the payload) so that we can
	// author mesh data on the payload layer and material data on the asset layer
	if ( Options && Options->MeshAssetOptions.bUsePayload )
	{
		AssetStage = UnrealUSDWrapper::NewStage( *UExporter::CurrentFilename );
		if ( AssetStage )
		{
			UsdUtils::SetUsdStageMetersPerUnit( AssetStage, Options->StageOptions.MetersPerUnit );
			UsdUtils::SetUsdStageUpAxis( AssetStage, Options->StageOptions.UpAxis );

			if ( UE::FUsdPrim AssetRootPrim = AssetStage.DefinePrim( UE::FSdfPath( *RootPrimPath ) ) )
			{
				AssetStage.SetDefaultPrim( AssetRootPrim );

				UsdUtils::AddPayload( AssetRootPrim, *PayloadFilename );
			}
		}
	}
	// Not using payload: Just author everything on the current edit target of the payload (== asset) layer
	else
	{
		AssetStage = UsdStage;
	}

	UnrealToUsd::ConvertStaticMesh( StaticMesh, RootPrim, UsdUtils::GetDefaultTimeCode(), &AssetStage, Options->MeshAssetOptions.LowestMeshLOD, Options->MeshAssetOptions.HighestMeshLOD );

	// Bake materials and replace unrealMaterials with references to the baked files.
	if ( Options->MeshAssetOptions.bBakeMaterials )
	{
		TSet<UMaterialInterface*> MaterialsToBake;
		for ( const FStaticMaterial& StaticMaterial : StaticMesh->GetStaticMaterials() )
		{
			MaterialsToBake.Add( StaticMaterial.MaterialInterface );
		}

		const bool bIsAssetLayer = true;
		UMaterialExporterUsd::ExportMaterialsForStage(
			MaterialsToBake.Array(),
			Options->MeshAssetOptions.MaterialBakingOptions,
			AssetStage,
			bIsAssetLayer,
			Options->MeshAssetOptions.bUsePayload,
			Options->MeshAssetOptions.bRemoveUnrealMaterials
		);
	}

	UsdStage.GetRootLayer().Save();
	if ( AssetStage && UsdStage != AssetStage )
	{
		AssetStage.GetRootLayer().Save();
	}

	// Analytics
	{
		bool bAutomated = ExportTask ? ExportTask->bAutomated : false;
		double ElapsedSeconds = FPlatformTime::ToSeconds64( FPlatformTime::Cycles64() - StartTime );
		FString Extension = FPaths::GetExtension( UExporter::CurrentFilename );

		UE::StaticMeshExporterUSD::Private::SendAnalytics(
			Object,
			Options,
			bAutomated,
			ElapsedSeconds,
			UsdUtils::GetUsdStageNumFrames( AssetStage ),
			Extension
		);
	}

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

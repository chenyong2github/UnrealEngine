// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimSequenceExporterUSD.h"

#include "AnimSequenceExporterUSDOptions.h"
#include "EngineAnalytics.h"
#include "Engine/SkeletalMesh.h"
#include "MaterialExporterUSD.h"
#include "UnrealUSDWrapper.h"
#include "USDClassesModule.h"
#include "USDErrorUtils.h"
#include "USDLayerUtils.h"
#include "USDOptionsWindow.h"
#include "USDSkeletalDataConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "Animation/AnimSequence.h"
#include "AssetExportTask.h"

namespace UE
{
	namespace AnimSequenceExporterUSD
	{
		namespace Private
		{
			void SendAnalytics( UObject* Asset, UAnimSequenceExporterUSDOptions* Options, bool bAutomated, double ElapsedSeconds, double NumberOfFrames, const FString& Extension )
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

						EventAttributes.Emplace( TEXT( "ExportPreviewMesh" ), LexToString( Options->bExportPreviewMesh ) );
						if ( Options->bExportPreviewMesh )
						{
							EventAttributes.Emplace( TEXT( "UsePayload" ), LexToString( Options->PreviewMeshOptions.bUsePayload ) );
							if ( Options->PreviewMeshOptions.bUsePayload )
							{
								EventAttributes.Emplace( TEXT( "PayloadFormat" ), Options->PreviewMeshOptions.PayloadFormat );
							}
							EventAttributes.Emplace( TEXT( "BakeMaterials" ), Options->PreviewMeshOptions.bBakeMaterials );
							if ( Options->PreviewMeshOptions.bBakeMaterials )
							{
								FString BakedPropertiesString;
								{
									const UEnum* PropertyEnum = StaticEnum<EMaterialProperty>();
									for ( const FPropertyEntry& PropertyEntry : Options->PreviewMeshOptions.MaterialBakingOptions.Properties )
									{
										FString PropertyString = PropertyEnum->GetNameByValue( PropertyEntry.Property ).ToString();
										PropertyString.RemoveFromStart( TEXT( "MP_" ) );
										BakedPropertiesString += PropertyString + TEXT( ", " );
									}

									BakedPropertiesString.RemoveFromEnd( TEXT( ", " ) );
								}

								EventAttributes.Emplace( TEXT( "RemoveUnrealMaterials" ), Options->PreviewMeshOptions.bRemoveUnrealMaterials );
								EventAttributes.Emplace( TEXT( "BakedProperties" ), BakedPropertiesString );
								EventAttributes.Emplace( TEXT( "DefaultTextureSize" ), Options->PreviewMeshOptions.MaterialBakingOptions.DefaultTextureSize.ToString() );
							}
							EventAttributes.Emplace( TEXT( "LowestMeshLOD" ), LexToString( Options->PreviewMeshOptions.LowestMeshLOD ) );
							EventAttributes.Emplace( TEXT( "HighestMeshLOD" ), LexToString( Options->PreviewMeshOptions.HighestMeshLOD ) );
						}
					}

					IUsdClassesModule::SendAnalytics( MoveTemp( EventAttributes ), FString::Printf( TEXT( "Export.%s" ), *ClassName ), bAutomated, ElapsedSeconds, NumberOfFrames, Extension );
				}
			}
		}
	}
}

UAnimSequenceExporterUSD::UAnimSequenceExporterUSD()
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
	SupportedClass = UAnimSequence::StaticClass();
	bText = false;
#endif // #if USE_USD_SDK
}

bool UAnimSequenceExporterUSD::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
#if USE_USD_SDK
	UAnimSequence* AnimSequence = CastChecked< UAnimSequence >( Object );
	if ( !AnimSequence )
	{
		return false;
	}

	FScopedUsdMessageLog UsdMessageLog;

	UAnimSequenceExporterUSDOptions* Options = nullptr;
	if ( ExportTask )
	{
		Options = Cast<UAnimSequenceExporterUSDOptions>( ExportTask->Options );
	}
	if ( !Options && ( !ExportTask || !ExportTask->bAutomated ) )
	{
		Options = GetMutableDefault<UAnimSequenceExporterUSDOptions>();
		if ( Options )
		{
			Options->PreviewMeshOptions.MaterialBakingOptions.TexturesDir.Path = FPaths::Combine( FPaths::GetPath( UExporter::CurrentFilename ), TEXT( "Textures" ) );

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
	const FString& AssetFilename = UExporter::CurrentFilename;
	if ( Options && Options->PreviewMeshOptions.bUsePayload )
	{
		FString PathPart;
		FString FilenamePart;
		FString ExtensionPart;
		FPaths::Split( PayloadFilename, PathPart, FilenamePart, ExtensionPart );

		if ( FormatExtension.Contains( Options->PreviewMeshOptions.PayloadFormat ) )
		{
			ExtensionPart = Options->PreviewMeshOptions.PayloadFormat;
		}

		PayloadFilename = FPaths::Combine( PathPart, FilenamePart + TEXT( "_payload." ) + ExtensionPart );
	}

	UE::FUsdStage AssetStage = UnrealUSDWrapper::NewStage( *AssetFilename );
	if ( !AssetStage )
	{
		return false;
	}

	if ( Options )
	{
		UsdUtils::SetUsdStageMetersPerUnit( AssetStage, Options->StageOptions.MetersPerUnit );
		UsdUtils::SetUsdStageUpAxis( AssetStage, Options->StageOptions.UpAxis );
	}

	FString PrimPath;
	UE::FUsdPrim SkelRootPrim;
	USkeletalMesh* SkeletalMesh = nullptr;

	if ( Options && Options->bExportPreviewMesh )
	{
		SkeletalMesh = AnimSequence->GetPreviewMesh();
		USkeleton* AnimSkeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;

		if ( !AnimSkeleton && !SkeletalMesh )
		{
			AnimSkeleton = AnimSequence->GetSkeleton();
			SkeletalMesh = AnimSkeleton ? AnimSkeleton->GetAssetPreviewMesh( AnimSequence ) : nullptr;
		}

		if ( AnimSkeleton && !SkeletalMesh )
		{
			SkeletalMesh = AnimSkeleton->FindCompatibleMesh();
		}

		if ( SkeletalMesh )
		{
			PrimPath = TEXT( "/" ) + UsdUtils::SanitizeUsdIdentifier( *SkeletalMesh->GetName() );
			SkelRootPrim = AssetStage.DefinePrim( UE::FSdfPath( *PrimPath ), TEXT( "SkelRoot" ) );

			if ( SkelRootPrim )
			{
				AssetStage.SetDefaultPrim( SkelRootPrim );
			}

			// Using payload: Convert mesh data through the asset stage (that references the payload) so that we can
			// author mesh data on the payload layer and material data on the asset layer
			if ( Options->PreviewMeshOptions.bUsePayload )
			{
				if ( UE::FUsdStage PayloadStage = UnrealUSDWrapper::NewStage( *PayloadFilename ) )
				{
					UsdUtils::SetUsdStageMetersPerUnit( PayloadStage, Options->StageOptions.MetersPerUnit );
					UsdUtils::SetUsdStageUpAxis( PayloadStage, Options->StageOptions.UpAxis );

					if ( UE::FUsdPrim PayloadSkelRootPrim = PayloadStage.DefinePrim( UE::FSdfPath( *PrimPath ), TEXT( "SkelRoot" ) ) )
					{
						UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, PayloadSkelRootPrim, UsdUtils::GetDefaultTimeCode(), &AssetStage, Options->PreviewMeshOptions.LowestMeshLOD, Options->PreviewMeshOptions.HighestMeshLOD );
						PayloadStage.SetDefaultPrim( PayloadSkelRootPrim );
					}

					PayloadStage.GetRootLayer().Save();

					if ( SkelRootPrim )
					{
						UsdUtils::AddPayload( SkelRootPrim, *PayloadFilename );
					}
				}
			}
			// Not using payload: Just author everything on the current edit target of the payload (== asset) layer
			else
			{
				UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, SkelRootPrim, UsdUtils::GetDefaultTimeCode(), nullptr, Options->PreviewMeshOptions.LowestMeshLOD, Options->PreviewMeshOptions.HighestMeshLOD );
			}
		}
		else
		{
			FUsdLogManager::LogMessage(
				EMessageSeverity::Warning,
				FText::Format( NSLOCTEXT( "AnimSequenceExporterUSD", "InvalidSkelMesh", "Couldn't find the skeletal mesh to export for anim sequence {0}." ), FText::FromName( AnimSequence->GetFName() ) ) );
		}
	}

	PrimPath += TEXT( "/" ) + UsdUtils::SanitizeUsdIdentifier( *AnimSequence->GetName() );
	UE::FUsdPrim SkelAnimPrim = AssetStage.DefinePrim( UE::FSdfPath( *PrimPath ), TEXT("SkelAnimation") );
	if ( !SkelAnimPrim )
	{
		return false;
	}

	if ( !AssetStage.GetDefaultPrim() )
	{
		AssetStage.SetDefaultPrim( SkelAnimPrim );
	}

	const double StartTimeCode = 0.0;
	const double EndTimeCode = AnimSequence->GetNumberOfSampledKeys() - 1;
	UsdUtils::AddTimeCodeRangeToLayer( AssetStage.GetRootLayer(), StartTimeCode, EndTimeCode );

	AssetStage.SetTimeCodesPerSecond( AnimSequence->GetSamplingFrameRate().AsDecimal() );

	UnrealToUsd::ConvertAnimSequence( AnimSequence, SkelAnimPrim );

	if ( SkelRootPrim )
	{
		UsdUtils::BindAnimationSource( SkelRootPrim, SkelAnimPrim );
	}

	// Bake materials and replace unrealMaterials with references to the baked files.
	// Do this last because we will need to check the default prim of the stage during the traversal for replacing baked materials
	if ( Options && Options->bExportPreviewMesh && SkeletalMesh && Options->PreviewMeshOptions.bBakeMaterials )
	{
		TSet<UMaterialInterface*> MaterialsToBake;
		for ( const FSkeletalMaterial& SkeletalMaterial : SkeletalMesh->GetMaterials() )
		{
			MaterialsToBake.Add( SkeletalMaterial.MaterialInterface );
		}

		const bool bIsAssetLayer = true;
		UMaterialExporterUsd::ExportMaterialsForStage(
			MaterialsToBake.Array(),
			Options->PreviewMeshOptions.MaterialBakingOptions,
			AssetStage,
			bIsAssetLayer,
			Options->PreviewMeshOptions.bUsePayload,
			Options->PreviewMeshOptions.bRemoveUnrealMaterials
		);
	}

	AssetStage.GetRootLayer().Save();

	// Analytics
	{
		bool bAutomated = ExportTask ? ExportTask->bAutomated : false;
		double ElapsedSeconds = FPlatformTime::ToSeconds64( FPlatformTime::Cycles64() - StartTime );
		FString Extension = FPaths::GetExtension( UExporter::CurrentFilename );

		UE::AnimSequenceExporterUSD::Private::SendAnalytics(
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
// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimSequenceExporterUSD.h"

#include "AnimSequenceExporterUSDOptions.h"
#include "UnrealUSDWrapper.h"
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

UAnimSequenceExporterUSD::UAnimSequenceExporterUSD()
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

	if ( Options && Options->bExportPreviewMesh )
	{
		USkeletalMesh* SkeletalMesh = AnimSequence->GetPreviewMesh();
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

			// Using payload: Convert mesh data through the asset stage (that references the payload) so that we can
			// author mesh data on the payload layer and material data on the asset layer
			if ( Options->PreviewMeshOptions.bUsePayload )
			{
				if ( UE::FUsdStage PayloadStage = UnrealUSDWrapper::NewStage( *PayloadFilename ) )
				{
					UsdUtils::SetUsdStageMetersPerUnit( PayloadStage, Options->PreviewMeshOptions.StageOptions.MetersPerUnit );
					UsdUtils::SetUsdStageUpAxis( PayloadStage, Options->PreviewMeshOptions.StageOptions.UpAxis );

					if ( UE::FUsdPrim PayloadSkelRootPrim = PayloadStage.DefinePrim( UE::FSdfPath( *PrimPath ), TEXT( "SkelRoot" ) ) )
					{
						UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, PayloadSkelRootPrim, UsdUtils::GetDefaultTimeCode(), &AssetStage );
						PayloadStage.SetDefaultPrim( PayloadSkelRootPrim );
					}

					PayloadStage.GetRootLayer().Save();

					if ( SkelRootPrim )
					{
						AssetStage.SetDefaultPrim( SkelRootPrim );
						UsdUtils::AddPayload( SkelRootPrim, *PayloadFilename );
					}
				}
			}
			// Not using payload: Just author everything on the current edit target of the payload (== asset) layer
			else
			{
				UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, SkelRootPrim );
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
	const double EndTimeCode = AnimSequence->GetNumberOfFrames() - 1;
	UsdUtils::AddTimeCodeRangeToLayer( AssetStage.GetRootLayer(), StartTimeCode, EndTimeCode );
	AssetStage.SetTimeCodesPerSecond( AnimSequence->GetFrameRate() );

	UnrealToUsd::ConvertAnimSequence( AnimSequence, SkelAnimPrim );

	if ( SkelRootPrim )
	{
		UsdUtils::BindAnimationSource( SkelRootPrim, SkelAnimPrim );
	}

	AssetStage.GetRootLayer().Save();

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}
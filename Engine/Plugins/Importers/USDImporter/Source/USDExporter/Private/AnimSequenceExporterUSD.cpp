// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimSequenceExporterUSD.h"

#include "UnrealUSDWrapper.h"
#include "USDErrorUtils.h"
#include "USDLayerUtils.h"
#include "USDSkeletalDataConversion.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "Animation/AnimSequence.h"

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
	
	UE::FUsdStage UsdStage = UnrealUSDWrapper::NewStage( *UExporter::CurrentFilename );
	if ( !UsdStage )
	{
		return false;
	}

	FString PrimPath;
	UE::FUsdPrim SkelRootPrim;

	const bool bExportPreviewMesh = true; // Will be moved to an option window in the future
	if ( bExportPreviewMesh )
	{
		USkeleton* AnimSkeleton = AnimSequence->GetSkeleton();
		USkeletalMesh* SkeletalMesh = AnimSkeleton ? AnimSkeleton->GetAssetPreviewMesh(AnimSequence) : nullptr;

		if ( SkeletalMesh )
		{
			PrimPath = TEXT( "/" ) + SkeletalMesh->GetName();

			FScopedUsdAllocs Allocs;

			SkelRootPrim = UsdStage.DefinePrim( UE::FSdfPath( *PrimPath ), TEXT("SkelRoot") );
			if ( !SkelRootPrim )
			{
				return false;
			}

			if ( !UsdStage.GetDefaultPrim() )
			{
				UsdStage.SetDefaultPrim( SkelRootPrim );
			}

			UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, SkelRootPrim );
		}
		else
		{
			FUsdLogManager::LogMessage(
				EMessageSeverity::Warning,
				FText::Format( NSLOCTEXT( "AnimSequenceExporterUSD", "InvalidSkelMesh", "Couldn't find the skeletal mesh to export for anim sequence {0}." ), FText::FromName( AnimSequence->GetFName() ) ) );
		}
	}

	PrimPath += TEXT( "/" ) + AnimSequence->GetName();

	UE::FUsdPrim SkelAnimPrim = UsdStage.DefinePrim( UE::FSdfPath( *PrimPath ), TEXT("SkelAnimation") );
	if ( !SkelAnimPrim )
	{
		return false;
	}

	const double StartTimeCode = 0.0;
	const double EndTimeCode = AnimSequence->GetNumberOfSampledKeys() - 1;
	UsdUtils::AddTimeCodeRangeToLayer( UsdStage.GetRootLayer(), StartTimeCode, EndTimeCode );

	UsdStage.SetTimeCodesPerSecond( AnimSequence->GetSamplingFrameRate().AsDecimal() );
		
	UnrealToUsd::ConvertAnimSequence( AnimSequence, SkelAnimPrim );

	if ( SkelRootPrim )
	{
		UsdUtils::BindAnimationSource( SkelRootPrim, SkelAnimPrim );
	}

	if ( !UsdStage.GetDefaultPrim() )
	{
		UsdStage.SetDefaultPrim( SkelAnimPrim );
	}

	UsdStage.GetRootLayer().Save();

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}
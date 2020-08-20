// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshExporterUSD.h"

#include "USDConversionUtils.h"
#include "USDMemory.h"
#include "USDSkeletalDataConversion.h"
#include "USDTypesConversion.h"
#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

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

	UE::FUsdStage UsdStage = UnrealUSDWrapper::NewStage( *UExporter::CurrentFilename );

	if ( !UsdStage )
	{
		return false;
	}

	FString RootPrimPath = ( TEXT( "/" ) + SkeletalMesh->GetName() );

	FScopedUsdAllocs Allocs;

	UE::FUsdPrim RootPrim = UsdStage.DefinePrim( UE::FSdfPath( *RootPrimPath ), TEXT("SkelRoot") );
	if ( !RootPrim )
	{
		return false;
	}

	UsdStage.SetDefaultPrim( RootPrim );

	UnrealToUsd::ConvertSkeletalMesh( SkeletalMesh, RootPrim );

	UsdStage.GetRootLayer().Save();

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

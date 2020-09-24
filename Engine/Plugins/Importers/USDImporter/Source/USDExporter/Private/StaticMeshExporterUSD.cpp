// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshExporterUSD.h"

#include "USDConversionUtils.h"
#include "USDGeomMeshConversion.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#include "Engine/StaticMesh.h"


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

	{
		UE::FUsdStage UsdStage = UnrealUSDWrapper::NewStage( *UExporter::CurrentFilename );

		if ( !UsdStage )
		{
			return false;
		}

		FString RootPrimPath = ( TEXT("/") + StaticMesh->GetName() );

		UE::FUsdPrim RootPrim = UsdStage.DefinePrim( UE::FSdfPath( *RootPrimPath ) );
		if ( !RootPrim )
		{
			return false;
		}

		UsdStage.SetDefaultPrim( RootPrim );

		UnrealToUsd::ConvertStaticMesh( StaticMesh, RootPrim );

		UsdStage.GetRootLayer().Save();
	}

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

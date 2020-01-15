// Copyright Epic Games, Inc. All Rights Reserved.

#include "StaticMeshExporterUSD.h"

#include "USDGeomMeshConversion.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "Engine/StaticMesh.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

UStaticMeshExporterUsd::UStaticMeshExporterUsd()
{
#if USE_USD_SDK
	FormatExtension.Append( { TEXT("usd"), TEXT("usda") } );
	FormatDescription.Append( {TEXT("USD File"), TEXT("USD File") } );
	SupportedClass = UStaticMesh::StaticClass();
	bText = false;
#endif // #if USE_USD_SDK
}

bool UStaticMeshExporterUsd::ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn, int32 FileIndex, uint32 PortFlags )
{
#if USE_USD_SDK
	UStaticMesh* StaticMesh = CastChecked< UStaticMesh >( Object );

	{
		FScopedUsdAllocs UsdAllocs;

		TUsdStore< pxr::UsdStageRefPtr > UsdStageStore = pxr::UsdStage::CreateNew( UnrealToUsd::ConvertString( *UExporter::CurrentFilename ).Get() );
		pxr::UsdStageRefPtr UsdStage = UsdStageStore.Get();

		if ( !UsdStage )
		{
			return false;
		}

		// Set up axis
		UsdUtils::SetUsdStageAxis( UsdStage, pxr::UsdGeomTokens->z );

		FString RootPrimPath = ( TEXT("/") + StaticMesh->GetName() );

		pxr::UsdPrim RootPrim = UsdStage->DefinePrim( UnrealToUsd::ConvertPath( *RootPrimPath ).Get() );

		if ( !RootPrim )
		{
			return false;
		}

		FString MeshPrimPath = FPaths::Combine( RootPrimPath, StaticMesh->GetName() );

		pxr::UsdGeomMesh UsdGeomMesh = pxr::UsdGeomMesh::Define( UsdStage, UnrealToUsd::ConvertPath( *MeshPrimPath ).Get() );

		if ( UsdGeomMesh )
		{
			// Set default prim
			UsdStage->SetDefaultPrim( RootPrim );

			UnrealToUsd::ConvertStaticMesh( StaticMesh, UsdGeomMesh );
		}

		UsdStage->GetRootLayer()->Save();
	}

	return true;
#else
	return false;
#endif // #if USE_USD_SDK
}

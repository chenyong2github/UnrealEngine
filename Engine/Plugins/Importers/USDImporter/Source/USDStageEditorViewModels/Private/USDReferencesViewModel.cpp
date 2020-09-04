// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDReferencesViewModel.h"

#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/sdf/reference.h"
	#include "pxr/usd/usd/prim.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

void FUsdReferencesViewModel::UpdateReferences( const UE::FUsdStage& UsdStage, const TCHAR* PrimPath )
{
	References.Reset();

	if ( !UsdStage )
	{
		return;
	}

#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	pxr::UsdPrim Prim( UsdStage.GetPrimAtPath( UE::FSdfPath( PrimPath ) ) );

	// Retrieve the strongest opinion prim spec, hopefully it's enough to get all references
	pxr::SdfPrimSpecHandle PrimSpec = Prim.GetPrimStack().size() > 0 ? Prim.GetPrimStack()[0] : pxr::SdfPrimSpecHandle();

	if ( PrimSpec )
	{
		pxr::SdfReferencesProxy ReferencesProxy = PrimSpec->GetReferenceList();

		for ( const pxr::SdfReference& UsdReference : ReferencesProxy.GetAddedOrExplicitItems() )
		{
			FUsdReference Reference;
			Reference.AssetPath = UsdToUnreal::ConvertString( UsdReference.GetAssetPath() );

			References.Add( MakeSharedUnreal< FUsdReference >( MoveTemp( Reference ) ) );
		}
	}
#endif // #if USE_USD_SDK
}
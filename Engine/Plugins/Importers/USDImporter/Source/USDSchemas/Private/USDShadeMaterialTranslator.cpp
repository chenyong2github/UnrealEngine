// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDShadeMaterialTranslator.h"

#include "USDGeomMeshConversion.h"
#include "USDTypesConversion.h"

#include "Materials/Material.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usdShade/material.h"
#include "USDIncludesEnd.h"


void FUsdShadeMaterialTranslator::CreateAssets()
{
	pxr::UsdShadeMaterial ShadeMaterial( Schema.Get() );

	if ( !ShadeMaterial )
	{
		return;
	}

	UObject*& CachedMaterial = Context->AssetsCache.FindOrAdd( UsdToUnreal::ConvertPath( ShadeMaterial.GetPath() ) );

	if ( !CachedMaterial )
	{
		UMaterial* NewMaterial = NewObject< UMaterial >();

		if ( UsdToUnreal::ConvertMaterial( ShadeMaterial, *NewMaterial ) )
		{
			//UMaterialEditingLibrary::RecompileMaterial( CachedMaterial ); // Too slow
			NewMaterial->PostEditChange();
		}
		else
		{
			NewMaterial = nullptr;
		}

		CachedMaterial = NewMaterial;
	}

	FScopeLock Lock( &Context->CriticalSection );
	{
		Context->PrimPathsToAssets.Add( UsdToUnreal::ConvertPath( Schema.Get().GetPath() ), CachedMaterial );
	}
}

#endif // #if USE_USD_SDK

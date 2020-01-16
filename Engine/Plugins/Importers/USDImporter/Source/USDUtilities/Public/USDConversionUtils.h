// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "USDMemory.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/base/gf/matrix4f.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "USDIncludesEnd.h"

namespace UsdUtils
{
	template<typename T>
	T* FindOrCreateObject(UObject* InParent, const FString& InName, EObjectFlags Flags)
	{
		T* Object = FindObject<T>(InParent, *InName);

		if (!Object)
		{
			Object = NewObject<T>(InParent, FName(*InName), Flags);
		}

		return Object;
	}

	template<typename T>
	static T GetUsdValue( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime() )
	{
		T Value = T();
		if(Attribute)
		{
			Attribute.Get( &Value, TimeCode );
		}

		return Value;
	}

	static pxr::TfToken GetUsdStageAxis(pxr::UsdStageRefPtr Stage)
	{
		if (Stage->HasAuthoredMetadata(pxr::UsdGeomTokens->upAxis))
		{
			pxr::TfToken Axis;
			Stage->GetMetadata(pxr::UsdGeomTokens->upAxis, &Axis);
			return Axis;
		}

		return pxr::UsdGeomTokens->z;
	}

	static void SetUsdStageAxis( pxr::UsdStageRefPtr Stage, pxr::TfToken Axis )
	{
		Stage->SetMetadata( pxr::UsdGeomTokens->upAxis, Axis );
	}

	inline bool HasCompositionArcs( const pxr::UsdPrim& Prim )
	{
		if ( !Prim )
		{
			return false;
		}

		return Prim.HasAuthoredReferences() || Prim.HasPayload() || Prim.HasAuthoredInherits() || Prim.HasAuthoredSpecializes() || Prim.HasVariantSets();
	}

	USDUTILITIES_API UClass* GetActorTypeForPrim( const pxr::UsdPrim& Prim );

	USDUTILITIES_API UClass* GetComponentTypeForPrim( const pxr::UsdPrim& Prim );

	USDUTILITIES_API TUsdStore< pxr::TfToken > GetUVSetName( int32 UVChannelIndex );

	USDUTILITIES_API bool IsAnimated( const pxr::UsdPrim& Prim );

	/**
	 * Returns all prims of type SchemaType (or a descendant type) in the subtree of prims rooted at StartPrim.
	 * Stops going down the subtrees when it hits a schema type to exclude.
	 */
	USDUTILITIES_API TArray< TUsdStore< pxr::UsdPrim > > GetAllPrimsOfType( const pxr::UsdPrim& StartPrim, const pxr::TfType& SchemaType, const TArray< TUsdStore< pxr::TfType > >& ExcludeSchemaTypes = {} );
}

#endif // #if USE_USD_SDK


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"

#include "USDMemory.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class GfMatrix4d;
	class GfVec2f;
	class GfVec3f;
	class GfVec4f;
	class SdfPath;
	class TfToken;
	class TfType;
	class UsdAttribute;
	class UsdPrim;
	class UsdTimeCode;

	class UsdStage;
	template< typename T > class TfRefPtr;

	using UsdStageRefPtr = TfRefPtr< UsdStage >;
PXR_NAMESPACE_CLOSE_SCOPE

#endif // #if USE_USD_SDK

namespace UE
{
	class FUsdPrim;
}

namespace UsdUtils
{
	template<typename T>
	T* FindOrCreateObject( UObject* InParent, const FString& InName, EObjectFlags Flags )
	{
		T* Object = FindObject<T>(InParent, *InName);

		if (!Object)
		{
			Object = NewObject<T>(InParent, FName(*InName), Flags);
		}

		return Object;
	}

#if USE_USD_SDK
	template< typename ValueType >
	USDUTILITIES_API ValueType GetUsdValue( const pxr::UsdAttribute& Attribute, pxr::UsdTimeCode TimeCode );

	USDUTILITIES_API pxr::TfToken GetUsdStageAxis( const pxr::UsdStageRefPtr& Stage );
	USDUTILITIES_API void SetUsdStageAxis( const pxr::UsdStageRefPtr& Stage, pxr::TfToken Axis );

	USDUTILITIES_API float GetUsdStageMetersPerUnit( const pxr::UsdStageRefPtr& Stage );
	USDUTILITIES_API void SetUsdStageMetersPerUnit( const pxr::UsdStageRefPtr& Stage, float MetersPerUnit );

	USDUTILITIES_API bool HasCompositionArcs( const pxr::UsdPrim& Prim );

	USDUTILITIES_API UClass* GetActorTypeForPrim( const pxr::UsdPrim& Prim );

	USDUTILITIES_API UClass* GetComponentTypeForPrim( const pxr::UsdPrim& Prim );

	USDUTILITIES_API TUsdStore< pxr::TfToken > GetUVSetName( int32 UVChannelIndex );

	USDUTILITIES_API bool IsAnimated( const pxr::UsdPrim& Prim );

	/**
	 * Returns all prims of type SchemaType (or a descendant type) in the subtree of prims rooted at StartPrim.
	 * Stops going down the subtrees when it hits a schema type to exclude.
	 */
	USDUTILITIES_API TArray< TUsdStore< pxr::UsdPrim > > GetAllPrimsOfType( const pxr::UsdPrim& StartPrim, const pxr::TfType& SchemaType, const TArray< TUsdStore< pxr::TfType > >& ExcludeSchemaTypes = {} );
	USDUTILITIES_API TArray< TUsdStore< pxr::UsdPrim > > GetAllPrimsOfType( const pxr::UsdPrim& StartPrim, const pxr::TfType& SchemaType, TFunction< bool( const pxr::UsdPrim& ) > PruneChildren, const TArray< TUsdStore< pxr::TfType > >& ExcludeSchemaTypes = {} );

	USDUTILITIES_API FString GetAssetPathFromPrimPath( const FString& RootContentPath, const pxr::UsdPrim& Prim );
#endif // #if USE_USD_SDK

	USDUTILITIES_API TArray< UE::FUsdPrim > GetAllPrimsOfType( const UE::FUsdPrim& StartPrim, const TCHAR* SchemaName );
	USDUTILITIES_API TArray< UE::FUsdPrim > GetAllPrimsOfType( const UE::FUsdPrim& StartPrim, const TCHAR* SchemaName, TFunction< bool( const UE::FUsdPrim& ) > PruneChildren );

	USDUTILITIES_API bool IsAnimated( const UE::FUsdPrim& Prim );
}


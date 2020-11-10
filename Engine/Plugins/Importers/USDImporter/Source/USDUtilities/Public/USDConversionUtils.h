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
	class UsdGeomPrimvar;
	class UsdGeomMesh;

	class UsdStage;
	template< typename T > class TfRefPtr;

	using UsdStageRefPtr = TfRefPtr< UsdStage >;
PXR_NAMESPACE_CLOSE_SCOPE

#endif // #if USE_USD_SDK

class UUsdAssetImportData;
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

	/** Case sensitive hashing function for TMap */
	template <typename ValueType>
	struct FCaseSensitiveStringMapFuncs : BaseKeyFuncs<ValueType, FString, /*bInAllowDuplicateKeys*/false>
	{
		static FORCEINLINE const FString& GetSetKey( const TPair<FString, ValueType>& Element )
		{
			return Element.Key;
		}
		static FORCEINLINE bool Matches( const FString& A, const FString& B )
		{
			return A.Equals( B, ESearchCase::CaseSensitive );
		}
		static FORCEINLINE uint32 GetKeyHash( const FString& Key )
		{
			return FCrc::StrCrc32<TCHAR>( *Key );
		}
	};

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

	/**
	 * Heuristic to try and guess what UV index we should assign this primvar to.
	 * We need something like this because one material may use st0, and another st_0 (both meaning the same thing),
	 * but a mesh that binds both materials may interpret these as targeting completely different UV sets
	 * @param PrimvarName - Name of the primvar that should be used as UV set
	 * @return UV index that should be used for this primvar
	 */
	USDUTILITIES_API int32 GetPrimvarUVIndex( FString PrimvarName );

	/**
	 * Gets the names of the primvars that should be used as UV sets, per index, for this mesh.
	 * (e.g. first item of array is primvar for UV set 0, second for UV set 1, etc).
	 * This overload will only return primvars with 'texcoord2f' role.	 *
	 * @param UsdMesh - Mesh that contains primvars that can be used as texture coordinates.
	 * @return Array where each index gives the primvar that should be used for that UV index
	 */
	USDUTILITIES_API TArray< TUsdStore< pxr::UsdGeomPrimvar > > GetUVSetPrimvars( const pxr::UsdGeomMesh& UsdMesh );

	/**
	 * Gets the names of the primvars that should be used as UV sets, per index, for this mesh.
	 * (e.g. first item of array is primvar for UV set 0, second for UV set 1, etc).
	 * This overload will only return primvars with 'texcoord2f' role.	 *
	 * @param UsdMesh - Mesh that contains primvars that can be used as texture coordinates.
	 * @param MaterialToPrimvarsUVSetNames - Maps from a material prim path, to pairs indicating which primvar names are used as 'st' coordinates, and which UVIndex the imported material will sample from (e.g. ["st0", 0], ["myUvSet2", 2], etc). These are supposed to be the materials used by the mesh, and we do this because it helps identify which primvars are valid/used as texture coordinates, as the user may have these named as 'myUvSet2' and still expect it to work
	 * @return Array where each index gives the primvar that should be used for that UV index
	 */
	USDUTILITIES_API TArray< TUsdStore< pxr::UsdGeomPrimvar > > GetUVSetPrimvars( const pxr::UsdGeomMesh& UsdMesh, const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarsUVSetNames );

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
	USDUTILITIES_API TArray< UE::FUsdPrim > GetAllPrimsOfType( const UE::FUsdPrim& StartPrim, const TCHAR* SchemaName, TFunction< bool( const UE::FUsdPrim& ) > PruneChildren, const TArray<const TCHAR*>& ExcludeSchemaNames = {} );

	USDUTILITIES_API bool IsAnimated( const UE::FUsdPrim& Prim );

	/** Returns the time code for non-timesampled values. Usually a quiet NaN. */
	USDUTILITIES_API double GetDefaultTimeCode();

	USDUTILITIES_API UUsdAssetImportData* GetAssetImportData( UObject* Asset );
}


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "CoreMinimal.h"

#include "UsdWrappers/SdfPath.h"

struct FUsdSchemaTranslationContext;
namespace UE
{
	class FSdfPath;
	class FUsdPrim;
}

enum class ECollapsingType
{
	Assets,
	Components
};

/**
 * Caches whether given prim paths represent prims that can have their assets or components collapsed when importing/opening
 * a USD Stage.
 */
struct USDSCHEMAS_API FUsdCollapsingCache
{
	bool Serialize( FArchive& Ar );

	bool IsPathCollapsed( const UE::FSdfPath& Path, ECollapsingType CollapsingType ) const;
	bool DoesPathCollapseChildren( const UE::FSdfPath& Path, ECollapsingType CollapsingType ) const;

	// Returns Path in case it represents an uncollapsed prim, or returns the path to the prim that collapsed it
	UE::FSdfPath UnwindToNonCollapsedPath( const UE::FSdfPath& Path, ECollapsingType CollapsingType ) const;

	void RebuildCacheForSubtree( const UE::FUsdPrim& Prim, FUsdSchemaTranslationContext& Context );

	void Clear();
	bool IsEmpty();

private:
	mutable FRWLock Lock;
	TMap< UE::FSdfPath, UE::FSdfPath > AssetPathsToCollapsedRoot;
	TMap< UE::FSdfPath, UE::FSdfPath > ComponentPathsToCollapsedRoot;
};
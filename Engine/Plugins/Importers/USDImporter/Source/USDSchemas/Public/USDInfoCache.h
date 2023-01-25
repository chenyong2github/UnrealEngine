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
 * Caches information about a specific USD Stage
 */
class USDSCHEMAS_API FUsdInfoCache
{
public:
	struct FUsdInfoCacheImpl;

	FUsdInfoCache();
	virtual ~FUsdInfoCache();

	bool Serialize(FArchive& Ar);

	// Returns whether we contain any info about prim at 'Path' at all
	bool ContainsInfoAboutPrim(const UE::FSdfPath& Path) const;

	void RebuildCacheForSubtree(const UE::FUsdPrim& Prim, FUsdSchemaTranslationContext& Context);

	void Clear();
	bool IsEmpty();

public:
	bool IsPathCollapsed(const UE::FSdfPath& Path, ECollapsingType CollapsingType) const;
	bool DoesPathCollapseChildren(const UE::FSdfPath& Path, ECollapsingType CollapsingType) const;

	// Returns Path in case it represents an uncollapsed prim, or returns the path to the prim that collapsed it
	UE::FSdfPath UnwindToNonCollapsedPath(const UE::FSdfPath& Path, ECollapsingType CollapsingType) const;

public:
	// Provides the total vertex or material slots counts for each prim *and* its subtree.
	// This is built inside RebuildCacheForSubtree, so it will factor in the used Context's bMergeIdenticalMaterialSlots.
	// Note that these aren't affected by actual collapsing: A prim that doesn't collapse its children will still
	// provide the total sum of vertex counts of its entire subtree when queried
	TOptional<uint64> GetSubtreeVertexCount(const UE::FSdfPath& Path);
	TOptional<uint64> GetSubtreeMaterialSlotCount(const UE::FSdfPath& Path);

public:
	void LinkAssetToPrim(const UE::FSdfPath& Path, UObject* Asset);

	TSet<TWeakObjectPtr<UObject>> RemoveAllAssetPrimLinks(const UE::FSdfPath& Path);

	UObject* GetSingleAssetForPrim(const UE::FSdfPath& Path, const UClass* FilterClass = nullptr) const;
	TSet<TWeakObjectPtr<UObject>> GetAssetsForPrim(const UE::FSdfPath& Path, const UClass* FilterClass = nullptr) const;

	UE::FSdfPath GetPrimForAsset(UObject* Asset) const;
	TMap<UE::FSdfPath, TSet<TWeakObjectPtr<UObject>>> GetAllAssetPrimLinks() const;

private:
	TUniquePtr<FUsdInfoCacheImpl> Impl;
};


// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "CoreMinimal.h"
#include "UObject/GCObject.h"

/** Owns the assets generated and reused by the USD stage, allowing thread-safe retrieval/storage */
class USDUTILITIES_API FUsdAssetCache final : public FGCObject
{
public:
	FUsdAssetCache() = default;

	// Temporary constructor to allow quickly building one of these while we still maintain deprecated signatures to functions
	// that receive asset caches as direct maps
	explicit FUsdAssetCache( TMap< FString, UObject* >& InHashToAssets );

	//~ Begin FGCObject interface
	virtual void AddReferencedObjects( FReferenceCollector& Collector ) override;
	//~ End FGCObject interface

	void CacheAsset( const FString& Hash, UObject* Asset, const FString& PrimPath = FString() );
	void DiscardAsset( const FString& Hash );
	UObject* GetCachedAsset( const FString& Hash ) const;
	TMap< FString, UObject* > GetCachedAssets() const { return HashToAssets; }; // Can't return a reference as it wouldn't be thread-safe

	void LinkAssetToPrim( const FString& PrimPath, UObject* Asset );
	void RemoveAssetPrimLink( const FString& PrimPath );
	UObject* GetAssetForPrim( const FString& PrimPath ) const;
	TMap< FString, UObject* > GetAssetPrimLinks() const { return PrimPathToAssets; }; // Can't return a reference as it wouldn't be thread-safe

	int32 GetNumAssets() const { return HashToAssets.Num(); }
	void Reset();

	/**
	 * Every time an asset is retrieved/inserted we add it to ActiveAssets. With these functions that container can be reset/returned.
	 *
	 * When importing via the USDStageImporter we will move assets from this cache to the content folder.
	 * The problem is that some of those assets may not be currently used (e.g. inactive variant, purpose, etc.).
	 * The USDStageImporter will then use these functions before/after translating the scene to know which items from the
	 * cache that are actually used for the current scene.
	 */
	void MarkAssetsAsStale();

	/** Returns assets that aren't marked as stale */
	TSet<UObject*> GetActiveAssets() const;

	// We need to be serializable so that AUsdStageActor can duplicate us for PIE
	USDUTILITIES_API friend FArchive& operator<<( FArchive&, FUsdAssetCache& );

private:
	// Primary storage
	TMap< FString, UObject* > HashToAssets;

	// Points to the assets in primary storage, used to quickly check if we own an asset
	TSet< UObject* > OwnedAssets;

	// Keeps associations from prim paths to assets that we own in primary storage
    TMap< FString, UObject* > PrimPathToAssets;

	// Assets that were added/retrieved since the last call to MarkAssetsAsSlate();
	mutable TSet<UObject*> ActiveAssets;

	// Used to ensure thread safety
	mutable FCriticalSection CriticalSection;
};


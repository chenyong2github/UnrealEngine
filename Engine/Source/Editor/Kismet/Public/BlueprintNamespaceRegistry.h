// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAssetData;
struct FBlueprintNamespacePathTree;

/**
 * A shared utility class that keeps track of registered Blueprint namespace identifiers sourced from objects and assets in the editor.
 */
class KISMET_API FBlueprintNamespaceRegistry
{
public:
	~FBlueprintNamespaceRegistry();

	/**
	 * Provides public singleton access.
	 */
	static FBlueprintNamespaceRegistry& Get();

	/**
	 * One-time initialization method; separated from the ctor so it can be called explicitly.
	 */
	void Initialize();

	/**
	 * One-time shutdown method; separated from the dtor so it can be called explicitly.
	 */
	void Shutdown();

	/**
	 * @return TRUE if the given path identifier is currently registered.
	 */
	bool IsRegisteredPath(const FString& InPath) const;

	/**
	 * @param InPath	Path identifier string (e.g. "X.Y" or "X.Y.").
	 * @param OutNames	On output, an array containing the set of names rooted to the given path (e.g. "Z" in "X.Y.Z").
	 */
	void GetNamesUnderPath(const FString& InPath, TArray<FName>& OutNames) const;

	/**
	 * @param OutPaths	On output, contains the full set of all currently-registered namespace identifier paths.
	 */
	void GetAllRegisteredPaths(TArray<FString>& OutPaths) const;

	/**
	 * Adds an explicit namespace identifier to the registry if not already included.
	 * 
	 * @param InPath	Path identifier string (e.g. "X.Y").
	 */
	void RegisterNamespace(const FString& InPath);

protected:
	FBlueprintNamespaceRegistry();

	/** Asset registry event handler methods. */
	void OnAssetAdded(const FAssetData& AssetData);
	void OnAssetRemoved(const FAssetData& AssetData);
	void OnAssetRenamed(const FAssetData& AssetData, const FString& InOldName);

	/** Namespace identifier registration methods. */
	void FindAndRegisterAllNamespaces();
	void RegisterNamespace(const UObject* InObject);
	void RegisterNamespace(const FAssetData& AssetData);

	/** Console command implementations (debugging/testing). */
	void ToggleDefaultNamespace();
	void DumpAllRegisteredPaths();
	void OnDefaultNamespaceTypeChanged();

private:
	/** Indicates whether the registry has been initialized. */
	bool bIsInitialized;

	/** Delegate handles to allow for deregistration on shutdown. */
	FDelegateHandle OnAssetAddedDelegateHandle;
	FDelegateHandle OnAssetRemovedDelegateHandle;
	FDelegateHandle OnAssetRenamedDelegateHandle;
	FDelegateHandle OnDefaultNamespaceTypeChangedDelegateHandle;

	/** Handles storage and retrieval for namespace path identifiers. */
	TUniquePtr<FBlueprintNamespacePathTree> PathTree;
};
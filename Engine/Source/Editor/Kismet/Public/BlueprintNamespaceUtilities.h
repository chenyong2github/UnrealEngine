// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FAssetData;

/** Default namespace type for objects/assets if one is not explicitly assigned. */
enum class EDefaultBlueprintNamespaceType
{
	/** All objects/assets belong to the global namespace by default. */
	DefaultToGlobalNamespace,

	/** All objects/assets base their default namespace on the underlying package path. */
	UsePackagePathAsDefaultNamespace,
};

/** A wrapper struct around various Blueprint namespace utility and support methods. */
struct KISMET_API FBlueprintNamespaceUtilities
{
public:
	/**
	 * Analyzes the given asset to determine its explicitly-assigned namespace identifier, or otherwise returns its default namespace.
	 * 
	 * @param InAssetData	Input asset data.
	 * @return The unique Blueprint namespace identifier associated with the given asset, or an empty string if the asset belongs to the global namespace (default).
	 */
	static FString GetAssetNamespace(const FAssetData& InAssetData);

	/**
	 * Analyzes the given object to determine its explicitly-assigned namespace identifier, or otherwise returns its default namespace.
	 * 
	 * @param InObject	A reference to the input object.
	 * @return The unique Blueprint namespace identifier associated with the given object, or an empty string if the object belongs to the global namespace (default).
	 */
	static FString GetObjectNamespace(const UObject* InObject);

	/**
	 * Sets the default Blueprint namespace type that objects/assets should use when not explicitly assigned.
	 *
	 * @param InType		Default namespace type to use.
	 */
	static void SetDefaultBlueprintNamespaceType(EDefaultBlueprintNamespaceType InType);

	/** @return The default Blueprint namespace type objects/assets should use. Currently used for debugging/testing. */
	static EDefaultBlueprintNamespaceType GetDefaultBlueprintNamespaceType();

	/** Delegate invoked whenever the default Blueprint namespace type changes. */
	DECLARE_MULTICAST_DELEGATE(FOnDefaultBlueprintNamespaceTypeChanged);
	static FOnDefaultBlueprintNamespaceTypeChanged& OnDefaultBlueprintNamespaceTypeChanged();
};
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "Elements/Framework/TypedElementLimits.h"

/**
 * Non-templated base class for the asset editor customization registry.
 */
class EDITORFRAMEWORK_API FTypedElementAssetEditorCustomizationRegistryBase
{
public:
	virtual ~FTypedElementAssetEditorCustomizationRegistryBase() = default;

protected:
	/**
	 * Given an element name, attempt to get its registered type ID from the global registry.
	 * @return The registered type ID, or 0 if the element name is not registered.
	 */
	FTypedHandleTypeId GetElementTypeIdFromName(const FName InElementTypeName) const;

	/**
	 * Given an element name, attempt to get its registered type ID from the global registry.
	 * @return The registered type ID, or asserts if the element name is not registered.
	 */
	FTypedHandleTypeId GetElementTypeIdFromNameChecked(const FName InElementTypeName) const;
};

/**
 * Utility to register and retrieve asset editor customizations for a given type.
 */
template <typename CustomizationBaseType, typename DefaultCustomizationType = CustomizationBaseType>
class TTypedElementAssetEditorCustomizationRegistry : public FTypedElementAssetEditorCustomizationRegistryBase
{
public:
	template <typename... TDefaultArgs>
	TTypedElementAssetEditorCustomizationRegistry(TDefaultArgs&&... DefaultArgs)
		: DefaultAssetEditorCustomization(MakeUnique<DefaultCustomizationType>(Forward<TDefaultArgs>(DefaultArgs)...))
	{
	}

	virtual ~TTypedElementAssetEditorCustomizationRegistry() = default;

	TTypedElementAssetEditorCustomizationRegistry(const TTypedElementAssetEditorCustomizationRegistry&) = delete;
	TTypedElementAssetEditorCustomizationRegistry& operator=(const TTypedElementAssetEditorCustomizationRegistry&) = delete;

	TTypedElementAssetEditorCustomizationRegistry(TTypedElementAssetEditorCustomizationRegistry&&) = delete;
	TTypedElementAssetEditorCustomizationRegistry& operator=(TTypedElementAssetEditorCustomizationRegistry&&) = delete;

	/**
	 * Set the default asset editor customization instance.
	 */
	void SetDefaultAssetEditorCustomization(TUniquePtr<CustomizationBaseType>&& InAssetEditorCustomization)
	{
		checkf(InAssetEditorCustomization, TEXT("Default asset editor customization cannot be null!"));
		DefaultAssetEditorCustomization = MoveTemp(InAssetEditorCustomization);
	}

	/**
	 * Register an asset editor customization for the given element type.
	 */
	void RegisterAssetEditorCustomizationByTypeName(const FName InElementTypeName, TUniquePtr<CustomizationBaseType>&& InAssetEditorCustomization)
	{
		RegisterAssetEditorCustomizationByTypeId(GetElementTypeIdFromNameChecked(InElementTypeName), MoveTemp(InAssetEditorCustomization));
	}

	/**
	 * Register an asset editor customization for the given element type.
	 */
	void RegisterAssetEditorCustomizationByTypeId(const FTypedHandleTypeId InElementTypeId, TUniquePtr<CustomizationBaseType>&& InAssetEditorCustomization)
	{
		RegisteredAssetEditorCustomizations[InElementTypeId - 1] = MoveTemp(InAssetEditorCustomization);
	}

	/**
	 * Unregister an asset editor customization for the given element type.
	 */
	void UnregisterAssetEditorCustomizationByTypeName(const FName InElementTypeName)
	{
		UnregisterAssetEditorCustomizationByTypeId(GetElementTypeIdFromNameChecked(InElementTypeName));
	}

	/**
	 * Unregister an asset editor customization for the given element type.
	 */
	void UnregisterAssetEditorCustomizationByTypeId(const FTypedHandleTypeId InElementTypeId)
	{
		RegisteredAssetEditorCustomizations[InElementTypeId - 1].Reset();
	}

	/**
	 * Get the asset editor customization for the given element type.
	 * @note If bAllowFallback is true, then this will return the default asset editor customization if no override is present, otherwise it will return null.
	 */
	CustomizationBaseType* GetAssetEditorCustomizationByTypeName(const FName InElementTypeName, const bool bAllowFallback = true) const
	{
		return GetAssetEditorCustomizationByTypeId(GetElementTypeIdFromNameChecked(InElementTypeName), bAllowFallback);
	}

	/**
	 * Get the asset editor customization for the given element type.
	 * @note If bAllowFallback is true, then this will return the default asset editor customization if no override is present, otherwise it will return null.
	 */
	CustomizationBaseType* GetAssetEditorCustomizationByTypeId(const FTypedHandleTypeId InElementTypeId, const bool bAllowFallback = true) const
	{
		CustomizationBaseType* AssetEditorCustomization = RegisteredAssetEditorCustomizations[InElementTypeId - 1].Get();
		return AssetEditorCustomization
			? AssetEditorCustomization
			: bAllowFallback 
				? DefaultAssetEditorCustomization.Get()
				: nullptr;
	}

private:
	/** Default asset editor customization, used if no type-specific override is present. */
	TUniquePtr<CustomizationBaseType> DefaultAssetEditorCustomization;

	/** Array of registered asset editor customizations, indexed by ElementTypeId-1. */
	TUniquePtr<CustomizationBaseType> RegisteredAssetEditorCustomizations[TypedHandleMaxTypeId - 1];
};

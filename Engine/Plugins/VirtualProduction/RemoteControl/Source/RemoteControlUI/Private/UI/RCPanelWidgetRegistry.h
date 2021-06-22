// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class IDetailTreeNode;
class IPropertyRowGenerator;
class FStructOnScope;

enum class ERCFindNodeMethod
{
	Name, // Find a node by providing its property name.
	Path  // Find a node by providing a property path.
};

class FRCPanelWidgetRegistry
{
public:
	/**
	 * Get a detail tree node for a given object and property.
	 * @param InObject the object used to generate the detail row.
	 * @param InField a string specifying which field to generate a row for.
	 * @param InFindMethod whether the field parameter is a path or a field name.
	 */
	TSharedPtr<IDetailTreeNode> GetObjectTreeNode(UObject* InObject, const FString& InField, ERCFindNodeMethod InFindMethod);

	/**
	 * Get a detail tree node for a given struct and field.
	 * @param InStruct the struct used to generate the detail row.
	 * @param InField a string specifying which field to generate a row for.
	 * @param InFindMethod whether the field parameter is a path or a field name.
	 */
	TSharedPtr<IDetailTreeNode> GetStructTreeNode(const TSharedPtr<FStructOnScope>& InStruct, const FString& InField, ERCFindNodeMethod InFindMethod);

	/**
	 * Update the generator for a given object.
	 */
	void Refresh(UObject* InObject);

	/**
	 * Update the generator for a given struct.
	 */
	void Refresh(const TSharedPtr<FStructOnScope>& InStruct);

	/**
	 * Clear the registry and its cache.
	 */
	void Clear();

private:
	/** Map of objects to row generator, used to have one row generator per object. */
	TMap<TWeakObjectPtr<UObject>, TSharedPtr<IPropertyRowGenerator>> ObjectToRowGenerator;
	/** Map of struct on scope to row generator, used to have one row generator per struct ptr. */
	TMap<TSharedPtr<FStructOnScope>, TSharedPtr<IPropertyRowGenerator>> StructToRowGenerator;
	/** Cache of Object&Field to tree nodes. */
	TMap<TPair<TWeakObjectPtr<UObject>, FString>, TWeakPtr<IDetailTreeNode>> TreeNodeCache;
};

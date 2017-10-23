// Copyright 1998-2017 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PropertyEditorModule.h"

class IDetailTreeNode;

struct FPropertyRowGeneratorArgs
{
	FPropertyRowGeneratorArgs()
		: NotifyHook(nullptr)
		, DefaultsOnlyVisibility(EEditDefaultsOnlyNodeVisibility::Show)
		, bAllowMultipleTopLevelObjects(false)
	{}

	/** Notify hook to call when properties are changed */
	FNotifyHook* NotifyHook;

	/** Controls how CPF_DisableEditOnInstance nodes will be treated */
	EEditDefaultsOnlyNodeVisibility DefaultsOnlyVisibility;

	bool bAllowMultipleTopLevelObjects;
};

class IPropertyRowGenerator
{
public:
	virtual ~IPropertyRowGenerator() {}

	/**
	 * Sets the objects that should be used to generate rows
	 *
	 * @param InObjects	The list of objects to generate rows from.  Note unless FPropertyRowGeneratorArgs.bAllowMultipleTopLevelObjects is set to true, the properties used will be the common base class of all passed in objects
	 */
	virtual void SetObjects(const TArray<UObject*>& InObjects) = 0;

	/**
	 * Delegate called when rows have been refreshed.  This delegate should always be bound to something because once this is called, none of the rows previously generated can be trusted
	 */
	DECLARE_EVENT(IPropertyRowGenerator, FOnRowsRefreshed);
	virtual FOnRowsRefreshed& OnRowsRefreshed() = 0;

	/** 
	 * @return The list of root tree nodes that have been generated.  Note: There will only be one root node unless  FPropertyRowGeneratorArgs.bAllowMultipleTopLevelObjects was set to true when the generator was created
	 */
	virtual const TArray<TSharedRef<IDetailTreeNode>>& GetRootTreeNodes() const = 0;

	/**
	 * Finds a tree node by property handle.  
	 * 
	 * @return The found tree node or null if the node cannot be found
	 */
	virtual TSharedPtr<IDetailTreeNode> FindTreeNode(TSharedPtr<IPropertyHandle> PropertyHandle) const = 0;

	/**
	 * Registers a custom detail layout delegate for a specific class in this instance of the generator only
	 *
	 * @param Class	The class the custom detail layout is for
	 * @param DetailLayoutDelegate	The delegate to call when querying for custom detail layouts for the classes properties
	 */
	virtual void RegisterInstancedCustomPropertyLayout(UStruct* Class, FOnGetDetailCustomizationInstance DetailLayoutDelegate) = 0;
	virtual void RegisterInstancedCustomPropertyTypeLayout(FName PropertyTypeName, FOnGetPropertyTypeCustomizationInstance PropertyTypeLayoutDelegate, TSharedPtr<IPropertyTypeIdentifier> Identifier = nullptr) = 0;

	/**
	 * Unregisters a custom detail layout delegate for a specific class in this instance of the generator only
	 *
	 * @param Class	The class with the custom detail layout delegate to remove
	 */
	virtual void UnregisterInstancedCustomPropertyLayout(UStruct* Class) = 0;
	virtual void UnregisterInstancedCustomPropertyTypeLayout(FName PropertyTypeName, TSharedPtr<IPropertyTypeIdentifier> Identifier = nullptr) = 0;

	virtual TSharedPtr<class FAssetThumbnailPool> GetGeneratedThumbnailPool() = 0;
};
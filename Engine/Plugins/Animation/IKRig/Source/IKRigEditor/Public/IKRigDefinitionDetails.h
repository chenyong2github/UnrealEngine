// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtrTemplates.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class IDetailLayoutBuilder;
class UIKRigDefinition;
class UIKRigController;
struct FAssetData;
class FReply;
class ITableRow;
class STableViewBase;
class IPropertyHandle;

class FIKRigDefinitionDetails : public IDetailCustomization
{

public:

	virtual ~FIKRigDefinitionDetails();

	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
	/** IDetailCustomization interface */

private:

	/** pointers to "Model" and "Controller" in MVC pattern */
	TWeakObjectPtr<UIKRigDefinition> IKRigDefinition;
	UIKRigController* IKRigController;

	/** source skeleton asset, used to initialize hierarchy */
	FString GetCurrentSourceAsset() const;
	bool ShouldFilterAsset(const FAssetData& AssetData);
	void OnAssetSelected(const FAssetData& AssetData);
	TWeakObjectPtr<UObject> SelectedAsset;

	/** import new skeleton */
	FReply OnImportHierarchy();
	bool CanImport() const;

	TWeakPtr< IDetailLayoutBuilder > DetailBuilderWeakPtr;
};


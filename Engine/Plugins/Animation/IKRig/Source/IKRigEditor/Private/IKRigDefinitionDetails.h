// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "IDetailCustomization.h"

class IDetailLayoutBuilder;
class UIKRigDefinition;
class UIKRigController;

// Utility class to build combo boxes out of arrays of names.
struct FGoalNameListItem : public TSharedFromThis<FGoalNameListItem>
{
	FName GoalName;
	FName DisplayName;

	FGoalNameListItem(const FName& InName)
	{
		GoalName = InName;
		DisplayName = InName;
	}
};

class FIKRigDefinitionDetails : public IDetailCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual ~FIKRigDefinitionDetails();

	/** IDetailCustomization interface */
	virtual void CustomizeDetails( IDetailLayoutBuilder& DetailBuilder ) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;
private:
	TWeakObjectPtr<UIKRigDefinition>		IKRigDefinition;
	UIKRigController*						IKRigController;

	// source asset
	FString GetCurrentSourceAsset() const;
	bool ShouldFilterAsset(const FAssetData& AssetData);
	void OnAssetSelected(const FAssetData& AssetData);
	TWeakObjectPtr<UObject>					SelectedAsset;

	FReply OnImportHierarchy();
	bool CanImport() const;

	TWeakPtr< IDetailLayoutBuilder > DetailBuilderWeakPtr;

	FReply OnShowClassPicker();

	typedef TSharedPtr<FGoalNameListItem> FGoalNameListItemPtr;

	void OnObjectPostEditChange(UObject* Object, FPropertyChangedEvent& InPropertyChangedEvent);
	TSharedRef<ITableRow> OnGenerateWidgetForGoals(FGoalNameListItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleGoalNameChanged(const FText& NewName, ETextCommit::Type CommitType, FGoalNameListItemPtr InItem);
	FText GetGoalNameText(FGoalNameListItemPtr InName) const;

	TSharedPtr<IPropertyHandle> GoalPropertyHandle;
	TSharedPtr<SListView<FGoalNameListItemPtr>> GoalListView;
	TArray<FGoalNameListItemPtr> GoalListNames;

	FDelegateHandle ObjectChangedDelegate;
};


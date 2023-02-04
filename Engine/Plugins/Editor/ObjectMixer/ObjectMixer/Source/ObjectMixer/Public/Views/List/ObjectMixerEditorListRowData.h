// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"

#include "ISceneOutlinerTreeItem.h"
#include "PropertyHandle.h"
#include "SSceneOutliner.h"

class SObjectMixerEditorList;

/** Defines data carried by each row type. */
struct OBJECTMIXEREDITOR_API FObjectMixerEditorListRowData
{
	struct FTransientEditorVisibilityRules
	{
		/**
		 * If true, the user wants the row to be hidden temporarily in the editor.
		 * This is transient visibility like the eye icon in the Scene Outliner, not the bVisible or bHiddenInGame properties.
		 */
		bool bShouldBeHiddenInEditor = false;

		/**
		 * If true, the user wants the row to have solo visibility. Multiple rows at once can be set to solo.
		 * Solo rows' objects are exclusively visible,
		 * so all other objects found in the panel will be invisible while at least one row is in a solo state.
		 */
		bool bShouldBeSolo = false;
	};

	struct FPropertyPropagationInfo
	{
		FPropertyPropagationInfo() = default;
		
		FSceneOutlinerTreeItemID RowIdentifier;
		FName PropertyName = NAME_None;
		EPropertyValueSetFlags::Type PropertyValueSetFlags = 0;
	};
	
	FObjectMixerEditorListRowData(
		SSceneOutliner* InSceneOutliner, const FText& InDisplayNameOverride = FText::GetEmpty())
	: SceneOutlinerPtr(InSceneOutliner)
	, DisplayNameOverride(InDisplayNameOverride)
	{}
	
	FObjectMixerEditorListRowData() = default;

	~FObjectMixerEditorListRowData() = default;

	const TArray<TObjectPtr<UObjectMixerObjectFilter>>& GetObjectFilterInstances() const;

	const UObjectMixerObjectFilter* GetMainObjectFilterInstance() const;

	UE_NODISCARD bool GetIsTreeViewItemExpanded(const TSharedRef<ISceneOutlinerTreeItem> InRow);
	void SetIsTreeViewItemExpanded(const TSharedRef<ISceneOutlinerTreeItem> InRow, const bool bNewExpanded);

	UE_NODISCARD bool GetIsSelected(const TSharedRef<ISceneOutlinerTreeItem> InRow);
	void SetIsSelected(const TSharedRef<ISceneOutlinerTreeItem> InRow, const bool bNewSelected);

	UE_NODISCARD bool HasAtLeastOneChildThatIsNotSolo(
		const TSharedRef<ISceneOutlinerTreeItem> InRow, const bool bRecursive = true) const;

	UE_NODISCARD FText GetDisplayName(TSharedPtr<ISceneOutlinerTreeItem> InTreeItem) const;

	UE_NODISCARD const FText& GetDisplayNameOverride() const
	{
		return DisplayNameOverride;
	}

	void SetDisplayNameOverride(const FText& InDisplayNameOverride)
	{
		DisplayNameOverride = InDisplayNameOverride;
	}

	UE_NODISCARD SObjectMixerEditorList* GetListView() const;

	UE_NODISCARD TArray<TSharedPtr<ISceneOutlinerTreeItem>> GetSelectedTreeViewItems() const;
	
	void OnChangeVisibility(const FSceneOutlinerTreeItemRef TreeItem, const bool bNewVisible);
	
	UE_NODISCARD const FTransientEditorVisibilityRules& GetVisibilityRules() const;
	void SetVisibilityRules(const FTransientEditorVisibilityRules& InVisibilityRules);

	bool IsUserSetHiddenInEditor() const;
	void SetUserHiddenInEditor(const bool bNewHidden);
	
	bool GetRowSoloState() const;
	void SetRowSoloState(const bool bNewSolo);

	void ClearSoloRows() const;

	bool GetIsHybridRow() const
	{
		return HybridComponent.IsValid();
	}

	UActorComponent* GetHybridComponent() const
	{
		return HybridComponent.Get();
	}
	
	/** If this row represents an actor or other container and should show the data for a single child component, define it here. */
	void SetHybridComponent(UActorComponent* InHybridComponent)
	{
		HybridComponent = InHybridComponent;
	}

	void PropagateChangesToSimilarSelectedRowProperties(
		const TSharedRef<ISceneOutlinerTreeItem> InRow, const FPropertyPropagationInfo PropertyPropagationInfo);
	
	TMap<FName, TWeakPtr<IPropertyHandle>> PropertyNamesToHandles;

	SSceneOutliner* SceneOutlinerPtr;

protected:
	FTransientEditorVisibilityRules VisibilityRules;

	FText DisplayNameOverride;

	TWeakObjectPtr<UActorComponent> HybridComponent = nullptr;
};

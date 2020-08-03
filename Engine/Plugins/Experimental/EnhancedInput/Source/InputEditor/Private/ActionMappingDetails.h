// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "PropertyHandle.h"
#include "IDetailCustomNodeBuilder.h"
#include "IDetailCustomization.h"

// TODO: This is derived from (and will eventually replace) InputSettingsDetails.h

class FDetailWidgetRow;
class IDetailChildrenBuilder;
class IDetailGroup;
class IDetailLayoutBuilder;

namespace InputConstants
{
	const FMargin PropertyPadding(2.0f, 0.0f, 2.0f, 0.0f);
	const float TextBoxWidth = 250.0f;
	const float ScaleBoxWidth = 50.0f;
}

struct FMappingSet
{
	const class UInputAction* SharedAction;
	IDetailGroup* DetailGroup;
	TArray<TSharedRef<IPropertyHandle>> Mappings;
};

class FActionMappingsNodeBuilder : public IDetailCustomNodeBuilder, public TSharedFromThis<FActionMappingsNodeBuilder>
{
public:
	FActionMappingsNodeBuilder(IDetailLayoutBuilder* InDetailLayoutBuilder, const TSharedPtr<IPropertyHandle>& InPropertyHandle);

	/** IDetailCustomNodeBuilder interface */
	virtual void SetOnRebuildChildren(FSimpleDelegate InOnRebuildChildren) override { OnRebuildChildren = InOnRebuildChildren; }
	virtual bool RequiresTick() const override { return true; }
	virtual void Tick(float DeltaTime) override;
	virtual void GenerateHeaderRowContent(FDetailWidgetRow& NodeRow) override;
	virtual void GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder) override;
	virtual bool InitiallyCollapsed() const override { return true; };
	virtual FName GetName() const override { return FName(TEXT("ActionMappings")); }

private:
	void AddActionMappingButton_OnClick();
	void ClearActionMappingButton_OnClick();
	void OnActionMappingActionChanged(const FAssetData& AssetData, const FMappingSet MappingSet);
	void AddActionMappingToGroupButton_OnClick(const FMappingSet MappingSet);
	void RemoveActionMappingGroupButton_OnClick(const FMappingSet MappingSet);

	bool GroupsRequireRebuild() const;
	void RebuildGroupedMappings();
	void RebuildChildren()
	{
		OnRebuildChildren.ExecuteIfBound();
	}
	/** Makes sure that groups have their expansion set after any rebuilding */
	void HandleDelayedGroupExpansion();

private:
	/** Called to rebuild the children of the detail tree */
	FSimpleDelegate OnRebuildChildren;

	/** Associated detail layout builder */
	IDetailLayoutBuilder* DetailLayoutBuilder;

	/** Property handle to associated Action Mappings */
	TSharedPtr<IPropertyHandle> ActionMappingsPropertyHandle;

	TArray<FMappingSet> GroupedMappings;

	TArray<TPair<const UInputAction*, bool>> DelayedGroupExpansionStates;
};
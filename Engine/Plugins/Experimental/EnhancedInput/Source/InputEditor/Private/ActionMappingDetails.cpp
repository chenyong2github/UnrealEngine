// Copyright Epic Games, Inc. All Rights Reserved.

#include "ActionMappingDetails.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IDocumentation.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ActionMappingDetails"

// TODO: This is derived from (and will eventually replace) InputSettingsDetails.cpp

FActionMappingsNodeBuilderEx::FActionMappingsNodeBuilderEx(IDetailLayoutBuilder* InDetailLayoutBuilder, const TSharedPtr<IPropertyHandle>& InPropertyHandle)
	: DetailLayoutBuilder(InDetailLayoutBuilder)
	, ActionMappingsPropertyHandle(InPropertyHandle)
{
}

void FActionMappingsNodeBuilderEx::Tick(float DeltaTime)
{
	if (GroupsRequireRebuild())
	{
		RebuildChildren();
	}
	HandleDelayedGroupExpansion();
}

void FActionMappingsNodeBuilderEx::GenerateHeaderRowContent(FDetailWidgetRow& NodeRow)
{
	TSharedRef<SWidget> AddButton = PropertyCustomizationHelpers::MakeAddButton(
		FSimpleDelegate::CreateSP(this, &FActionMappingsNodeBuilderEx::AddActionMappingButton_OnClick),
		TAttribute<FText>(this, &FActionMappingsNodeBuilderEx::GetAddNewActionTooltip),
		TAttribute<bool>(this, &FActionMappingsNodeBuilderEx::CanAddNewActionMapping));

	TSharedRef<SWidget> ClearButton = PropertyCustomizationHelpers::MakeEmptyButton(FSimpleDelegate::CreateSP(this, &FActionMappingsNodeBuilderEx::ClearActionMappingButton_OnClick),
		LOCTEXT("ClearActionMappingToolTip", "Removes all Action Mappings"));

	FSimpleDelegate RebuildChildrenDelegate = FSimpleDelegate::CreateSP(this, &FActionMappingsNodeBuilderEx::RebuildChildren);
	ActionMappingsPropertyHandle->SetOnPropertyValueChanged(RebuildChildrenDelegate);
	ActionMappingsPropertyHandle->AsArray()->SetOnNumElementsChanged(RebuildChildrenDelegate);

	NodeRow
	.FilterString(ActionMappingsPropertyHandle->GetPropertyDisplayName())
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			ActionMappingsPropertyHandle->CreatePropertyNameWidget()
		]
		+ SHorizontalBox::Slot()
		.Padding(2.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			AddButton
		]
		+ SHorizontalBox::Slot()
		.Padding(2.0f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.AutoWidth()
		[
			ClearButton
		]
	];
}

bool FActionMappingsNodeBuilderEx::CanAddNewActionMapping() const
{
	// If the last action mapping the user has added is still null, then do not allow adding another one
	TSharedPtr<IPropertyHandleArray> ActionMappingsArrayHandle = ActionMappingsPropertyHandle->AsArray();

	uint32 NumMappings;
	ActionMappingsArrayHandle->GetNumElements(NumMappings);

	if(NumMappings > 0)
	{
		TSharedRef<IPropertyHandle> ActionMapping = ActionMappingsArrayHandle->GetElement(NumMappings - 1);
		const UObject* Action;
		FPropertyAccess::Result Result = ActionMapping->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEnhancedActionKeyMapping, Action))->GetValue(Action);
		return Result == FPropertyAccess::Success && Action;
	}

	// If there are no mappings, then the user is allowed to add one
	return true;
}

FText FActionMappingsNodeBuilderEx::GetAddNewActionTooltip() const
{
	if(CanAddNewActionMapping())
	{
		return LOCTEXT("AddActionMappingToolTip_Enabled", "Adds Action Mapping");
	}
	else
	{
		return LOCTEXT("AddActionMappingToolTip_Disabled", "Cannot add an action mapping while an empty mapping exists");
	}
}

void FActionMappingsNodeBuilderEx::GenerateChildContent(IDetailChildrenBuilder& ChildrenBuilder)
{
	RebuildGroupedMappings();

	for (int32 Index = 0; Index < GroupedMappings.Num(); ++Index)
	{
		FMappingSet& MappingSet = GroupedMappings[Index];

		FString GroupNameString(TEXT("ActionMappings."));
		GroupNameString += MappingSet.SharedAction->GetPathName();
		FName GroupName(*GroupNameString);
		IDetailGroup& ActionMappingGroup = ChildrenBuilder.AddGroup(GroupName, FText::FromString(MappingSet.SharedAction->GetPathName()));
		MappingSet.DetailGroup = &ActionMappingGroup;

		TSharedRef<SWidget> AddButton = PropertyCustomizationHelpers::MakeAddButton(FSimpleDelegate::CreateSP(this, &FActionMappingsNodeBuilderEx::AddActionMappingToGroupButton_OnClick, MappingSet),
			LOCTEXT("AddActionMappingToGroupToolTip", "Add a control binding to the Action Mapping"));

		TSharedRef<SWidget> RemoveButton = PropertyCustomizationHelpers::MakeDeleteButton(FSimpleDelegate::CreateSP(this, &FActionMappingsNodeBuilderEx::RemoveActionMappingGroupButton_OnClick, MappingSet),
			LOCTEXT("RemoveActionMappingGroupToolTip", "Remove the Action Mapping Group"));

		ActionMappingGroup.HeaderRow()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(InputConstants::TextBoxWidth)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UInputAction::StaticClass())
					.ObjectPath(MappingSet.SharedAction ? MappingSet.SharedAction->GetPathName() : FString())
					.DisplayUseSelected(true)
					.OnObjectChanged(this, &FActionMappingsNodeBuilderEx::OnActionMappingActionChanged, MappingSet)
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(InputConstants::PropertyPadding)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				AddButton
			]
			+ SHorizontalBox::Slot()
			.Padding(InputConstants::PropertyPadding)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				RemoveButton
			]
		];

		for (int32 MappingIndex = 0; MappingIndex < MappingSet.Mappings.Num(); ++MappingIndex)
		{
			ActionMappingGroup.AddPropertyRow(MappingSet.Mappings[MappingIndex]).ShowPropertyButtons(false);
		}
	}
}

void FActionMappingsNodeBuilderEx::AddActionMappingButton_OnClick()
{
	static const FName BaseActionMappingName(*LOCTEXT("NewActionMappingName", "NewActionMapping").ToString());
	static int32 NewMappingCount = 0;
	const FScopedTransaction Transaction(LOCTEXT("AddActionMapping_Transaction", "Add Action Mapping"));

	TArray<UObject*> OuterObjects;
	ActionMappingsPropertyHandle->GetOuterObjects(OuterObjects);

	if (OuterObjects.Num() == 1)
	{
		UInputMappingContext* InputContext = CastChecked<UInputMappingContext>(OuterObjects[0]);
		InputContext->Modify();
		ActionMappingsPropertyHandle->NotifyPreChange();

		DelayedGroupExpansionStates.Emplace(nullptr, true);
		InputContext->MapKey(nullptr, FKey());

		ActionMappingsPropertyHandle->NotifyPostChange(EPropertyChangeType::ArrayAdd);
	}
}

void FActionMappingsNodeBuilderEx::ClearActionMappingButton_OnClick()
{
	ActionMappingsPropertyHandle->AsArray()->EmptyArray();
}

void FActionMappingsNodeBuilderEx::OnActionMappingActionChanged(const FAssetData& AssetData, const FMappingSet MappingSet)
{
	const FScopedTransaction Transaction(LOCTEXT("SwitchActionMapping_Transaction", "Switch Action Mapping"));

	const UInputAction* SelectedAction = Cast<const UInputAction>(AssetData.GetAsset());

	const UObject* CurrentAction = nullptr;
	if (MappingSet.Mappings.Num() > 0)
	{
		MappingSet.Mappings[0]->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEnhancedActionKeyMapping, Action))->GetValue(CurrentAction);
	}

	if (SelectedAction != CurrentAction)
	{
		for (int32 Index = 0; Index < MappingSet.Mappings.Num(); ++Index)
		{
			MappingSet.Mappings[Index]->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEnhancedActionKeyMapping, Action))->SetValue(SelectedAction);
		}

		if (MappingSet.DetailGroup)
		{
			DelayedGroupExpansionStates.Emplace(SelectedAction, MappingSet.DetailGroup->GetExpansionState());

			// Don't want to save expansion state of old asset
			MappingSet.DetailGroup->ToggleExpansion(false);
		}
	}
}

void FActionMappingsNodeBuilderEx::AddActionMappingToGroupButton_OnClick(const FMappingSet MappingSet)
{
	const FScopedTransaction Transaction(LOCTEXT("AddActionMappingToGroup_Transaction", "Add a control binding to the Action Mapping"));

	TArray<UObject*> OuterObjects;
	ActionMappingsPropertyHandle->GetOuterObjects(OuterObjects);

	if (OuterObjects.Num() == 1)
	{
		UInputMappingContext* InputContext = CastChecked<UInputMappingContext>(OuterObjects[0]);
		InputContext->Modify();
		ActionMappingsPropertyHandle->NotifyPreChange();

		DelayedGroupExpansionStates.Emplace(MappingSet.SharedAction, true);
		InputContext->MapKey(MappingSet.SharedAction, FKey());

		ActionMappingsPropertyHandle->NotifyPostChange(EPropertyChangeType::ArrayAdd);
	}
}

void FActionMappingsNodeBuilderEx::RemoveActionMappingGroupButton_OnClick(const FMappingSet MappingSet)
{
	const FScopedTransaction Transaction(LOCTEXT("RemoveActionMappingGroup_Transaction", "Remove Action Mapping and all control bindings"));

	TSharedPtr<IPropertyHandleArray> ActionMappingsArrayHandle = ActionMappingsPropertyHandle->AsArray();

	TArray<int32> SortedIndices;
	for (int32 Index = 0; Index < MappingSet.Mappings.Num(); ++Index)
	{
		SortedIndices.AddUnique(MappingSet.Mappings[Index]->GetIndexInArray());
	}
	SortedIndices.Sort();

	for (int32 Index = SortedIndices.Num() - 1; Index >= 0; --Index)
	{
		ActionMappingsArrayHandle->DeleteItem(SortedIndices[Index]);
	}
}

bool FActionMappingsNodeBuilderEx::GroupsRequireRebuild() const
{
	for (int32 GroupIndex = 0; GroupIndex < GroupedMappings.Num(); ++GroupIndex)
	{
		const FMappingSet& MappingSet = GroupedMappings[GroupIndex];
		for (int32 MappingIndex = 0; MappingIndex < MappingSet.Mappings.Num(); ++MappingIndex)
		{
			const UObject* Action;
			MappingSet.Mappings[MappingIndex]->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEnhancedActionKeyMapping, Action))->GetValue(Action);
			if (MappingSet.SharedAction != Action)
			{
				return true;
			}
		}
	}
	return false;
}

void FActionMappingsNodeBuilderEx::RebuildGroupedMappings()
{
	GroupedMappings.Empty();

	TSharedPtr<IPropertyHandleArray> ActionMappingsArrayHandle = ActionMappingsPropertyHandle->AsArray();

	uint32 NumMappings;
	ActionMappingsArrayHandle->GetNumElements(NumMappings);
	for (uint32 Index = 0; Index < NumMappings; ++Index)
	{
		TSharedRef<IPropertyHandle> ActionMapping = ActionMappingsArrayHandle->GetElement(Index);
		const UObject* Action;
		FPropertyAccess::Result Result = ActionMapping->GetChildHandle(GET_MEMBER_NAME_CHECKED(FEnhancedActionKeyMapping, Action))->GetValue(Action);

		if (Result == FPropertyAccess::Success)
		{
			int32 FoundIndex = INDEX_NONE;
			for (int32 GroupIndex = 0; GroupIndex < GroupedMappings.Num(); ++GroupIndex)
			{
				if (GroupedMappings[GroupIndex].SharedAction == Action)
				{
					FoundIndex = GroupIndex;
					break;
				}
			}
			if (FoundIndex == INDEX_NONE)
			{
				FoundIndex = GroupedMappings.Num();
				GroupedMappings.AddZeroed();
				GroupedMappings[FoundIndex].SharedAction = Cast<const UInputAction>(Action);
			}
			GroupedMappings[FoundIndex].Mappings.Add(ActionMapping);
		}
	}
}

void FActionMappingsNodeBuilderEx::HandleDelayedGroupExpansion()
{
	if (DelayedGroupExpansionStates.Num() > 0)
	{
		for (auto GroupState : DelayedGroupExpansionStates)
		{
			for (auto& MappingSet : GroupedMappings)
			{
				if (MappingSet.SharedAction == GroupState.Key)
				{
					MappingSet.DetailGroup->ToggleExpansion(GroupState.Value);
					break;
				}
			}
		}
		DelayedGroupExpansionStates.Empty();
	}
}

#undef LOCTEXT_NAMESPACE

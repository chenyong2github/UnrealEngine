// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IDetailLayoutBuilder;
class IPropertyHandle;

#define BEGIN_CATEGORY(CategoryName) { \
	IDetailCategoryBuilder& CurrentCategory = InLayoutBuilder.EditCategory(CategoryName);

#define BEGIN_LABELED_CATEGORY(CategoryName, CategoryLabel) { \
	IDetailCategoryBuilder& CurrentCategory = InLayoutBuilder.EditCategory(CategoryName, CategoryLabel);

#define END_CATEGORY() }

#define ADD_PROPERTY(ClassName, PropertyName) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle); \
}

#define ADD_EXPANDED_PROPERTY(ClassName, PropertyName) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle).ShouldAutoExpand(true); \
}

#define ADD_ADVANCED_PROPERTY(ClassName, PropertyName) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle, EPropertyLocation::Advanced); \
}

#define ADD_CUSTOM_PROPERTY(FilterText) CurrentCategory.AddCustomRow(FilterText)

#define ADD_NESTED_PROPERTY(NestedPropertyHelper, ClassName, PropertyPath) { \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()); \
}

#define HIDE_NESTED_PROPERTY(NestedPropertyHelper, ClassName, PropertyPath) { \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	PropertyHandle->MarkHiddenByCustomization(); \
}

#define ADD_EXPANDED_NESTED_PROPERTY(NestedPropertyHelper, ClassName, PropertyPath) { \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()).ShouldAutoExpand(true); \
}

#define ADD_ADVANCED_NESTED_PROPERTY(NestedPropertyHelper, ClassName, PropertyPath) { \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle.ToSharedRef(), EPropertyLocation::Advanced); \
}

#define RENAME_NESTED_PROPERTY(NestedPropertyHelper, ClassName, PropertyPath, NewPropertyName) { \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()).DisplayName(NewPropertyName); \
}

#define RENAME_NESTED_PROPERTY_AND_TOOLTIP(NestedPropertyHelper, ClassName, PropertyPath, NewPropertyName, NewPropertyTooltip) { \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()).DisplayName(NewPropertyName).ToolTip(NewPropertyTooltip); \
}

#define RENAME_NESTED_CONDITIONAL_PROPERTY(NestedPropertyHelper, ClassName, PropertyPath, NewPropertyName, ConditionalPropertyPath) { \
	CREATE_NESTED_PROPERTY_EDITCONDITION_1ARG(Conditional, NestedPropertyHelper, ClassName, ConditionalPropertyPath); \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()).DisplayName(NewPropertyName).EditCondition(Conditional, nullptr); \
}

#define RENAME_NESTED_CONDITIONAL_PROPERTY_AND_TOOLTIP(NestedPropertyHelper, ClassName, PropertyPath, NewPropertyName, NewTooltip, ConditionalPropertyPath) { \
	CREATE_NESTED_PROPERTY_EDITCONDITION_1ARG(Conditional, NestedPropertyHelper, ClassName, ConditionalPropertyPath); \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()).DisplayName(NewPropertyName).ToolTip(NewTooltip).EditCondition(Conditional, nullptr); \
}

#define REPLACE_PROPERTY_WITH_CUSTOM(ClassName, PropertyName, Widget) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	InLayoutBuilder.HideProperty(PropertyHandle); \
	CurrentCategory.AddCustomRow(PropertyHandle->GetPropertyDisplayName()).NameContent()[PropertyHandle->CreatePropertyNameWidget()].ValueContent()[Widget]; \
}

#define BEGIN_GROUP(GroupName, GroupLabel) { \
	IDetailGroup& CurrentGroup = CurrentCategory.AddGroup(GroupName, GroupLabel, false, true);

#define BEGIN_GROUP_WITH_TOOLTIP(GroupName, GroupLabel, GroupTooltip) { \
	IDetailGroup& CurrentGroup = CurrentCategory.AddGroup(GroupName, GroupLabel, false, true); \
	CurrentGroup.HeaderRow() \
	[ \
		SNew(STextBlock) \
		.Font(IDetailLayoutBuilder::GetDetailFont()) \
		.Text(GroupLabel) \
		.ToolTipText(GroupTooltip) \
	]; \

#define END_GROUP() }

#define ADD_GROUP_PROPERTY(ClassName, PropertyName) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentGroup.AddPropertyRow(PropertyHandle); \
}

#define ADD_GROUP_EXPANDED_PROPERTY(ClassName, PropertyName) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentGroup.AddPropertyRow(PropertyHandle).ShouldAutoExpand(true); \
}

#define ADD_GROUP_NESTED_PROPERTY(NestedPropertyHelper, ClassName, PropertyPath) { \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	CurrentGroup.AddPropertyRow(PropertyHandle.ToSharedRef()).ShouldAutoExpand(true); \
}

#define ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP(NestedPropertyHelper, ClassName, PropertyPath, Tooltip) { \
	TSharedPtr<IPropertyHandle> PropertyHandle = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(PropertyHandle.IsValid()); \
	check(PropertyHandle->IsValidHandle()); \
	PropertyHandle->SetToolTipText(Tooltip); \
	CurrentGroup.AddPropertyRow(PropertyHandle.ToSharedRef()).ShouldAutoExpand(true); \
}

// Edit condition helpers
#define GET_PROPERTY_HANDLE(HandleName, ClassName, PropertyPath) \
	const TSharedPtr<IPropertyHandle> HandleName = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(HandleName.IsValid()); \
	check(HandleName->IsValidHandle()); \

#define GET_NESTED_PROPERTY_HANDLE(HandleName, NestedPropertyHelper, ClassName, PropertyPath) \
	const TSharedPtr<IPropertyHandle> HandleName = NestedPropertyHelper.GetNestedProperty(GET_MEMBER_NAME_STRING_CHECKED(ClassName, PropertyPath)); \
	check(HandleName.IsValid()); \
	check(HandleName->IsValidHandle()); \

#define CREATE_NESTED_PROPERTY_EDITCONDITION_1ARG(EditConditionName, NestedPropertyHelper, ClassName, PropertyPath) \
	GET_NESTED_PROPERTY_HANDLE(EditConditionName##PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	const TAttribute<bool> EditConditionName = TAttribute<bool>::Create([this, EditConditionName##PropertyHandle]() \
	{ \
		bool bCond1 = false; \
		EditConditionName##PropertyHandle->GetValue(bCond1); \
		return bCond1; \
	});

#define CREATE_NESTED_PROPERTY_EDITCONDITION_2ARG(EditConditionName, NestedPropertyHelper, ClassName, PropertyPath, PropertyPath2) \
	GET_NESTED_PROPERTY_HANDLE(EditConditionName##PropertyHandle1, NestedPropertyHelper, ClassName, PropertyPath); \
	GET_NESTED_PROPERTY_HANDLE(EditConditionName##PropertyHandle2, NestedPropertyHelper, ClassName, PropertyPath2); \
	const TAttribute<bool> EditConditionName = TAttribute<bool>::Create([this, EditConditionName##PropertyHandle1, EditConditionName##PropertyHandle2]() \
	{ \
		bool bCond1 = false; \
		bool bCond2 = false; \
		EditConditionName##PropertyHandle1->GetValue(bCond1); \
		EditConditionName##PropertyHandle2->GetValue(bCond2); \
		return bCond1 && bCond2; \
	});

#define CREATE_NESTED_PROPERTY_EDITCONDITION_3ARG(EditConditionName, NestedPropertyHelper, ClassName, PropertyPath, PropertyPath2, PropertyPath3) \
	GET_NESTED_PROPERTY_HANDLE(EditConditionName##PropertyHandle1, NestedPropertyHelper, ClassName, PropertyPath); \
	GET_NESTED_PROPERTY_HANDLE(EditConditionName##PropertyHandle2, NestedPropertyHelper, ClassName, PropertyPath2); \
	GET_NESTED_PROPERTY_HANDLE(EditConditionName##PropertyHandle3, NestedPropertyHelper, ClassName, PropertyPath3); \
	const TAttribute<bool> EditConditionName = TAttribute<bool>::Create([this, EditConditionName##PropertyHandle1, EditConditionName##PropertyHandle2, EditConditionName##PropertyHandle3]() \
	{ \
		bool bCond1 = false; \
		bool bCond2 = false; \
		bool bCond3 = false; \
		EditConditionName##PropertyHandle1->GetValue(bCond1); \
		EditConditionName##PropertyHandle2->GetValue(bCond2); \
		EditConditionName##PropertyHandle3->GetValue(bCond3); \
		return bCond1 && bCond2 && bCond3; \
	});

#define ADD_GROUP_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	IDetailPropertyRow& PropertyRow = CurrentGroup.AddPropertyRow(PropertyHandle.ToSharedRef()); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr);\
}

#define ADD_GROUP_NESTED_PROPERTY_WITH_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, Tooltip, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	PropertyHandle->SetToolTipText(Tooltip); \
	IDetailPropertyRow& PropertyRow = CurrentGroup.AddPropertyRow(PropertyHandle.ToSharedRef()); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr);\
}

#define RENAME_GROUP_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, NewPropertyName, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	IDetailPropertyRow& PropertyRow = CurrentGroup.AddPropertyRow(PropertyHandle.ToSharedRef()); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr); \
	PropertyRow.DisplayName(NewPropertyName); \
}

#define ADD_EXPANDED_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr);\
	PropertyRow.ShouldAutoExpand(true); \
}

#define ADD_ADVANCED_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(PropertyHandle.ToSharedRef(), EPropertyLocation::Advanced); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr); \
}

#define ADD_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr);\
}

#define ADD_NESTED_PROPERTY_WITH_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, Tooltip, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	PropertyHandle->SetToolTipText(Tooltip); \
	IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr);\
}

#define RENAME_NESTED_PROPERTY_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, NewPropertyName, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr); \
	PropertyRow.DisplayName(NewPropertyName); \
}

#define RENAME_NESTED_PROPERTY_AND_TOOLTIP_EDIT_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, NewPropertyName, NewToolTipText, PropertyRowEditCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()); \
	PropertyRow.EditCondition(PropertyRowEditCondition, nullptr); \
	PropertyRow.DisplayName(NewPropertyName); \
	PropertyRow.ToolTip(NewToolTipText); \
}

// Support IsEnabled
#define ADD_NESTED_PROPERTY_ENABLE_CONDITION(NestedPropertyHelper, ClassName, PropertyPath, PropertyRowEnableCondition) { \
	GET_NESTED_PROPERTY_HANDLE(PropertyHandle, NestedPropertyHelper, ClassName, PropertyPath); \
	IDetailPropertyRow& PropertyRow = CurrentCategory.AddProperty(PropertyHandle.ToSharedRef()); \
	PropertyRow.IsEnabled(PropertyRowEnableCondition);\
}

#define REPLACE_PROPERTY_WITH_CUSTOM_ENABLE_CONDITION(ClassName, PropertyName, Widget, PropertyRowEnableCondition) { \
	GET_PROPERTY_HANDLE(PropertyHandle, ClassName, PropertyName); \
	InLayoutBuilder.HideProperty(PropertyHandle); \
	FDetailWidgetRow& DetailWidgetRow = CurrentCategory.AddCustomRow(PropertyHandle->GetPropertyDisplayName()); \
	DetailWidgetRow.IsEnabled(PropertyRowEnableCondition); \
	DetailWidgetRow.NameContent()[PropertyHandle->CreatePropertyNameWidget()].ValueContent()[Widget]; \
}

struct FDisplayClusterConfiguratorNestedPropertyHelper
{
public:
	FDisplayClusterConfiguratorNestedPropertyHelper(IDetailLayoutBuilder& InLayoutBuilder) :
		LayoutBuilder(InLayoutBuilder)
	{ }

	TSharedPtr<IPropertyHandle> GetNestedProperty(const FString& PropertyPath);
	void GetNestedProperties(const FString& PropertyPath, TArray<TSharedPtr<IPropertyHandle>>& OutPropertyHandles);
	void GetNestedPropertyKeys(const FString& PropertyPath, TArray<FString>& OutPropertyKeys);

private:
	TSharedPtr<IPropertyHandle> GetProperty(const FString& PropertyName, const TSharedPtr<IPropertyHandle>& Parent, const FString& ParentPath);
	TSharedPtr<IPropertyHandle> FindCachedHandle(const TArray<FString>& PropertyPath, FString& OutFoundPath, int32& OutFoundIndex);
	bool IsListType(const TSharedPtr<IPropertyHandle>& PropertyHandle);

private:
	TMap<FString, TSharedPtr<IPropertyHandle>> CachedPropertyHandles;
	IDetailLayoutBuilder& LayoutBuilder;
};
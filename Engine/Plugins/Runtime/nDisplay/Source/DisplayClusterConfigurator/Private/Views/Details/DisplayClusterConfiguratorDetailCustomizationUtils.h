// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class IDetailLayoutBuilder;
class IPropertyHandle;

#define BEGIN_CATEGORY(CategoryName) { \
	IDetailCategoryBuilder& CurrentCategory = InLayoutBuilder.EditCategory(CategoryName);

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

#define REPLACE_PROPERTY_WITH_CUSTOM(ClassName, PropertyName, Widget) { \
	TSharedRef<IPropertyHandle> PropertyHandle = InLayoutBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ClassName, PropertyName)); \
	check(PropertyHandle->IsValidHandle()); \
	InLayoutBuilder.HideProperty(PropertyHandle); \
	CurrentCategory.AddCustomRow(PropertyHandle->GetPropertyDisplayName()).NameContent()[PropertyHandle->CreatePropertyNameWidget()].ValueContent()[Widget]; \
}

#define BEGIN_GROUP(GroupName, GroupLabel) { \
	IDetailGroup& CurrentGroup = CurrentCategory.AddGroup(GroupName, GroupLabel, false, true);

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
	CurrentGroup.AddPropertyRow(PropertyHandle.ToSharedRef()); \
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
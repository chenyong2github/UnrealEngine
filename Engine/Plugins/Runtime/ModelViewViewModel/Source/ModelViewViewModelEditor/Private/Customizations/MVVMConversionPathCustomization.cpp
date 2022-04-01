// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVVMConversionPathCustomization.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "MVVMBlueprintViewBinding.h"
#include "PropertyHandle.h"
#include "WidgetBlueprint.h"
#include "Widgets/SMVVMConversionPath.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MVVMConversionPath"

UE::MVVM::FConversionPathCustomization::FConversionPathCustomization(UWidgetBlueprint* InWidgetBlueprint)
{
	WidgetBlueprint = InWidgetBlueprint;
}

void UE::MVVM::FConversionPathCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	SetterProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewConversionPath, SetConversionFunctionPath));
	GetterProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewConversionPath, GetConversionFunctionPath));

	HeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		InPropertyHandle->CreatePropertyValueWidget()
	];
}

FText UE::MVVM::FConversionPathCustomization::GetGetterPath() const
{
	FString Value;
	FPropertyAccess::Result Result = GetterProperty->GetValue(Value);

	if (Result == FPropertyAccess::Success)
	{
		return FText::FromString(Value);
	}

	if (Result == FPropertyAccess::MultipleValues)
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}

	return FText::GetEmpty();
}

FText UE::MVVM::FConversionPathCustomization::GetSetterPath() const
{
	FString Value;
	FPropertyAccess::Result Result = SetterProperty->GetValue(Value);

	if (Result == FPropertyAccess::Success)
	{
		return FText::FromString(Value);
	}

	if (Result == FPropertyAccess::MultipleValues)
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}

	return FText::GetEmpty();
}

void UE::MVVM::FConversionPathCustomization::OnTextCommitted(const FText& NewValue, ETextCommit::Type CommitType, bool bIsGetter)
{
	FString NewString = NewValue.ToString();
	OnFunctionPathChanged(NewString, bIsGetter);
}

void UE::MVVM::FConversionPathCustomization::OnFunctionPathChanged(const FString& NewPath, bool bIsGetter)
{
	TSharedPtr<IPropertyHandle> Handle = bIsGetter ? GetterProperty : SetterProperty;
	Handle->SetValue(NewPath);
}

void UE::MVVM::FConversionPathCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	TSharedPtr<IPropertyHandle> ParentHandle = InPropertyHandle->GetParentHandle();
	TArray<void*> RawData;
	ParentHandle->AccessRawData(RawData);

	TArray<FMVVMBlueprintViewBinding*> ViewBindings;
	for (void* Data : RawData)
	{
		ViewBindings.Add((FMVVMBlueprintViewBinding*) Data);
	}

	// setter
	ChildBuilder.AddProperty(SetterProperty.ToSharedRef())
		.CustomWidget()
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("SetterLabel", "Setter"))
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text(this, &FConversionPathCustomization::GetSetterPath)
				.OnTextCommitted(this, &FConversionPathCustomization::OnTextCommitted, false)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SMVVMConversionPath, WidgetBlueprint, false)
				.Bindings(ViewBindings)
				.OnFunctionChanged(this, &FConversionPathCustomization::OnFunctionPathChanged, false)
			]
		];

	// getter
	ChildBuilder.AddProperty(GetterProperty.ToSharedRef())
		.CustomWidget()
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("GetterLabel", "Getter"))
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(SEditableTextBox)
				.Text(this, &FConversionPathCustomization::GetGetterPath)
				.OnTextCommitted(this, &FConversionPathCustomization::OnTextCommitted, true)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SMVVMConversionPath, WidgetBlueprint, true)
				.Bindings(ViewBindings)
				.OnFunctionChanged(this, &FConversionPathCustomization::OnFunctionPathChanged, true)
			]
		];
}

#undef LOCTEXT_NAMESPACE
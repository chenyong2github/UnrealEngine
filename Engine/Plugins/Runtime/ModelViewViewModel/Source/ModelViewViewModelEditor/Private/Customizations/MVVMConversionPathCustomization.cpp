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
	SourceToDestinationProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewConversionPath, SourceToDestinationFunction));
	DestinationToSourceProperty = InPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FMVVMBlueprintViewConversionPath, DestinationToSourceFunction));

	HeaderRow.NameContent()
	[
		InPropertyHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	[
		InPropertyHandle->CreatePropertyValueWidget()
	];
}

FText UE::MVVM::FConversionPathCustomization::GetSourceToDestinationPath() const
{
	void* Value;
	FPropertyAccess::Result Result = SourceToDestinationProperty->GetValueData(Value);

	if (Result == FPropertyAccess::Success)
	{
		return FText::FromName(reinterpret_cast<FMemberReference*>(Value)->GetMemberName());
	}

	if (Result == FPropertyAccess::MultipleValues)
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}

	return FText::GetEmpty();
}

FText UE::MVVM::FConversionPathCustomization::GetDestinationToSourcePath() const
{
	void* Value;
	FPropertyAccess::Result Result = DestinationToSourceProperty->GetValueData(Value);

	if (Result == FPropertyAccess::Success)
	{
		return FText::FromName(reinterpret_cast<FMemberReference*>(Value)->GetMemberName());
	}

	if (Result == FPropertyAccess::MultipleValues)
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}

	return FText::GetEmpty();
}

void UE::MVVM::FConversionPathCustomization::OnTextCommitted(const FText& NewValue, ETextCommit::Type CommitType, bool bSourceToDestination)
{
	FString NewString = NewValue.ToString();

	UFunction* FoundFunction = nullptr;
	if (WidgetBlueprint)
	{
		FoundFunction = WidgetBlueprint->GetBlueprintClass()->FindFunctionByName(*NewString);
	}
	if (FoundFunction == nullptr)
	{
		FoundFunction = FindObject<UFunction>(nullptr, *NewString, true);
	}
	OnFunctionPathChanged(FoundFunction, bSourceToDestination);
}

void UE::MVVM::FConversionPathCustomization::OnFunctionPathChanged(const UFunction* NewFunction, bool bSourceToDestination)
{
	TSharedPtr<IPropertyHandle> Handle = bSourceToDestination ? SourceToDestinationProperty : DestinationToSourceProperty;

	FStructProperty* StructProperty = CastFieldChecked<FStructProperty>(Handle->GetProperty());
	check(StructProperty);

	void* PreviousValue;
	Handle->GetValueData(PreviousValue);

	FMemberReference NewReference;
	if (NewFunction)
	{
		NewReference.SetFromField<UFunction>(NewFunction, WidgetBlueprint ? WidgetBlueprint->SkeletonGeneratedClass : nullptr);
	}

	FString TextValue;
	StructProperty->Struct->ExportText(TextValue, &NewReference, PreviousValue, nullptr, EPropertyPortFlags::PPF_None, nullptr);
	ensure(Handle->SetValueFromFormattedString(TextValue, EPropertyValueSetFlags::DefaultFlags) == FPropertyAccess::Result::Success);
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
	ChildBuilder.AddProperty(DestinationToSourceProperty.ToSharedRef())
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
				.Text(this, &FConversionPathCustomization::GetDestinationToSourcePath)
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
	ChildBuilder.AddProperty(SourceToDestinationProperty.ToSharedRef())
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
				.Text(this, &FConversionPathCustomization::GetSourceToDestinationPath)
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
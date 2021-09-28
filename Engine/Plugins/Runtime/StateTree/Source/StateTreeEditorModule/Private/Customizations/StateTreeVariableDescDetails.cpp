// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeVariableDescDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "StateTree.h"
#include "StateTreeState.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "StateTreePropertyHelpers.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

TSharedRef<IPropertyTypeCustomization> FStateTreeVariableDescDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeVariableDescDetails);
}

void FStateTreeVariableDescDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	NameProperty = StructProperty->GetChildHandle(TEXT("Name"));
	TypeProperty = StructProperty->GetChildHandle(TEXT("Type"));
	BaseClassProperty = StructProperty->GetChildHandle(TEXT("BaseClass"));

	HeaderRow
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			// Icon slot
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SImage)
				.Image(this, &FStateTreeVariableDescDetails::GetIcon)
				.ColorAndOpacity(FLinearColor::White)
			]
			// Description
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(FMargin(6.0f, 2.0f))
			[
				SNew(STextBlock)
				.Text(this, &FStateTreeVariableDescDetails::GetDescription)
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
			]
			// Array controls
			+ SHorizontalBox::Slot()
			.Padding(FMargin(12.0f, 0.0f))
			.AutoWidth()
			[
				StructPropertyHandle->CreateDefaultPropertyButtonWidgets()
			]
		];
}

void FStateTreeVariableDescDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (NameProperty)
	{
		StructBuilder.AddProperty(NameProperty.ToSharedRef());
	}

	if (TypeProperty)
	{
		StructBuilder.AddProperty(TypeProperty.ToSharedRef());
	}

	if (BaseClassProperty)
	{
		StructBuilder.AddProperty(BaseClassProperty.ToSharedRef()).Visibility(TAttribute<EVisibility>(this, &FStateTreeVariableDescDetails::IsBaseClassVisible));
	}
}


EVisibility FStateTreeVariableDescDetails::IsBaseClassVisible() const
{
	return GetType().Get(EStateTreeVariableType::Void) == EStateTreeVariableType::Object ? EVisibility::Visible : EVisibility::Collapsed;
}

FText FStateTreeVariableDescDetails::GetDescription() const
{
	FName Name;
	if (NameProperty && NameProperty->GetValue(Name) == FPropertyAccess::Success)
	{
		return FText::FromName(Name);
	}
	return FText::GetEmpty();
}

const FSlateBrush* FStateTreeVariableDescDetails::GetIcon() const
{
	return UE::StateTree::PropertyHelpers::GetTypeIcon(GetType().Get(EStateTreeVariableType::Void));
}

TOptional<EStateTreeVariableType> FStateTreeVariableDescDetails::GetType() const
{
	if (TypeProperty)
	{
		uint8 Value;
		if (TypeProperty->GetValue(Value) == FPropertyAccess::Success)
		{
			return EStateTreeVariableType(Value);
		}
	}
	return TOptional<EStateTreeVariableType>();
}


#undef LOCTEXT_NAMESPACE

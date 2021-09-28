// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeVariableLayoutDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailPropertyRow.h"
#include "IDetailChildrenBuilder.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

TSharedRef<IPropertyTypeCustomization> FStateTreeVariableLayoutDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeVariableLayoutDetails);
}

void FStateTreeVariableLayoutDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	VariablesProperty = StructProperty->GetChildHandle(TEXT("Variables"));

	// Listen changes to the array. The list will not update automatically since we're dealing with the list items ourselves.
	const FSimpleDelegate& ForceRefresh = FSimpleDelegate::CreateSP(this, &FStateTreeVariableLayoutDetails::OnForceRefresh);
	VariablesProperty->SetOnPropertyValueChanged(ForceRefresh);

	HeaderRow
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			// Name
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				StructPropertyHandle->CreatePropertyNameWidget()
			]
			// Array controls
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(FMargin(12.0f, 0.0f))
			[
				VariablesProperty->CreateDefaultPropertyButtonWidgets()
			]
		];
}

void FStateTreeVariableLayoutDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (VariablesProperty)
	{
		// Inline descriptor array as children
		uint32 ChildNum = 0;
		if (VariablesProperty->GetNumChildren(ChildNum) == FPropertyAccess::Success)
		{
			for (uint32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
			{
				TSharedPtr<IPropertyHandle> ChildProperty = VariablesProperty->GetChildHandle(ChildIndex);
				if (ChildProperty)
				{
					StructBuilder.AddProperty(ChildProperty.ToSharedRef());
				}
			}
		}
	}
}

void FStateTreeVariableLayoutDetails::OnForceRefresh()
{
	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}



#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeTransitionDetails.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
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

#define LOCTEXT_NAMESPACE "StateTreeEditor"

TSharedRef<IPropertyTypeCustomization> FStateTreeTransitionDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeTransitionDetails);
}

void FStateTreeTransitionDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities().Get();

	StateProperty = StructProperty->GetChildHandle(TEXT("State"));
	ConditionsProperty = StructProperty->GetChildHandle(TEXT("Conditions"));

	HeaderRow
		.WholeRowContent()
		.VAlign(VAlign_Center)
		[
			SNew(SHorizontalBox)
			// Description
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &FStateTreeTransitionDetails::GetDescription)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				StructPropertyHandle->CreateDefaultPropertyButtonWidgets()
			]
		];
}

void FStateTreeTransitionDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	if (StateProperty)
	{
		IDetailPropertyRow& Property = StructBuilder.AddProperty(StateProperty.ToSharedRef());
	}

	if (ConditionsProperty)
	{
		// Show conditions always expanded, with simplified header (remove item count)
		IDetailPropertyRow& Property = StructBuilder.AddProperty(ConditionsProperty.ToSharedRef());
		Property.ShouldAutoExpand(true);

		static const bool bShowChildren = true;
		Property.CustomWidget(bShowChildren)
			.NameContent()
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(0.0f, 2.0f))
				[
					SNew(STextBlock)
					.Text(ConditionsProperty->GetPropertyDisplayName())
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
				]
			]
			.ValueContent()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				SNew(SBox) // Empty, suppress noisy array details.
			];
	}
}


FText FStateTreeTransitionDetails::GetDescription() const
{
	if (StateProperty)
	{
		TArray<void*> RawData;
		StateProperty->AccessRawData(RawData);
		if (RawData.Num() == 1)
		{
			FStateTreeStateLink* StateLink = static_cast<FStateTreeStateLink*>(RawData[0]);
			if (StateLink != nullptr)
			{
				switch (StateLink->Type)
				{
				case EStateTreeTransitionType::Succeeded:
					return LOCTEXT("TransitionSucceeded", "Succeeded");
					break;
				case EStateTreeTransitionType::Failed:
					return LOCTEXT("TransitionFailed", "Failed");
					break;
				case EStateTreeTransitionType::SelectChildState:
					return LOCTEXT("TransitionSelect", "Select");
					break;
				case EStateTreeTransitionType::GotoState:
					return FText::Join(FText::FromString(TEXT(" ")), LOCTEXT("TransitionGoto", "Go to"), FText::FromName(StateLink->Name));
					break;
				}
			}
		}
		else
		{
			return LOCTEXT("MultipleSelected", "Multiple selected");
		}
	}
	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE

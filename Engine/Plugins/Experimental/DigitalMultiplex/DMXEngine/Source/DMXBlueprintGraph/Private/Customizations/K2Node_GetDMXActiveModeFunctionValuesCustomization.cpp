// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/K2Node_GetDMXActiveModeFunctionValuesCustomization.h"
#include "K2Node_GetDMXActiveModeFunctionValues.h"
#include "K2Node_GetDMXActiveModeFunctionValues.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Input/SButton.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "DetailCategoryBuilder.h"
#include "IDetailsView.h"

#define LOCTEXT_NAMESPACE "K2Node_GetDMXActiveModeFunctionValuesCustomization"

TSharedRef<IDetailCustomization> K2Node_GetDMXActiveModeFunctionValuesCustomization::MakeInstance()
{
	return MakeShared<K2Node_GetDMXActiveModeFunctionValuesCustomization>();
}

void K2Node_GetDMXActiveModeFunctionValuesCustomization::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	this->DetailLayout = &InDetailLayout;
	UK2Node_GetDMXActiveModeFunctionValues* Node = GetK2Node_GetDMXActiveModeFunctionValues();

	static const FName FixtureSettingsCategoryName = TEXT("Fixture Settings");
	InDetailLayout.EditCategory(FixtureSettingsCategoryName, FText::GetEmpty(), ECategoryPriority::Important);
	IDetailCategoryBuilder& FunctionActionsCategory = InDetailLayout.EditCategory("DMXFunctionActions", LOCTEXT("FunctionActionCategoryName", "Function Actions"), ECategoryPriority::Important);

	FunctionActionsCategory.AddCustomRow(FText::GetEmpty())
		.WholeRowContent()
		.HAlign(HAlign_Left)
		[
			SNew( SBox )
			.MaxDesiredWidth(300.f)
			[
				SNew(SUniformGridPanel)
				.SlotPadding(2.0f)
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.OnClicked(this, &K2Node_GetDMXActiveModeFunctionValuesCustomization::ExposeFunctionsClicked)
					.ToolTipText(LOCTEXT("ExposeFunctionsButtonTooltip", "Expose Functions to Node Pins"))
					.IsEnabled_Lambda([Node]() -> bool { return Node && !Node->IsExposed(); })
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ExposeFunctionsButton", "Expose Functions"))
					]
				]
				+ SUniformGridPanel::Slot(1, 0)
				[
					SNew(SButton)
					.OnClicked(this, &K2Node_GetDMXActiveModeFunctionValuesCustomization::ResetFunctionsClicked)
					.ToolTipText(LOCTEXT("ResetFunctionsButtonTooltip", "Resets Functions from Node Pins."))
					.IsEnabled_Lambda([Node]() -> bool { return Node && Node->IsExposed(); })
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("ResetEmitterButton", "Reset Functions"))
					]
				]
			]
		];
}

FReply K2Node_GetDMXActiveModeFunctionValuesCustomization::ExposeFunctionsClicked()
{
	const TArray< TWeakObjectPtr<UObject> >& SelectedObjects = DetailLayout->GetSelectedObjects();

	for (int32 Idx = 0; Idx < SelectedObjects.Num(); ++Idx)
	{
		if (SelectedObjects[Idx].IsValid())
		{
			if (UK2Node_GetDMXActiveModeFunctionValues* K2Node_GetDMXActiveModeFunctionValues = Cast<UK2Node_GetDMXActiveModeFunctionValues>(SelectedObjects[Idx].Get()))
			{
				K2Node_GetDMXActiveModeFunctionValues->ExposeFunctions();
			}
		}
	}

	return FReply::Handled();
}

FReply K2Node_GetDMXActiveModeFunctionValuesCustomization::ResetFunctionsClicked()
{
	if (UK2Node_GetDMXActiveModeFunctionValues* K2Node_GetDMXActiveModeFunctionValues = GetK2Node_GetDMXActiveModeFunctionValues())
	{
		K2Node_GetDMXActiveModeFunctionValues->ResetFunctions();
	}

	return FReply::Handled();
}

UK2Node_GetDMXActiveModeFunctionValues* K2Node_GetDMXActiveModeFunctionValuesCustomization::GetK2Node_GetDMXActiveModeFunctionValues() const
{
	const TArray< TWeakObjectPtr<UObject> >& SelectedObjects = DetailLayout->GetSelectedObjects();

	for (int32 Idx = 0; Idx < SelectedObjects.Num(); ++Idx)
	{
		if (SelectedObjects[Idx].IsValid())
		{
			return Cast<UK2Node_GetDMXActiveModeFunctionValues>(SelectedObjects[Idx].Get());
		}
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE

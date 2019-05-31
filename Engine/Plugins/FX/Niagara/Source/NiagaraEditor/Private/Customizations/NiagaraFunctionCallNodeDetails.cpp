// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraFunctionCallNodeDetails.h"
#include "UObject/WeakObjectPtr.h"
#include "NiagaraNodeFunctionCall.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBox.h"
#include "Layout/Margin.h"
#include "NiagaraEditorStyle.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SCheckBox.h"

#define LOCTEXT_NAMESPACE "NiagaraFunctionCallNodeDetails"

TSharedRef<IDetailCustomization> FNiagaraFunctionCallNodeDetails::MakeInstance()
{
	return MakeShareable(new FNiagaraFunctionCallNodeDetails);
}

void FNiagaraFunctionCallNodeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	static const FName SwitchCategoryName = TEXT("Propagated Static Switch Values");

	TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsCustomized);
	if (ObjectsCustomized.Num() != 1 || !ObjectsCustomized[0]->IsA<UNiagaraNodeFunctionCall>())
	{
		return;
	}
	
	Node = CastChecked<UNiagaraNodeFunctionCall>(ObjectsCustomized[0].Get());
	UNiagaraGraph* CalledGraph = Node->GetCalledGraph();
	if (!CalledGraph)
	{
		return;
	}

	// For each static switch value inside the function graph we add a checkbox row to allow the user to propagate it up in the call hierachy.
	// Doing so then disables the option to set the value directly on the function call node.
	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(SwitchCategoryName);
	for (const FNiagaraVariable& SwitchInput : CalledGraph->FindStaticSwitchInputs())
	{
		FDetailWidgetRow& Row = CategoryBuilder.AddCustomRow(LOCTEXT("NiagaraFunctionPropagatedValuesFilterText", "Propagated Static Switch Values"));

		Row
		.NameContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(FText::FromName(SwitchInput.GetName()))
			]
		]
		.ValueContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([SwitchInput, this]()
				{
					return Node->PropagatedStaticSwitchParameters.Contains(SwitchInput) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([SwitchInput, this](const ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Unchecked)
					{
						Node->PropagatedStaticSwitchParameters.Remove(SwitchInput);
					}
					else if (NewState == ECheckBoxState::Checked)
					{
						Node->PropagatedStaticSwitchParameters.AddUnique(SwitchInput);
					}
					Node->RefreshFromExternalChanges();
				})
			]
		];
	}
}

#undef LOCTEXT_NAMESPACE

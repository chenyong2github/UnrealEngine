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
#include "Widgets/Input/SEditableTextBox.h"

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
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([SwitchInput, this]()
				{
					return Node->FindPropagatedVariable(SwitchInput) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([SwitchInput, this](const ECheckBoxState NewState)
				{
					if (NewState == ECheckBoxState::Unchecked)
					{
						Node->RemovePropagatedVariable(SwitchInput);
					}
					else if (NewState == ECheckBoxState::Checked)
					{
						if (!Node->FindPropagatedVariable(SwitchInput))
						{
							Node->PropagatedStaticSwitchParameters.Emplace(SwitchInput);
						}
						CopyMetadataFromCalledGraph(SwitchInput);
					}
					Node->RefreshFromExternalChanges();
				})
			]
			+ SVerticalBox::Slot()
			.HAlign(HAlign_Fill)
			[
				SNew(SEditableTextBox)
				.ToolTipText(LOCTEXT("NiagaraOverridePropagatedValueName_TooltipText", "When set, the name of the propagated switch value is changed to a different value."))
				.HintText(LOCTEXT("NiagaraOverridePropagatedValueName_HintText", "Override name"))
				.IsEnabled_Lambda([SwitchInput, this]() { return Node->FindPropagatedVariable(SwitchInput) != nullptr; })
				.OnTextCommitted_Lambda([SwitchInput, this](const FText& NewText, ETextCommit::Type CommitType)
				{
					FNiagaraPropagatedVariable* Propagated = Node->FindPropagatedVariable(SwitchInput);
					if (Propagated)
					{
						FNiagaraVariable OldVar = Propagated->ToVariable();
						Propagated->PropagatedName = NewText.ToString();
						CopyMetadataForNameOverride(OldVar, Propagated->ToVariable());
					}
				})
				.Text_Lambda([SwitchInput, this]()
				{
					FNiagaraPropagatedVariable* Propagated = Node->FindPropagatedVariable(SwitchInput);
					if (Propagated)
					{
						return FText::FromString(Propagated->PropagatedName);
					}
					return FText();
				})
			]
		];
	}
}

void FNiagaraFunctionCallNodeDetails::CopyMetadataFromCalledGraph(FNiagaraVariable FromVariable)
{
	UNiagaraGraph* NodeGraph = GetNodeGraph();
	UNiagaraGraph* CalledGraph = GetCalledGraph();
	if (!NodeGraph || !CalledGraph)
	{
		return;
	}
	if (NodeGraph->GetMetaData(FromVariable).IsSet())
	{
		return;
	}
	TOptional<FNiagaraVariableMetaData> OriginalData = CalledGraph->GetMetaData(FromVariable);
	if (OriginalData.IsSet())
	{
		NodeGraph->SetMetaData(FromVariable, OriginalData.GetValue());
		NodeGraph->NotifyGraphChanged();
	}
}

void FNiagaraFunctionCallNodeDetails::CopyMetadataForNameOverride(FNiagaraVariable FromVariable, FNiagaraVariable ToVariable)
{
	UNiagaraGraph* NodeGraph = GetNodeGraph();
	if (!NodeGraph)
	{
		return;
	}
	if (NodeGraph->GetMetaData(ToVariable).IsSet())
	{
		return;
	}
	TOptional<FNiagaraVariableMetaData> OriginalData = NodeGraph->GetMetaData(FromVariable);
	if (OriginalData.IsSet())
	{
		NodeGraph->SetMetaData(ToVariable, OriginalData.GetValue());
		NodeGraph->NotifyGraphChanged();
	}
}

UNiagaraGraph* FNiagaraFunctionCallNodeDetails::GetNodeGraph()
{
	if (!Node.IsValid())
	{
		return nullptr;
	}
	return Node->GetNiagaraGraph();
}

UNiagaraGraph* FNiagaraFunctionCallNodeDetails::GetCalledGraph()
{
	if (!Node.IsValid())
	{
		return nullptr;
	}
	return Node->GetCalledGraph();
}

#undef LOCTEXT_NAMESPACE

// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraStaticSwitchNodeDetails.h"
#include "UObject/WeakObjectPtr.h"
#include "NiagaraNodeStaticSwitch.h"
#include "DetailCategoryBuilder.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBox.h"
#include "Layout/Margin.h"
#include "NiagaraEditorStyle.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SEditableTextBox.h"

#define LOCTEXT_NAMESPACE "NiagaraStaticSwitchNodeDetails"

TSharedRef<IDetailCustomization> FNiagaraStaticSwitchNodeDetails::MakeInstance()
{
	return MakeShareable(new FNiagaraStaticSwitchNodeDetails);
}

FNiagaraStaticSwitchNodeDetails::FNiagaraStaticSwitchNodeDetails()
{
	DropdownOptions.Add(MakeShareable(new SwitchDropdownOption("Bool")));
	DropdownOptions.Add(MakeShareable(new SwitchDropdownOption("Integer")));

	for (FNiagaraTypeDefinition Type : FNiagaraTypeRegistry::GetRegisteredParameterTypes())
	{
		//TODO are these all relevant niagara enum types?
		if (Type.IsEnum())
		{
			DropdownOptions.Add(MakeShareable(new SwitchDropdownOption(Type.GetEnum()->GetName(), Type.GetEnum())));
		}
	}
}

void FNiagaraStaticSwitchNodeDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	static const FName SwitchCategoryName = TEXT("Static Switch");

	TArray<TWeakObjectPtr<UObject>> ObjectsCustomized;
	DetailBuilder.GetObjectsBeingCustomized(ObjectsCustomized);
	if (ObjectsCustomized.Num() == 1 && ObjectsCustomized[0]->IsA<UNiagaraNodeStaticSwitch>())
	{
		Node = CastChecked<UNiagaraNodeStaticSwitch>(ObjectsCustomized[0].Get());
		UpdateSelectionFromNode();
		
		IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory(SwitchCategoryName);		
		FDetailWidgetRow& NameWidget = CategoryBuilder.AddCustomRow(LOCTEXT("NiagaraSwitchNodeNameFilterText", "Input parameter name"));

		NameWidget
		.NameContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(LOCTEXT("NiagaraSwitchNodeNameText", "Input parameter name"))
			]
		]
		.ValueContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SEditableTextBox)
				.Text(this, &FNiagaraStaticSwitchNodeDetails::GetParameterNameText)
				.ToolTipText(LOCTEXT("NiagaraSwitchNodeNameTooltip", "This is the name of the parameter that is exposed to the user calling this function graph."))
				.OnTextCommitted(this, &FNiagaraStaticSwitchNodeDetails::OnParameterNameCommited)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
			]
		];

		FDetailWidgetRow& DropdownWidget = CategoryBuilder.AddCustomRow(LOCTEXT("NiagaraSwitchNodeTypeFilterText", "Input parameter type"));

		DropdownWidget
		.NameContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(LOCTEXT("NiagaraSwitchNodeTypeText", "Static switch type"))
			]
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.AutoWidth()
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SComboBox<TSharedPtr<SwitchDropdownOption>>)
				.OptionsSource(&DropdownOptions)
				.OnSelectionChanged(this, &FNiagaraStaticSwitchNodeDetails::OnSelectionChanged)
				.OnGenerateWidget(this, &FNiagaraStaticSwitchNodeDetails::CreateWidgetForDropdownOption)
				.InitiallySelectedItem(SelectedDropdownItem)
				[
					SNew(STextBlock)
					.Margin(FMargin(0.0f, 2.0f))
					.Text(this, &FNiagaraStaticSwitchNodeDetails::GetDropdownItemLabel)
				]
			]
		];

		FDetailWidgetRow& IntValueOption = CategoryBuilder.AddCustomRow(LOCTEXT("NiagaraSwitchNodeIntFilterText", "Max integer value"));

		IntValueOption
		.NameContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(STextBlock)
				.IsEnabled(this, &FNiagaraStaticSwitchNodeDetails::GetIntOptionEnabled)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(LOCTEXT("NiagaraSwitchNodeIntOptionText", "Max integer value"))
			]
		]
		.ValueContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SNumericEntryBox<int32>)
				.IsEnabled(this, &FNiagaraStaticSwitchNodeDetails::GetIntOptionEnabled)
				.AllowSpin(false)
				.MinValue(0)
				.MaxValue(99)
				.Value(this, &FNiagaraStaticSwitchNodeDetails::GetIntOptionValue)
				.OnValueCommitted(this, &FNiagaraStaticSwitchNodeDetails::IntOptionValueCommitted)
			]
		];
	}
}

TSharedRef<SWidget> FNiagaraStaticSwitchNodeDetails::CreateWidgetForDropdownOption(TSharedPtr<SwitchDropdownOption> InOption)
{
	return SNew(STextBlock).Text(FText::FromString(*InOption->Name));
}

void FNiagaraStaticSwitchNodeDetails::OnSelectionChanged(TSharedPtr<SwitchDropdownOption> NewValue, ESelectInfo::Type)
{
	SelectedDropdownItem = NewValue;
	if (!SelectedDropdownItem.IsValid() || !Node.IsValid())
	{
		return;
	}
	
	FNiagaraTypeDefinition OldType = Node->GetInputType();
	if (SelectedDropdownItem == DropdownOptions[0])
	{
		Node->SwitchTypeData.SwitchType = ENiagaraStaticSwitchType::Bool;
	}
	else if (SelectedDropdownItem == DropdownOptions[1])
	{
		Node->SwitchTypeData.SwitchType = ENiagaraStaticSwitchType::Integer;
	}
	else
	{
		Node->SwitchTypeData.SwitchType = ENiagaraStaticSwitchType::Enum;
		Node->SwitchTypeData.Enum = SelectedDropdownItem->Enum;
	}
	Node->OnSwitchParameterTypeChanged(OldType);
}

FText FNiagaraStaticSwitchNodeDetails::GetDropdownItemLabel() const
{
	if (SelectedDropdownItem.IsValid())
	{
		return FText::FromString(*SelectedDropdownItem->Name);
	}

	return LOCTEXT("InvalidNiagaraStaticSwitchNodeComboEntryText", "<Invalid selection>");
}

void FNiagaraStaticSwitchNodeDetails::UpdateSelectionFromNode()
{
	SelectedDropdownItem.Reset();
	ENiagaraStaticSwitchType SwitchType = Node->SwitchTypeData.SwitchType;

	if (SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		SelectedDropdownItem = DropdownOptions[0];
	}
	else if (SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		SelectedDropdownItem = DropdownOptions[1];
	}
	else if (SwitchType == ENiagaraStaticSwitchType::Enum && Node->SwitchTypeData.Enum)
	{
		FString SelectedName = Node->SwitchTypeData.Enum->GetName();
		for (TSharedPtr<SwitchDropdownOption>& Option : DropdownOptions)
		{
			if (SelectedName.Equals(*Option->Name))
			{
				SelectedDropdownItem = Option;
				return;
			}
		}
	}
}

bool FNiagaraStaticSwitchNodeDetails::GetIntOptionEnabled() const
{
	return Node.IsValid() && Node->SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer;
}

TOptional<int32> FNiagaraStaticSwitchNodeDetails::GetIntOptionValue() const
{
	return Node.IsValid() ? TOptional<int32>(Node->SwitchTypeData.MaxIntCount) : TOptional<int32>();
}

void FNiagaraStaticSwitchNodeDetails::IntOptionValueCommitted(int32 Value, ETextCommit::Type CommitInfo)
{
	if (Node.IsValid() && Value > 0)
	{
		Node->SwitchTypeData.MaxIntCount = Value;
		Node->RefreshFromExternalChanges();
	}
}

FText FNiagaraStaticSwitchNodeDetails::GetParameterNameText() const
{
	return Node.IsValid() ? FText::FromName(Node->InputParameterName) : FText();
}

void FNiagaraStaticSwitchNodeDetails::OnParameterNameCommited(const FText& InText, ETextCommit::Type InCommitType)
{
	if (Node.IsValid())
	{
		Node->ChangeSwitchParameterName(FName(*InText.ToString()));
	}
}

#undef LOCTEXT_NAMESPACE

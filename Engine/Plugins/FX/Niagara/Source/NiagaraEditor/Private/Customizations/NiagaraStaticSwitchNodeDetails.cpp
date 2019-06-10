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
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "NiagaraConstants.h"

#define LOCTEXT_NAMESPACE "NiagaraStaticSwitchNodeDetails"

TSharedRef<IDetailCustomization> FNiagaraStaticSwitchNodeDetails::MakeInstance()
{
	return MakeShareable(new FNiagaraStaticSwitchNodeDetails);
}

FNiagaraStaticSwitchNodeDetails::FNiagaraStaticSwitchNodeDetails()
{
	DropdownOptions.Add(MakeShareable(new SwitchDropdownOption("Bool")));
	DropdownOptions.Add(MakeShareable(new SwitchDropdownOption("Bool Constant")));
	DropdownOptions.Add(MakeShareable(new SwitchDropdownOption("Integer")));
	DropdownOptions.Add(MakeShareable(new SwitchDropdownOption("Integer Constant")));
	DropdownOptions.Add(MakeShareable(new SwitchDropdownOption("Enum Constant")));

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
		FDetailWidgetRow& DropdownWidget = CategoryBuilder.AddCustomRow(LOCTEXT("NiagaraSwitchNodeTypeFilterText", "Input parameter type"));
		FDetailWidgetRow& IntValueOption = CategoryBuilder.AddCustomRow(LOCTEXT("NiagaraSwitchNodeIntFilterText", "Max integer value"));
		FDetailWidgetRow& DefaultValueOption = CategoryBuilder.AddCustomRow(LOCTEXT("NiagaraSwitchNodeDefaultFilterText", "Default value"));
		FDetailWidgetRow& ConstantSelection = CategoryBuilder.AddCustomRow(LOCTEXT("NiagaraSwitchNodeConstantFilterText", "Compiler constant"));

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
				.IsEnabled(this, &FNiagaraStaticSwitchNodeDetails::GetDefaultOptionEnabled)
				.ToolTipText(LOCTEXT("NiagaraSwitchNodeNameTooltip", "This is the name of the parameter that is exposed to the user calling this function graph."))
				.OnTextCommitted(this, &FNiagaraStaticSwitchNodeDetails::OnParameterNameCommited)
				.SelectAllTextWhenFocused(true)
				.RevertTextOnEscape(true)
			]
		];

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

		DefaultValueOption
		.NameContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(STextBlock)
				.IsEnabled(this, &FNiagaraStaticSwitchNodeDetails::GetDefaultOptionEnabled)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(LOCTEXT("NiagaraSwitchNodeDefaultOptionText", "Default value"))
			]
		]
		.ValueContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(SWidgetSwitcher)
				.IsEnabled(this, &FNiagaraStaticSwitchNodeDetails::GetDefaultOptionEnabled)
		  		.WidgetIndex(this, &FNiagaraStaticSwitchNodeDetails::GetDefaultWidgetIndex)

		  		+ SWidgetSwitcher::Slot()
		  		[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return GetSwitchDefaultValue().Get(0) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged(this, &FNiagaraStaticSwitchNodeDetails::DefaultBoolValueCommitted)
				]

		  		+ SWidgetSwitcher::Slot()
		  		[
					SNew(SNumericEntryBox<int32>)
					.AllowSpin(false)
					.MinValue(0)
					.MaxValue(99)
					.Value(this, &FNiagaraStaticSwitchNodeDetails::GetSwitchDefaultValue)
					.OnValueCommitted(this, &FNiagaraStaticSwitchNodeDetails::DefaultIntValueCommitted)
				]
				
				+ SWidgetSwitcher::Slot()
		  		[
					SNew(SComboBox<TSharedPtr<DefaultEnumOption>>)
					.OptionsSource(&DefaultEnumDropdownOptions)
					.OnSelectionChanged(this, &FNiagaraStaticSwitchNodeDetails::OnSelectionChanged)
					.OnGenerateWidget(this, &FNiagaraStaticSwitchNodeDetails::CreateWidgetForDropdownOption)
					.InitiallySelectedItem(SelectedDefaultValue)
					[
						SNew(STextBlock)
						.Margin(FMargin(0.0f, 2.0f))
						.Text(this, &FNiagaraStaticSwitchNodeDetails::GetDefaultSelectionItemLabel)
					]
				]
			]
		];

		ConstantSelection
		.NameContent()
		[
			SNew(SBox)
			.Padding(FMargin(0.0f, 2.0f))
			[
				SNew(STextBlock)
				.IsEnabled(this, &FNiagaraStaticSwitchNodeDetails::IsConstantSelection)
				.TextStyle(FNiagaraEditorStyle::Get(), "NiagaraEditor.ParameterText")
				.Text(LOCTEXT("NiagaraSwitchNodeConstantText", "Compiler constant"))
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
				SNew(SComboBox<TSharedPtr<ConstantDropdownOption>>)
				.IsEnabled(this, &FNiagaraStaticSwitchNodeDetails::IsConstantSelection)
				.OptionsSource(&ConstantOptions)
				.OnSelectionChanged(this, &FNiagaraStaticSwitchNodeDetails::OnSelectionChanged)
				.OnGenerateWidget(this, &FNiagaraStaticSwitchNodeDetails::CreateWidgetForDropdownOption)
				.InitiallySelectedItem(SelectedConstantItem)
				[
					SNew(STextBlock)
					.Margin(FMargin(0.0f, 2.0f))
					.Text(this, &FNiagaraStaticSwitchNodeDetails::GetConstantSelectionItemLabel)
				]
			]
		];

		RefreshDropdownValues();
	}
}

int32 FNiagaraStaticSwitchNodeDetails::GetDefaultWidgetIndex() const
{
	if (!Node.IsValid())
	{
		return 0;
	}
	ENiagaraStaticSwitchType Type = Node->SwitchTypeData.SwitchType;
	return Type == ENiagaraStaticSwitchType::Bool ? 0 : (Type == ENiagaraStaticSwitchType::Integer ? 1 : 2);
}

TOptional<int32> FNiagaraStaticSwitchNodeDetails::GetSwitchDefaultValue() const
{
	TOptional<FNiagaraVariableMetaData> MetaData = GetSwitchParameterMetadata();
	return MetaData.IsSet() ? TOptional<int32>(MetaData->StaticSwitchDefaultValue) : TOptional<int32>();
}

void FNiagaraStaticSwitchNodeDetails::DefaultIntValueCommitted(int32 Value, ETextCommit::Type CommitInfo)
{
	TOptional<FNiagaraVariableMetaData> MetaData = GetSwitchParameterMetadata();
	if (MetaData.IsSet())
	{
		MetaData->StaticSwitchDefaultValue = Value;
		SetSwitchParameterMetadata(MetaData.GetValue());
	}
}

void FNiagaraStaticSwitchNodeDetails::DefaultBoolValueCommitted(ECheckBoxState NewState)
{
	TOptional<FNiagaraVariableMetaData> MetaData = GetSwitchParameterMetadata();
	if (MetaData.IsSet())
	{
		MetaData->StaticSwitchDefaultValue = (NewState == ECheckBoxState::Checked) ? 1 : 0;
		SetSwitchParameterMetadata(MetaData.GetValue());
	}
}

TSharedRef<SWidget> FNiagaraStaticSwitchNodeDetails::CreateWidgetForDropdownOption(TSharedPtr<SwitchDropdownOption> InOption)
{
	return SNew(STextBlock).Text(FText::FromString(*InOption->Name));
}

TSharedRef<SWidget> FNiagaraStaticSwitchNodeDetails::CreateWidgetForDropdownOption(TSharedPtr<DefaultEnumOption> InOption)
{
	return SNew(STextBlock).Text(InOption->DisplayName);
}

TSharedRef<SWidget> FNiagaraStaticSwitchNodeDetails::CreateWidgetForDropdownOption(TSharedPtr<ConstantDropdownOption> InOption)
{
	return SNew(STextBlock)
		.Text(InOption->DisplayName)
		.ToolTipText(InOption->Tooltip);
}

FText FNiagaraStaticSwitchNodeDetails::GetConstantSelectionItemLabel() const
{
	if (SelectedConstantItem.IsValid())
	{
		return SelectedConstantItem->DisplayName;
	}

	return LOCTEXT("InvalidNiagaraStaticSwitchNodeComboEntryText", "<Invalid selection>");
}

void FNiagaraStaticSwitchNodeDetails::OnSelectionChanged(TSharedPtr<DefaultEnumOption> NewValue, ESelectInfo::Type)
{
	SelectedDefaultValue = NewValue;
	TOptional<FNiagaraVariableMetaData> MetaData = GetSwitchParameterMetadata();
	if (!SelectedDefaultValue.IsValid() || !MetaData.IsSet())
	{
		return;
	}

	UEnum* Enum = Node->SwitchTypeData.Enum;
	if (!Enum)
	{
		return;
	}

	MetaData->StaticSwitchDefaultValue = SelectedDefaultValue->EnumIndex;
	SetSwitchParameterMetadata(MetaData.GetValue());
}

void FNiagaraStaticSwitchNodeDetails::OnSelectionChanged(TSharedPtr<SwitchDropdownOption> NewValue, ESelectInfo::Type)
{
	SelectedDropdownItem = NewValue;
	if (!SelectedDropdownItem.IsValid() || !Node.IsValid())
	{
		return;
	}
	
	FNiagaraTypeDefinition OldType = Node->GetInputType();
	Node->SwitchTypeData.Enum = nullptr;
	Node->SwitchTypeData.SwitchConstant = NAME_None;
	if (SelectedDropdownItem == DropdownOptions[0] || SelectedDropdownItem == DropdownOptions[1])
	{
		Node->SwitchTypeData.SwitchType = ENiagaraStaticSwitchType::Bool;
	}
	else if (SelectedDropdownItem == DropdownOptions[2] || SelectedDropdownItem == DropdownOptions[3])
	{
		Node->SwitchTypeData.SwitchType = ENiagaraStaticSwitchType::Integer;
	}
	else
	{
		Node->SwitchTypeData.SwitchType = ENiagaraStaticSwitchType::Enum;
		if (!IsConstantSelection())
		{
			Node->SwitchTypeData.Enum = SelectedDropdownItem->Enum;
		}
	}
	RefreshDropdownValues();
	Node->OnSwitchParameterTypeChanged(OldType);
}

void FNiagaraStaticSwitchNodeDetails::OnSelectionChanged(TSharedPtr<ConstantDropdownOption> NewValue, ESelectInfo::Type)
{
	SelectedConstantItem = NewValue;
	if (!NewValue.IsValid() || !Node.IsValid())
	{
		return;
	}
	Node->SwitchTypeData.SwitchConstant = NewValue->Constant.GetName();
	
	// in case of an enum constant we also need the enum type
	if (SelectedDropdownItem == DropdownOptions[4])
	{
		Node->SwitchTypeData.Enum = NewValue->Constant.GetType().Enum;
	}
	else
	{
		Node->SwitchTypeData.Enum = nullptr;
	}
	Node->RefreshFromExternalChanges();
}

FText FNiagaraStaticSwitchNodeDetails::GetDropdownItemLabel() const
{
	if (SelectedDropdownItem.IsValid())
	{
		return FText::FromString(*SelectedDropdownItem->Name);
	}

	return LOCTEXT("InvalidNiagaraStaticSwitchNodeComboEntryText", "<Invalid selection>");
}

FText FNiagaraStaticSwitchNodeDetails::GetDefaultSelectionItemLabel() const
{
	if (SelectedDefaultValue.IsValid())
	{
		return SelectedDefaultValue->DisplayName;
	}

	return LOCTEXT("InvalidNiagaraStaticSwitchNodeComboEntryText", "<Invalid selection>");
}

void FNiagaraStaticSwitchNodeDetails::RefreshDropdownValues()
{
	if (!Node.IsValid())
	{
		return;
	}

	// refresh the constant value dropdown values
	ENiagaraStaticSwitchType SwitchType = Node->SwitchTypeData.SwitchType;
	if (IsConstantSelection())
	{
		ConstantOptions.Empty();
		SelectedConstantItem.Reset();

		TArray<FNiagaraVariable> AllConstants;
		AllConstants.Append(FNiagaraConstants::GetStaticSwitchConstants());

		for (const FNiagaraVariable& Var : AllConstants)
		{
			FText DisplayName = FText::FromName(Var.GetName());
			FText Tooltip = FNiagaraConstants::GetEngineConstantDescription(Var);
			if ((SwitchType == ENiagaraStaticSwitchType::Bool && Var.GetType() == FNiagaraTypeDefinition::GetBoolDef()) ||
				(SwitchType == ENiagaraStaticSwitchType::Integer && Var.GetType() == FNiagaraTypeDefinition::GetIntDef()) ||
				(SwitchType == ENiagaraStaticSwitchType::Enum && Var.GetType().IsEnum()))
			{				
				ConstantOptions.Add(MakeShared<ConstantDropdownOption>(DisplayName, Tooltip, Var));
				if (Node->SwitchTypeData.SwitchConstant == Var.GetName())
				{
					SelectedConstantItem = ConstantOptions.Last();
				}
			}
		}
		if (!SelectedConstantItem.IsValid() && ConstantOptions.Num() > 0)
		{
			OnSelectionChanged(ConstantOptions[0], ESelectInfo::Type::Direct);
		}
		if (ConstantOptions.Num() == 0)
		{
			OnSelectionChanged(MakeShared<ConstantDropdownOption>(LOCTEXT("NiagaraNoConstantsForTypeText", "<No entries>"), FText(), FNiagaraVariable()), ESelectInfo::Type::Direct);
		}
	}

	// refresh the default options dropdown values
	if (SwitchType != ENiagaraStaticSwitchType::Enum)
	{
		return;
	}
	TOptional<FNiagaraVariableMetaData> MetaData = GetSwitchParameterMetadata();

	DefaultEnumDropdownOptions.Empty();
	UEnum* Enum = Node->SwitchTypeData.Enum;
	if (Enum)
	{
		SelectedDefaultValue.Reset();
		for (int i = 0; i < Enum->GetMaxEnumValue(); i++)
		{
			if (!Enum->IsValidEnumValue(i))
			{
				continue;
			}
			FText DisplayName = Enum->GetDisplayNameTextByIndex(i);
			DefaultEnumDropdownOptions.Add(MakeShared<DefaultEnumOption>(DisplayName, i));

			if (MetaData.IsSet() && i == MetaData->StaticSwitchDefaultValue)
			{
				SelectedDefaultValue = DefaultEnumDropdownOptions[i];
			}
		}
		if (!SelectedDefaultValue.IsValid() && DefaultEnumDropdownOptions.Num() > 0)
		{
			OnSelectionChanged(DefaultEnumDropdownOptions[0], ESelectInfo::Type::Direct);
		}
	}
}

TOptional<FNiagaraVariableMetaData> FNiagaraStaticSwitchNodeDetails::GetSwitchParameterMetadata() const
{
	if (!Node.IsValid() || !Node->GetNiagaraGraph())
	{
		TOptional<FNiagaraVariableMetaData> Empty;
		return Empty;
	}
	return Node->GetNiagaraGraph()->GetMetaData(FNiagaraVariable(Node->GetInputType(), Node->InputParameterName));
}

void FNiagaraStaticSwitchNodeDetails::SetSwitchParameterMetadata(const FNiagaraVariableMetaData& MetaData)
{
	if (!Node.IsValid() || !Node->GetNiagaraGraph())
	{
		return;
	}
	Node->GetNiagaraGraph()->SetMetaData(FNiagaraVariable(Node->GetInputType(), Node->InputParameterName), MetaData);
}

void FNiagaraStaticSwitchNodeDetails::UpdateSelectionFromNode()
{
	SelectedDropdownItem.Reset();
	ENiagaraStaticSwitchType SwitchType = Node->SwitchTypeData.SwitchType;
	bool IsConstantSelection = Node->IsSetByCompiler();

	if (SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		SelectedDropdownItem = IsConstantSelection ? DropdownOptions[1] : DropdownOptions[0];
	}
	else if (SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		SelectedDropdownItem = IsConstantSelection ? DropdownOptions[3] : DropdownOptions[2];
	}
	else if (SwitchType == ENiagaraStaticSwitchType::Enum && Node->SwitchTypeData.Enum)
	{
		if (IsConstantSelection)
		{
			SelectedDropdownItem = DropdownOptions[4];
		}
		else
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
}

bool FNiagaraStaticSwitchNodeDetails::IsConstantSelection() const
{
	return SelectedDropdownItem == DropdownOptions[1] || SelectedDropdownItem == DropdownOptions[3] || SelectedDropdownItem == DropdownOptions[4];
}

bool FNiagaraStaticSwitchNodeDetails::GetIntOptionEnabled() const
{
	return Node.IsValid() && Node->SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer && (!IsConstantSelection() || SelectedDropdownItem == DropdownOptions[3]);
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

bool FNiagaraStaticSwitchNodeDetails::GetDefaultOptionEnabled() const
{
	return !IsConstantSelection();
}

#undef LOCTEXT_NAMESPACE

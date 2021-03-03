// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNodeStaticSwitch.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraHlslTranslator.h"
#include "NiagaraConstants.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraScriptVariable.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "ScopedTransaction.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraSettings.h"
#include "ToolMenu.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeStaticSwitch"

UNiagaraNodeStaticSwitch::UNiagaraNodeStaticSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), InputParameterName(FName(TEXT("Undefined parameter name"))), IsValueSet(false), SwitchValue(0)
{
}

FNiagaraTypeDefinition UNiagaraNodeStaticSwitch::GetInputType() const
{
	if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		return FNiagaraTypeDefinition::GetBoolDef();
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		return FNiagaraTypeDefinition::GetIntDef();
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		return FNiagaraTypeDefinition(SwitchTypeData.Enum);
	}
	return FNiagaraTypeDefinition();
}

FString UNiagaraNodeStaticSwitch::GetInputCaseName(int32 Case) const
{
	if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		if (Case == 0)
		{
			return TEXT("False");
		}
		if (Case == 1)
		{
			return TEXT("True");
		}
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		return FString::FromInt(Case);
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		return SwitchTypeData.Enum->GetDisplayNameTextByValue(Case).ToString();
	}

	return TEXT("");
}

TArray<int32> UNiagaraNodeStaticSwitch::GetOptionValues() const
{
	TArray<int32> OptionValues;
	if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		OptionValues = { 1, 0 };
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		for(int32 Counter = 0; Counter < NumOptionsPerVariable; Counter++)
		{
			OptionValues.Add(Counter);
		}
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		UEnum* Enum = SwitchTypeData.Enum;

		for (int32 EnumIndex = 0; EnumIndex < Enum->NumEnums() - 1; ++EnumIndex)
		{
			bool const bShouldBeHidden = ShouldHideEnumEntry(Enum, EnumIndex);

			if (!bShouldBeHidden)
			{
				OptionValues.Add(Enum->GetValueByIndex(EnumIndex));
			}
		}
	}

	return OptionValues;
}

FName UNiagaraNodeStaticSwitch::GetOptionPinName(const FNiagaraVariable& Variable, int32 Value) const
{
	FString Suffix = TEXT("");
	if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		if (Value == 0)
		{
			Suffix = TEXT("false");
		}
		if (Value == 1)
		{
			Suffix =  TEXT("true");
		}
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		Suffix = FString::FromInt(Value);
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		Suffix = SwitchTypeData.Enum->GetDisplayNameTextByValue(Value).ToString();
	}

	return FName(Variable.GetName().ToString() + FString::Printf(TEXT(" if %s"), *Suffix));
}

void UNiagaraNodeStaticSwitch::ChangeSwitchParameterName(const FName& NewName)
{
	FNiagaraVariable OldValue(GetInputType(), InputParameterName);
	InputParameterName = NewName;
	GetNiagaraGraph()->RenameParameter(OldValue, NewName, true);
	VisualsChangedDelegate.Broadcast(this);
	RemoveUnusedGraphParameter(OldValue);	
}

void UNiagaraNodeStaticSwitch::OnSwitchParameterTypeChanged(const FNiagaraTypeDefinition& OldType)
{
	TOptional<FNiagaraVariableMetaData> OldMetaData = GetNiagaraGraph()->GetMetaData(FNiagaraVariable(OldType, InputParameterName));
	RefreshFromExternalChanges(); // Magick happens here: The old pins are destroyed and new ones are created.
	if (OldMetaData.IsSet())
	{
		GetNiagaraGraph()->SetMetaData(FNiagaraVariable(GetInputType(), InputParameterName), OldMetaData.GetValue());
	}

	VisualsChangedDelegate.Broadcast(this);
	RemoveUnusedGraphParameter(FNiagaraVariable(OldType, InputParameterName));
}

void UNiagaraNodeStaticSwitch::SetSwitchValue(int Value)
{
	IsValueSet = true;
	SwitchValue = Value;
}

void UNiagaraNodeStaticSwitch::SetSwitchValue(const FCompileConstantResolver& ConstantResolver)
{
	if (!IsSetByCompiler())
	{
		return;
	}
	ClearSwitchValue();

	const FNiagaraVariable* Found = FNiagaraConstants::FindStaticSwitchConstant(SwitchTypeData.SwitchConstant);
	FNiagaraVariable Constant = Found ? *Found : FNiagaraVariable();
	if (Found && ConstantResolver.ResolveConstant(Constant))
	{
		if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
		{
			SwitchValue = Constant.GetValue<bool>();
			IsValueSet = true;
		}
		else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer || SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum)
		{
			SwitchValue = Constant.GetValue<int32>();
			IsValueSet = true;
		}
	}
}

void UNiagaraNodeStaticSwitch::ClearSwitchValue()
{
	IsValueSet = false;
	SwitchValue = 0;
}

bool UNiagaraNodeStaticSwitch::IsSetByCompiler() const
{
	return !SwitchTypeData.SwitchConstant.IsNone();
}

void UNiagaraNodeStaticSwitch::RemoveUnusedGraphParameter(const FNiagaraVariable& OldParameter)
{
	TArray<FNiagaraVariable> GraphVariables = GetNiagaraGraph()->FindStaticSwitchInputs();
	int Index = GraphVariables.Find(OldParameter);
	if (Index == INDEX_NONE)
	{
		// Force delete the old static switch parameter.
		GetNiagaraGraph()->RemoveParameter(OldParameter, true);
	}
	else
	{
		GetNiagaraGraph()->NotifyGraphChanged();
	}

	// force the graph to refresh the metadata
	GetNiagaraGraph()->GetParameterReferenceMap();
}

void UNiagaraNodeStaticSwitch::AllocateDefaultPins()
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

	// create the input pins which differ in count depending on the switch type
	if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		NumOptionsPerVariable = 2;
		for (FNiagaraVariable& Var : OutputVars)
		{
			AddOptionPin(Var, 1);
			
		}
		for (FNiagaraVariable& Var : OutputVars)
		{
			AddOptionPin(Var, 0);
		}
		GetNiagaraGraph()->AddParameter(FNiagaraVariable(GetInputType(), InputParameterName), true);
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		for (int32 i = 0; i < NumOptionsPerVariable; i++)
		{
			for (FNiagaraVariable& Var : OutputVars)
			{
				AddOptionPin(Var, i);
			}
		}
		GetNiagaraGraph()->AddParameter(FNiagaraVariable(GetInputType(), InputParameterName), true);
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		UEnum* Enum = SwitchTypeData.Enum;
		
		int32 NextNaturalIndex = 0;
		for (int32 EnumIndex = 0; EnumIndex < Enum->NumEnums() - 1; ++EnumIndex)
		{
			bool const bShouldBeHidden = ShouldHideEnumEntry(Enum, EnumIndex);

			if (!bShouldBeHidden)
			{
				NextNaturalIndex++;
				for (const FNiagaraVariable& Variable : OutputVars)
				{
					AddOptionPin(Variable, Enum->GetValueByIndex(EnumIndex));
				}
			}
		}
		
		NumOptionsPerVariable = NextNaturalIndex;
		
		GetNiagaraGraph()->AddParameter(FNiagaraVariable(GetInputType(), InputParameterName), true);
	}
	
	// create the output pins
	for (int32 Index = 0; Index < OutputVars.Num(); Index++)
	{
		const FNiagaraVariable& Var = OutputVars[Index];
		UEdGraphPin* NewPin = CreatePin(EGPD_Output, Schema->TypeDefinitionToPinType(Var.GetType()), Var.GetName());
		NewPin->PersistentGuid = OutputVarGuids[Index];
	}

	CreateAddPin(EGPD_Output);

	// force the graph to refresh the metadata
	GetNiagaraGraph()->GetParameterReferenceMap();
}

FString UNiagaraNodeStaticSwitch::GetOptionPinSuffix(int32 Index) const
{
	FString PathSuffix = TEXT(" if ");
	if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		PathSuffix += Index == 0 ? TEXT("true") : TEXT("false");
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		PathSuffix += FString::FromInt(Index);
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		FText EnumName = SwitchTypeData.Enum->GetDisplayNameTextByIndex(Index);
		PathSuffix += EnumName.ToString();
	}

	return PathSuffix;
}

bool UNiagaraNodeStaticSwitch::AllowNiagaraTypeForAddPin(const FNiagaraTypeDefinition& InType) const
{
	// explicitly allow parameter maps and numeric types
	return InType.GetScriptStruct() != nullptr;
}

bool UNiagaraNodeStaticSwitch::GetVarIndex(FHlslNiagaraTranslator* Translator, int32 InputPinCount, int32& VarIndexOut) const
{	
	return GetVarIndex(Translator, InputPinCount, SwitchValue, VarIndexOut);
}

void UNiagaraNodeStaticSwitch::UpdateCompilerConstantValue(FHlslNiagaraTranslator* Translator)
{
	if (!IsSetByCompiler() || !Translator)
	{
		return;
	}
	ClearSwitchValue();

	const FNiagaraVariable* Found = FNiagaraConstants::FindStaticSwitchConstant(SwitchTypeData.SwitchConstant);
	FNiagaraVariable Constant = Found ? *Found : FNiagaraVariable();
	if (Found && Translator->GetLiteralConstantVariable(Constant))
	{
		if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
		{
			SwitchValue = Constant.GetValue<bool>();
			IsValueSet = true;
		}
		else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer || SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum)
		{
			SwitchValue = Constant.GetValue<int32>();
			IsValueSet = true;
		}
		else
		{
			Translator->Error(LOCTEXT("InvalidSwitchType", "Invalid static switch type."), this, nullptr);
		}
	}
	else
	{
		Translator->Error(FText::Format(LOCTEXT("InvalidConstantValue", "Unable to determine constant value '{0}' for static switch."), FText::FromName(SwitchTypeData.SwitchConstant)), this, nullptr);
	}
}

bool UNiagaraNodeStaticSwitch::GetVarIndex(FHlslNiagaraTranslator* Translator, int32 InputPinCount, int32 Value, int32& VarIndexOut) const
{
	bool Success = false;
	if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		VarIndexOut = Value ? 0 : InputPinCount / 2;
		Success = true;
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		int32 MaxValue = NumOptionsPerVariable;
		if (MaxValue >= 0)
		{
			if (Translator && (Value > MaxValue || Value < 0))
			{
				Translator->Warning(FText::Format(LOCTEXT("InvalidStaticSwitchIntValue", "The supplied int value {0} is outside the bounds for the static switch."), FText::FromString(FString::FromInt(Value))), this, nullptr);
			}
			VarIndexOut = FMath::Clamp(Value, 0, MaxValue) * (InputPinCount / (MaxValue + 1));
			Success = true;
		}
		else if (Translator)
		{
			Translator->Error(FText::Format(LOCTEXT("InvalidSwitchMaxIntValue", "Invalid max int value {0} for static switch."), FText::FromString(FString::FromInt(Value))), this, nullptr);
		}
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		TArray<int32> OptionValues = GetOptionValues();

		int32 MaxValue = OptionValues.Num();
		if (MaxValue > 0)
		{
			// do a sanity check here if the number of pins actually matches the enum count (which might have changed in the meantime without us noticing)
			FPinCollectorArray LocalOutputPins;
			GetOutputPins(LocalOutputPins);
			int32 OutputPinCount = LocalOutputPins.Num() - 1;
			if (OutputPinCount > 0 && OutputPinCount * NumOptionsPerVariable != InputPinCount)
			{
				if (Translator)
				{
					Translator->Error(FText::Format(LOCTEXT("InvalidSwitchEnumDefinition", "The number of pins on the static switch does not match the number of values defined in the enum."), FText::FromString(FString::FromInt(SwitchValue))), this, nullptr);
				}
			}
			if (Value <= MaxValue && Value >= 0)
			{
				VarIndexOut = Value * (InputPinCount / MaxValue);
				Success = true;
			}
			else if (Translator)
			{
				Translator->Error(FText::Format(LOCTEXT("InvalidSwitchEnumIndex", "Invalid static switch value \"{0}\" for enum value index."), FText::FromString(FString::FromInt(SwitchValue))), this, nullptr);
			}
		}
	}
	else if (Translator)
	{
		Translator->Error(LOCTEXT("InvalidSwitchType", "Invalid static switch type."), this, nullptr);
	}
	return Success;
}

void UNiagaraNodeStaticSwitch::Compile(FHlslNiagaraTranslator* Translator, TArray<int32>& Outputs)
{
	UNiagaraNode::Compile(Translator, Outputs);
}

bool UNiagaraNodeStaticSwitch::SubstituteCompiledPin(FHlslNiagaraTranslator* Translator, UEdGraphPin** LocallyOwnedPin)
{
	// if we compile the standalone module or function we don't have any valid input yet, so we just take the first option to satisfy the compiler
	ENiagaraScriptUsage TargetUsage = Translator->GetTargetUsage();
	bool IsDryRun = TargetUsage == ENiagaraScriptUsage::Module || TargetUsage == ENiagaraScriptUsage::Function;
	if (IsDryRun)
	{
		SwitchValue = 0;
	}
	else
	{
		UpdateCompilerConstantValue(Translator);
	}
	if (!IsValueSet && !IsDryRun)
	{
		FText ErrorMessage = FText::Format(LOCTEXT("MissingSwitchValue", "The input parameter \"{0}\" is not set to a constant value for the static switch node."), FText::FromString(InputParameterName.ToString()));
		Translator->Error(ErrorMessage, this, nullptr);
		return false;
	}

	FPinCollectorArray InputPins;
	GetInputPins(InputPins);
	FPinCollectorArray OutputPins;
	GetOutputPins(OutputPins);

	for (int i = 0; i < OutputPins.Num(); i++)
	{
		UEdGraphPin* OutPin = OutputPins[i];
		int32 VarIdx;
		if (OutPin == *LocallyOwnedPin && GetVarIndex(Translator, InputPins.Num(), IsDryRun ? 0 : SwitchValue, VarIdx))
		{
			UEdGraphPin* InputPin = InputPins[VarIdx + i];
			if (InputPin->LinkedTo.Num() == 1)
			{
				*LocallyOwnedPin = GetTracedOutputPin(InputPin->LinkedTo[0], true);
				return true;
			}
			else
			{
				*LocallyOwnedPin = InputPin;
			}
			return true;
		}
	}
	return false;
}

void UNiagaraNodeStaticSwitch::AddIntegerInputPin()
{
	FScopedTransaction Transaction(LOCTEXT("AddIntegerPinTransaction", "Added integer input pin to static switch"));

	this->Modify();

	NumOptionsPerVariable++;
	ReallocatePins();
}

void UNiagaraNodeStaticSwitch::RemoveIntegerInputPin()
{
	FScopedTransaction Transaction(LOCTEXT("RemoveIntegerPinTransaction", "Removed integer input pin from static switch"));

	this->Modify();

	NumOptionsPerVariable = FMath::Max(1, --NumOptionsPerVariable);
	ReallocatePins();
}

FText UNiagaraNodeStaticSwitch::GetIntegerAddButtonTooltipText() const
{
	return LOCTEXT("IntegerAddButtonTooltip", "Add a new input pin");
}

FText UNiagaraNodeStaticSwitch::GetIntegerRemoveButtonTooltipText() const
{
	return LOCTEXT("IntegerRemoveButtonTooltip", "Remove the input pin with the highest index");
}

EVisibility UNiagaraNodeStaticSwitch::ShowAddIntegerButton() const
{
	return SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility UNiagaraNodeStaticSwitch::ShowRemoveIntegerButton() const
{
	return SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer && NumOptionsPerVariable > 2 ? EVisibility::Visible : EVisibility::Collapsed;
}

void UNiagaraNodeStaticSwitch::PostLoad()
{
	Super::PostLoad();

	// Make sure that we are added to the static switch list.
	if (GetInputType().IsValid() && InputParameterName.IsValid())
	{
		UNiagaraScriptVariable* Var = GetNiagaraGraph()->GetScriptVariable(InputParameterName);
		if (Var != nullptr && Var->Variable.GetType() == GetInputType() && Var->Metadata.GetIsStaticSwitch() == false)
		{
			UE_LOG(LogNiagaraEditor, Log, TEXT("Static switch constant \"%s\" in \"%s\" didn't have static switch meta-data conversion set properly. Fixing now."), *InputParameterName.ToString(), *GetPathName())
			Var->Metadata.SetIsStaticSwitch(true);
			MarkNodeRequiresSynchronization(TEXT("Static switch metadata updated"), true);
		}
	}
}

UEdGraphPin* UNiagaraNodeStaticSwitch::GetTracedOutputPin(UEdGraphPin* LocallyOwnedOutputPin, bool bFilterForCompilation) const
{
	return GetTracedOutputPin(LocallyOwnedOutputPin, true, bFilterForCompilation);
}

UEdGraphPin* UNiagaraNodeStaticSwitch::GetTracedOutputPin(UEdGraphPin* LocallyOwnedOutputPin, bool bRecursive, bool bFilterForCompilation) const
{
	if (!bFilterForCompilation)
	{
		return LocallyOwnedOutputPin;
	}
	
	FPinCollectorArray InputPins;
	GetInputPins(InputPins);
	
	FPinCollectorArray OutputPins;
	GetOutputPins(OutputPins);

	for (int i = 0; i < OutputPins.Num(); i++)
	{
		UEdGraphPin* OutPin = OutputPins[i];
		if (IsAddPin(OutPin))
		{
			continue;
		}
		int32 VarIdx;
		if (OutPin == LocallyOwnedOutputPin && GetVarIndex(nullptr, InputPins.Num(), SwitchValue, VarIdx))
		{
			UEdGraphPin* InputPin = InputPins[VarIdx + i];
			if (InputPin->LinkedTo.Num() == 1)
			{
				return bRecursive ? UNiagaraNode::TraceOutputPin(InputPin->LinkedTo[0], bFilterForCompilation) : InputPin->LinkedTo[0];
			}
		}
	}
	
	return LocallyOwnedOutputPin;
}

UEdGraphPin* UNiagaraNodeStaticSwitch::GetPassThroughPin(const UEdGraphPin* LocallyOwnedOutputPin,	ENiagaraScriptUsage MasterUsage) const
{
	if (IsValueSet)
	{
		return GetTracedOutputPin(const_cast<UEdGraphPin*>(LocallyOwnedOutputPin), true);
	}
	return Super::GetPassThroughPin(LocallyOwnedOutputPin, MasterUsage);
}

void UNiagaraNodeStaticSwitch::BuildParameterMapHistory(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive /*= true*/, bool bFilterForCompilation /*= true*/) const
{
	UNiagaraNode::BuildParameterMapHistory(OutHistory, bRecursive, bFilterForCompilation);
}

bool UNiagaraNodeStaticSwitch::OnNewPinTypeRequested(UEdGraphPin* PinToChange, FNiagaraTypeDefinition NewType)
{
	auto FindPredicate = [=](const FGuid& Guid) { return Guid == PinToChange->PersistentGuid; };
	int32 FoundIndex = OutputVarGuids.IndexOfByPredicate(FindPredicate);
	if (FoundIndex != INDEX_NONE)
	{
		TSet<FName> OutputNames;
		for (const FNiagaraVariable& Output : OutputVars)
		{
			OutputNames.Add(Output.GetName());
		}
		FName OutputName = FNiagaraUtilities::GetUniqueName(NewType.GetFName(), OutputNames);
		
		OutputVars.RemoveAt(FoundIndex);
		OutputVars.EmplaceAt(FoundIndex, FNiagaraVariable(NewType, OutputName));
		ReallocatePins();
		return true;
	}

	return false;
}

void UNiagaraNodeStaticSwitch::AddWidgetsToOutputBox(TSharedPtr<SVerticalBox> OutputBox)
{
	OutputBox->AddSlot()
	[
		SNew(SSpacer)
	];

	TAttribute<EVisibility> AddVisibilityAttribute;
	TAttribute<EVisibility> RemoveVisibilityAttribute;
	AddVisibilityAttribute.BindUObject(this, &UNiagaraNodeStaticSwitch::ShowAddIntegerButton);
	RemoveVisibilityAttribute.BindUObject(this, &UNiagaraNodeStaticSwitch::ShowRemoveIntegerButton);

	OutputBox->AddSlot()
	.Padding(4.f, 5.f)
	.AutoHeight()
	.HAlign(HAlign_Right)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SButton)
			.Visibility(RemoveVisibilityAttribute)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(GetIntegerRemoveButtonTooltipText())
			.OnPressed(FSimpleDelegate::CreateUObject(this, &UNiagaraNodeStaticSwitch::RemoveIntegerInputPin))
			[
				SNew(SImage)
				.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 0.9f))
				.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Module.RemovePin"))
			]
		]
		+ SHorizontalBox::Slot()
		[
			SNew(SButton)
			.Visibility(AddVisibilityAttribute)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(GetIntegerAddButtonTooltipText())
			.OnPressed(FSimpleDelegate::CreateUObject(this, &UNiagaraNodeStaticSwitch::AddIntegerInputPin))
			[
				SNew(SImage)
				.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 0.9f))
				.Image(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Module.AddPin"))
			]
		]
	];
}

void UNiagaraNodeStaticSwitch::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if(SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		FToolMenuSection& Section = Menu->FindOrAddSection("Node");

		Section.AddMenuEntry(
			"AddEnumAsNiagaraType",
			FText::Format(LOCTEXT("AddEnumToTypesLabel", "Add enum {0} to Niagara type registry"), FText::FromString(SwitchTypeData.Enum->GetName())),
			LOCTEXT("AddEnumToTypesTooltip", "Adds the enum to the list of Niagara types so it can be used as a parameter."), 
			FSlateIcon(), 
			FUIAction(FExecuteAction::CreateLambda([=]()
			{
				GetMutableDefault<UNiagaraSettings>()->AddEnumParameterType(SwitchTypeData.Enum);
			}), FCanExecuteAction::CreateLambda([=]()
			{
				return !GetDefault<UNiagaraSettings>()->AdditionalParameterEnums.Contains(SwitchTypeData.Enum);
			})));
	}
}

FText UNiagaraNodeStaticSwitch::GetTooltipText() const
{
	return LOCTEXT("NiagaraStaticSwitchNodeTooltip", "This is a compile-time switch that selects one branch to compile based on an input parameter.");
}

FText UNiagaraNodeStaticSwitch::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FormatOrdered(LOCTEXT("StaticSwitchTitle", "Static Switch ({0})"), FText::FromName(IsSetByCompiler() ? SwitchTypeData.SwitchConstant : InputParameterName));
}

FLinearColor UNiagaraNodeStaticSwitch::GetNodeTitleColor() const
{
	const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(GetSchema());
	return Schema->NodeTitleColor_Constant;
}

#undef LOCTEXT_NAMESPACE

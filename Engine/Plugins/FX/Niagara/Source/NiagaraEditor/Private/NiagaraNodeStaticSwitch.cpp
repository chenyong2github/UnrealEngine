// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraNodeStaticSwitch.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraHlslTranslator.h"
#include "NiagaraConstants.h"

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

void UNiagaraNodeStaticSwitch::ChangeSwitchParameterName(const FName& NewName)
{
	FNiagaraVariable OldValue(GetInputType(), InputParameterName);
	InputParameterName = NewName;
	VisualsChangedDelegate.Broadcast(this);
	RemoveUnusedGraphParameter(OldValue);	
}

void UNiagaraNodeStaticSwitch::OnSwitchParameterTypeChanged(const FNiagaraTypeDefinition& OldType)
{
	RefreshFromExternalChanges();
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
	IsValueSet = false;

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
		GetNiagaraGraph()->RemoveParameter(OldParameter);
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
		for (FNiagaraVariable& Var : OutputVars)
		{
			FEdGraphPinType PinType = Schema->TypeDefinitionToPinType(Var.GetType());
			CreatePin(EGPD_Input, PinType, *(Var.GetName().ToString() + TEXT(" if true")));
		}
		for (FNiagaraVariable& Var : OutputVars)
		{
			FEdGraphPinType PinType = Schema->TypeDefinitionToPinType(Var.GetType());
			CreatePin(EGPD_Input, PinType, *(Var.GetName().ToString() + TEXT(" if false")));
		}
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		// the int range is inclusive, so we don't skip the last number
		for (int32 i = 0; i <= SwitchTypeData.MaxIntCount; i++)
		{
			for (FNiagaraVariable& Var : OutputVars)
			{
				FEdGraphPinType PinType = Schema->TypeDefinitionToPinType(Var.GetType());
				CreatePin(EGPD_Input, PinType, *(Var.GetName().ToString() + TEXT(" if ") + FString::FromInt(i)));
			}
		}
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		// The last enum value is a special "max" value that we do not want to display, so we skip it
		for (int32 i = 0; i < SwitchTypeData.Enum->NumEnums() - 1; i++)
		{
			for (FNiagaraVariable& Var : OutputVars)
			{
				FEdGraphPinType PinType = Schema->TypeDefinitionToPinType(Var.GetType());
				FText EnumName = SwitchTypeData.Enum->GetDisplayNameTextByIndex(i);
				CreatePin(EGPD_Input, PinType, *(Var.GetName().ToString() + TEXT(" if ") + EnumName.ToString()));
			}
		}
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

void UNiagaraNodeStaticSwitch::InsertInputPinsFor(const FNiagaraVariable& Var)
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

	int32 OptionsCount = 0;
	if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
	{
		OptionsCount = 2;
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
	{
		// +1 because the range is inclusive
		OptionsCount = SwitchTypeData.MaxIntCount + 1;
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		// -1 because the last enum value is a special "max" value
		OptionsCount = SwitchTypeData.Enum->NumEnums() - 1;
	}

	TArray<UEdGraphPin*> OldPins(Pins);
	Pins.Reset(OldPins.Num() + OptionsCount);

	// Create the inputs for each option
	for (int64 i = 0; i < OptionsCount; i++)
	{
		// Add the previous input pins
		for (int32 k = 0; k < OutputVars.Num() - 1; k++)
		{
			Pins.Add(OldPins[k]);
		}
		OldPins.RemoveAt(0, OutputVars.Num() - 1);

		// Add the new input pin		
		FString PathSuffix = TEXT(" if ");
		if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Bool)
		{
			PathSuffix += i == 0 ? TEXT("true") : TEXT("false");
		}
		else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Integer)
		{
			PathSuffix += FString::FromInt(i);
		}
		else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
		{
			FText EnumName = SwitchTypeData.Enum->GetDisplayNameTextByIndex(i);
			PathSuffix += EnumName.ToString();
		}
		CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(Var.GetType()), *(Var.GetName().ToString() + PathSuffix));
	}

	// Move the rest of the old pins over
	Pins.Append(OldPins);
}

bool UNiagaraNodeStaticSwitch::AllowNiagaraTypeForAddPin(const FNiagaraTypeDefinition& InType)
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
	IsValueSet = false;

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
		int32 MaxValue = SwitchTypeData.MaxIntCount;
		if (MaxValue >= 0)
		{
			if (Translator && (Value > MaxValue || Value < 0))
			{
				Translator->Warning(FText::Format(LOCTEXT("InvalidStaticSwitchIntValue", "The supplied int value {0} is outside the bounds for the static switch."), FText::FromString(FString::FromInt(Value))), this, nullptr);
			}
			VarIndexOut = FMath::Clamp(Value, 0, MaxValue) * (InputPinCount / MaxValue);
			Success = true;
		}
		else if (Translator)
		{
			Translator->Error(FText::Format(LOCTEXT("InvalidSwitchMaxIntValue", "Invalid max int value {0} for static switch."), FText::FromString(FString::FromInt(Value))), this, nullptr);
		}
	}
	else if (SwitchTypeData.SwitchType == ENiagaraStaticSwitchType::Enum && SwitchTypeData.Enum)
	{
		int32 MaxValue = SwitchTypeData.Enum->NumEnums() - 1;
		if (MaxValue > 0)
		{
			// do a sanity check here if the number of pins actually matches the enum count (which might have changed in the meantime without us noticing)
			TArray<UEdGraphPin*> LocalOutputPins;
			GetOutputPins(LocalOutputPins);
			int32 OutputPinCount = LocalOutputPins.Num() - 1;
			int32 ReservedValues = (InputPinCount / OutputPinCount);
			if (OutputPinCount > 0 && (MaxValue > ReservedValues || MaxValue < ReservedValues))
			{
				MaxValue = ReservedValues;
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

	TArray<UEdGraphPin*> InputPins;
	GetInputPins(InputPins);
	TArray<UEdGraphPin*> OutputPins;
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
				*LocallyOwnedPin = GetTracedOutputPin(InputPin->LinkedTo[0]);
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

UEdGraphPin* UNiagaraNodeStaticSwitch::GetTracedOutputPin(UEdGraphPin* LocallyOwnedOutputPin) const
{
	return GetTracedOutputPin(LocallyOwnedOutputPin, true);
}

UEdGraphPin* UNiagaraNodeStaticSwitch::GetTracedOutputPin(UEdGraphPin* LocallyOwnedOutputPin, bool bRecursive) const
{
	TArray<UEdGraphPin*> InputPins;
	GetInputPins(InputPins);
	TArray<UEdGraphPin*> OutputPins;
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
				return bRecursive ? UNiagaraNode::TraceOutputPin(InputPin->LinkedTo[0]) : InputPin->LinkedTo[0];
			}
		}
	}
	return LocallyOwnedOutputPin;
}

void UNiagaraNodeStaticSwitch::BuildParameterMapHistory(FNiagaraParameterMapHistoryBuilder& OutHistory, bool bRecursive /*= true*/, bool bFilterForCompilation /*= true*/) const
{
	UNiagaraNode::BuildParameterMapHistory(OutHistory, bRecursive, bFilterForCompilation);
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

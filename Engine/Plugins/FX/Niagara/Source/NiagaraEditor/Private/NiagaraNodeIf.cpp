// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraNodeIf.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraHlslTranslator.h"

#define LOCTEXT_NAMESPACE "NiagaraNodeIf"

const FString UNiagaraNodeIf::InputAPinSuffix(" A");
const FString UNiagaraNodeIf::InputBPinSuffix(" B");
const FName UNiagaraNodeIf::ConditionPinName("Condition");

UNiagaraNodeIf::UNiagaraNodeIf(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UNiagaraNodeIf::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// @TODO why do we need to have this post-change property here at all? 
	// Doing a null check b/c otherwise if doing a Duplicate via Ctrl-W, we die inside AllocateDefaultPins due to 
	// the point where we get this call not being completely formed.
	if (PropertyChangedEvent.Property != nullptr)
	{
		ReallocatePins();
	}
}

void UNiagaraNodeIf::PostLoad()
{
	Super::PostLoad();

	if (PathAssociatedPinGuids.Num() != OutputVars.Num())
	{
		PathAssociatedPinGuids.SetNum(OutputVars.Num());
	}

	auto LoadGuid = [&](FGuid& Guid, const FString& Name, const EEdGraphPinDirection Direction)
	{
		UEdGraphPin* Pin = FindPin(Name, Direction);
		if (Pin)
		{
			if (!Pin->PersistentGuid.IsValid())
			{
				Pin->PersistentGuid = FGuid::NewGuid();
			}
			Guid = Pin->PersistentGuid;
		}
		else
		{
			UE_LOG(LogNiagaraEditor, Error, TEXT("Unable to find output pin named %s"), *Name);
		}
	};
	
	LoadGuid(ConditionPinGuid, ConditionPinName.ToString(), EGPD_Input);
	for (int32 i = 0; i < OutputVars.Num(); i++)
	{
		const FString VarName = OutputVars[i].GetName().ToString();
		LoadGuid(PathAssociatedPinGuids[i].OutputPinGuid, VarName, EGPD_Output);
		const FString InputAName = VarName + InputAPinSuffix;
		LoadGuid(PathAssociatedPinGuids[i].InputAPinGuid, InputAName, EGPD_Input);
		const FString InputBName = VarName + InputBPinSuffix;
		LoadGuid(PathAssociatedPinGuids[i].InputBPinGuid, InputBName, EGPD_Input);
	}
}

bool UNiagaraNodeIf::AllowNiagaraTypeForAddPin(const FNiagaraTypeDefinition& InType)
{
	// Explicitly allow Numeric types, and explicitly disallow ParameterMap types
	
	return (Super::AllowNiagaraTypeForAddPin(InType) || InType == FNiagaraTypeDefinition::GetGenericNumericDef()) && InType != FNiagaraTypeDefinition::GetParameterMapDef();
}

void UNiagaraNodeIf::AllocateDefaultPins()
{
	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

	//Add the condition pin.
	UEdGraphPin* ConditionPin = CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(FNiagaraTypeDefinition::GetBoolDef()), ConditionPinName);
	ConditionPin->PersistentGuid = ConditionPinGuid;

	//Create the inputs for each path.
	for (int32 Index = 0; Index < OutputVars.Num(); Index++)
	{
		const FNiagaraVariable& Var = OutputVars[Index];
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(Var.GetType()), *(Var.GetName().ToString() + InputAPinSuffix));
		NewPin->PersistentGuid = PathAssociatedPinGuids[Index].InputAPinGuid;
	}

	for (int32 Index = 0; Index < OutputVars.Num(); Index++)
	{
		const FNiagaraVariable& Var = OutputVars[Index];
		UEdGraphPin* NewPin = CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(Var.GetType()), *(Var.GetName().ToString() + InputBPinSuffix));
		NewPin->PersistentGuid = PathAssociatedPinGuids[Index].InputBPinGuid;
	}

	for (int32 Index = 0; Index < OutputVars.Num(); Index++)
	{
		const FNiagaraVariable& Var = OutputVars[Index];
		UEdGraphPin* NewPin = CreatePin(EGPD_Output, Schema->TypeDefinitionToPinType(Var.GetType()), Var.GetName());
		NewPin->PersistentGuid = PathAssociatedPinGuids[Index].OutputPinGuid;
	}

	CreateAddPin(EGPD_Output);
}

void UNiagaraNodeIf::Compile(class FHlslNiagaraTranslator* Translator, TArray<int32>& Outputs)
{
	checkSlow(PathAssociatedPinGuids.Num() == OutputVars.Num());
	const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(GetSchema());

	int32 Condition = Translator->CompilePin(GetPinByGuid(ConditionPinGuid));

	TArray<int32> PathA;
	PathA.Reserve(PathAssociatedPinGuids.Num());
	for (const FPinGuidsForPath& PerPathAssociatedPinGuids : PathAssociatedPinGuids)
	{
		const UEdGraphPin* InputAPin = GetPinByGuid(PerPathAssociatedPinGuids.InputAPinGuid);
		if (Schema->PinToTypeDefinition(InputAPin) == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			Translator->Error(LOCTEXT("UnsupportedParamMapInIf", "Parameter maps are not supported in if nodes."), this, InputAPin);
		}
		PathA.Add(Translator->CompilePin(InputAPin));
	}
	TArray<int32> PathB;
	PathB.Reserve(PathAssociatedPinGuids.Num());
	for (const FPinGuidsForPath& PerPathAssociatedPinGuids : PathAssociatedPinGuids)
	{
		const UEdGraphPin* InputBPin = GetPinByGuid(PerPathAssociatedPinGuids.InputBPinGuid);
		if (Schema->PinToTypeDefinition(InputBPin) == FNiagaraTypeDefinition::GetParameterMapDef())
		{
			Translator->Error(LOCTEXT("UnsupportedParamMapInIf", "Parameter maps are not supported in if nodes."), this, InputBPin);
		}
		PathB.Add(Translator->CompilePin(InputBPin));
	}

	Translator->If(this, OutputVars, Condition, PathA, PathB, Outputs);
}

ENiagaraNumericOutputTypeSelectionMode UNiagaraNodeIf::GetNumericOutputTypeSelectionMode() const
{
	return ENiagaraNumericOutputTypeSelectionMode::Largest;
}


void UNiagaraNodeIf::ResolveNumerics(const UEdGraphSchema_Niagara* Schema, bool bSetInline, TMap<TPair<FGuid, UEdGraphNode*>, FNiagaraTypeDefinition>* PinCache)
{
	int32 VarStartIdx = 1;
	for (int32 i = 0; i < OutputVars.Num(); ++i)
	{
		// Fix up numeric input pins and keep track of numeric types to decide the output type.
		TArray<UEdGraphPin*> InputPins;
		TArray<UEdGraphPin*> OutputPins;
		
		InputPins.Add(Pins[i + VarStartIdx]);
		InputPins.Add(Pins[i + VarStartIdx + OutputVars.Num()]);
		OutputPins.Add(Pins[i + VarStartIdx + 2 * OutputVars.Num()]);
		NumericResolutionByPins(Schema, InputPins, OutputPins,  bSetInline, PinCache);
	}
}

bool UNiagaraNodeIf::RefreshFromExternalChanges()
{
	// TODO - Leverage code in reallocate pins to determine if any pins have changed...
	ReallocatePins();
	return true;
}

FGuid UNiagaraNodeIf::AddOutput(FNiagaraTypeDefinition Type, const FName& Name)
{
	FPinGuidsForPath& NewPinGuidsForPath = PathAssociatedPinGuids.Add_GetRef(FPinGuidsForPath());

	FNiagaraVariable NewOutput(Type, Name);
	OutputVars.Add(NewOutput);
	FGuid Guid = FGuid::NewGuid();
	NewPinGuidsForPath.OutputPinGuid = Guid;

	const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(GetSchema());
	FGuid PinAGuid = FGuid::NewGuid();
	UEdGraphPin* PinA = CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(Type), *(Name.ToString() + InputAPinSuffix), PathAssociatedPinGuids.Num());
	PinA->PersistentGuid = PinAGuid;
	NewPinGuidsForPath.InputAPinGuid = PinAGuid;

	FGuid PinBGuid = FGuid::NewGuid();
	UEdGraphPin* PinB = CreatePin(EGPD_Input, Schema->TypeDefinitionToPinType(Type), *(Name.ToString() + InputBPinSuffix), PathAssociatedPinGuids.Num() * 2);
	PinB->PersistentGuid = PinBGuid;
	NewPinGuidsForPath.InputBPinGuid = PinBGuid;

	return Guid;
}

const UEdGraphPin* UNiagaraNodeIf::GetPinByGuid(const FGuid& InGuid)
{
	UEdGraphPin* FoundPin = *Pins.FindByPredicate([&InGuid](const UEdGraphPin* Pin) { return Pin->PersistentGuid == InGuid; });
	checkf(FoundPin != nullptr, TEXT("Failed to get pin by cached Guid!"));
	return FoundPin;
}

void UNiagaraNodeIf::OnPinRemoved(UEdGraphPin* PinToRemove)
{
	auto FindByOutputPinGuidPredicate = [=](const FPinGuidsForPath& PerPinGuidsForPath) { return PerPinGuidsForPath.OutputPinGuid == PinToRemove->PersistentGuid; };
	int32 FoundIndex = PathAssociatedPinGuids.IndexOfByPredicate(FindByOutputPinGuidPredicate);
	if (FoundIndex != INDEX_NONE)
	{
		OutputVars.RemoveAt(FoundIndex);
		PathAssociatedPinGuids.RemoveAt(FoundIndex);
	}
	ReallocatePins();
}

void UNiagaraNodeIf::OnNewTypedPinAdded(UEdGraphPin* NewPin)
{
	Super::OnNewTypedPinAdded(NewPin);

	const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();
	FNiagaraTypeDefinition OutputType = Schema->PinToTypeDefinition(NewPin);

	TSet<FName> OutputNames;
	for (const FNiagaraVariable& Output : OutputVars)
	{
		OutputNames.Add(Output.GetName());
	}
	FName OutputName = FNiagaraUtilities::GetUniqueName(*OutputType.GetNameText().ToString(), OutputNames);

	FGuid Guid = AddOutput(OutputType, OutputName);

	// Update the pin's data too so that it's connection is maintained after reallocating.
	NewPin->PinName = OutputName;
	NewPin->PersistentGuid = Guid;
}

void UNiagaraNodeIf::OnPinRenamed(UEdGraphPin* RenamedPin, const FString& OldName)
{
	auto FindByOutputPinGuidPredicate = [=](const FPinGuidsForPath& PerPinGuidsForPath) { return PerPinGuidsForPath.OutputPinGuid == RenamedPin->PersistentGuid; };
	int32 FoundIndex = PathAssociatedPinGuids.IndexOfByPredicate(FindByOutputPinGuidPredicate);
	if(FoundIndex != INDEX_NONE)
	{
		TSet<FName> OutputNames;
		for (int32 Index = 0; Index < OutputVars.Num(); Index++)
		{
			if (FoundIndex != Index)
			{
				OutputNames.Add(OutputVars[Index].GetName());
			}
		}
		const FName OutputName = FNiagaraUtilities::GetUniqueName(RenamedPin->PinName, OutputNames);
		OutputVars[FoundIndex].SetName(OutputName);
	}
	ReallocatePins();
}

bool UNiagaraNodeIf::CanRenamePin(const UEdGraphPin* Pin) const
{
	return Super::CanRenamePin(Pin) && Pin->Direction == EGPD_Output;
}

bool UNiagaraNodeIf::CanRemovePin(const UEdGraphPin* Pin) const
{
	return Super::CanRemovePin(Pin) && Pin->Direction == EGPD_Output;
}


FText UNiagaraNodeIf::GetTooltipText() const
{
	return LOCTEXT("IfDesc", "If Condition is true, the output value is A, otherwise output B.");
}

FText UNiagaraNodeIf::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("IfTitle", "If");
}

#undef LOCTEXT_NAMESPACE

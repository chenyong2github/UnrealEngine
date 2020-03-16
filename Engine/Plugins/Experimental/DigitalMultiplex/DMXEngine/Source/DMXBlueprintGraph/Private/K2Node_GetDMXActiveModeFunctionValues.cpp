// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_GetDMXActiveModeFunctionValues.h"

#include "Library/DMXEntityFixturePatch.h"
#include "DMXSubsystem.h"
#include "DMXProtocolConstants.h"
#include "DMXBlueprintGraphLog.h"
#include "K2Node_GetDMXFixturePatch.h"

#include "KismetCompiler.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "EdGraphUtilities.h"
#include "K2Node_CallFunction.h"

#define LOCTEXT_NAMESPACE "UK2Node_GetDMXActiveModeFunctionValues"

const FName UK2Node_GetDMXActiveModeFunctionValues::InputDMXFixturePatchPinName(TEXT("InFixturePatch"));
const FName UK2Node_GetDMXActiveModeFunctionValues::InputDMXProtocolPinName(TEXT("InProtocol"));

const FName UK2Node_GetDMXActiveModeFunctionValues::OutputFunctionsMapPinName(TEXT("OutFunctionsMap"));
const FName UK2Node_GetDMXActiveModeFunctionValues::OutputIsSuccessPinName(TEXT("OutIsSuccessPinName"));


UK2Node_GetDMXActiveModeFunctionValues::UK2Node_GetDMXActiveModeFunctionValues()
{
	bIsEditable = true;
	bIsExposed = false;
}

void UK2Node_GetDMXActiveModeFunctionValues::OnFixturePatchChanged()
{
	// Reset fixture path nodes if we receive a notification
	if (Pins.Num() > 0 && bIsExposed)
	{
		ResetFunctions();
	}
}

void UK2Node_GetDMXActiveModeFunctionValues::RemovePinsRecursive(UEdGraphPin* PinToRemove)
{
	for (int32 SubPinIndex = PinToRemove->SubPins.Num() - 1; SubPinIndex >= 0; --SubPinIndex)
	{
		RemovePinsRecursive(PinToRemove->SubPins[SubPinIndex]);
	}

	int32 PinRemovalIndex = INDEX_NONE;
	if (Pins.Find(PinToRemove, PinRemovalIndex))
	{
		Pins.RemoveAt(PinRemovalIndex);
		PinToRemove->MarkPendingKill();
	}
}

void UK2Node_GetDMXActiveModeFunctionValues::RemoveOutputPin(UEdGraphPin* Pin)
{
	checkSlow(Pins.Contains(Pin));

	FScopedTransaction Transaction(LOCTEXT("RemovePinTx", "RemovePin"));
	Modify();

	RemovePinsRecursive(Pin);
	PinConnectionListChanged(Pin);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
}

FText UK2Node_GetDMXActiveModeFunctionValues::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Get DMX Function Values");
}

void UK2Node_GetDMXActiveModeFunctionValues::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Add execution pins
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	// Add input pin
	UEdGraphPin* InputDMXFixturePatchPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UDMXEntityFixturePatch::StaticClass(), InputDMXFixturePatchPinName);
	K2Schema->ConstructBasicPinTooltip(*InputDMXFixturePatchPin, LOCTEXT("InputDMXFixturePatch", "Input DMX Fixture Patch"), InputDMXFixturePatchPin->PinToolTip);
	
	// Protocol Name pin
	UEdGraphPin* InputDMXProtocolPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FDMXProtocolName::StaticStruct(), InputDMXProtocolPinName);
	K2Schema->ConstructBasicPinTooltip(*InputDMXProtocolPin, LOCTEXT("InputDMXProtocolPin", "The DMX protocol name"), InputDMXProtocolPin->PinToolTip);

	// Add output pin
	FCreatePinParams OutputFunctionsMapPinParams;
	OutputFunctionsMapPinParams.ContainerType = EPinContainerType::Map;
	OutputFunctionsMapPinParams.ValueTerminalType.TerminalCategory = UEdGraphSchema_K2::PC_Int;
	UEdGraphPin* OutputFunctionsMapPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Name, TEXT(""), OutputFunctionsMapPinName, OutputFunctionsMapPinParams);
	K2Schema->ConstructBasicPinTooltip(*OutputFunctionsMapPin, LOCTEXT("OutputFunctionsMap", "Output Functions Map."), OutputFunctionsMapPin->PinToolTip);

	UEdGraphPin* OutputIsSuccessPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, TEXT(""), OutputIsSuccessPinName);
	K2Schema->ConstructBasicPinTooltip(*OutputIsSuccessPin, LOCTEXT("OutputIsSuccessPin", "Is SuccessP"), OutputIsSuccessPin->PinToolTip);

	Super::AllocateDefaultPins();
}

void UK2Node_GetDMXActiveModeFunctionValues::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	// First node to execute. GetDMXSubsystem
	FName GetDMXSubsystemFunctionName = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, GetDMXSubsystem_Callable);
	UK2Node_CallFunction* DMXSubsystemNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	DMXSubsystemNode->FunctionReference.SetExternalMember(GetDMXSubsystemFunctionName, UDMXSubsystem::StaticClass());
	DMXSubsystemNode->AllocateDefaultPins();

	UEdGraphPin* DMXSubsystemExecPin = DMXSubsystemNode->GetExecPin();
	UEdGraphPin* DMXSubsystemThenPin = DMXSubsystemNode->GetThenPin();
	UEdGraphPin* DMXSubsystemResult = DMXSubsystemNode->GetReturnValuePin();

	// Hook up inputs
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *DMXSubsystemExecPin);

	// Hook up outputs
	UEdGraphPin* LastThenPin = DMXSubsystemThenPin;
	Schema->TryCreateConnection(LastThenPin, DMXSubsystemThenPin);

	// Second node to execute. GetFunctionsMap
	static const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, GetFunctionsMap);
	UFunction* GetFunctionsMapPointer = FindUField<UFunction>(UDMXSubsystem::StaticClass(), FunctionName);
	check(GetFunctionsMapPointer);

	UK2Node_CallFunction* GetFunctionsMapNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	GetFunctionsMapNode->FunctionReference.SetExternalMember(FunctionName, UDMXSubsystem::StaticClass());
	GetFunctionsMapNode->AllocateDefaultPins();

	// Function pins
	UEdGraphPin* GetFunctionsMapNodeSelfPin = GetFunctionsMapNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
	UEdGraphPin* GetFunctionsMapNodeExecPin = GetFunctionsMapNode->GetExecPin();
	UEdGraphPin* GetFunctionsMapNodeInFixturePatchPin = GetFunctionsMapNode->FindPinChecked(TEXT("InFixturePatch"));
	UEdGraphPin* GetFunctionsMapNodeSelectedProtocolPin = GetFunctionsMapNode->FindPinChecked(TEXT("SelectedProtocol"));
	UEdGraphPin* GetFunctionsMapNodeOutFunctionsMapPin = GetFunctionsMapNode->FindPinChecked(TEXT("OutFunctionsMap"));
	UEdGraphPin* GetFunctionsMapNodeOutIsSuccessPin = GetFunctionsMapNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* GetFunctionsMapNodeThenPin = GetFunctionsMapNode->GetThenPin();

	// Hook up inputs
	Schema->TryCreateConnection(GetFunctionsMapNodeSelfPin, DMXSubsystemResult);
	CompilerContext.MovePinLinksToIntermediate(*GetInputDMXFixturePatchPin(), *GetFunctionsMapNodeInFixturePatchPin);
	CompilerContext.MovePinLinksToIntermediate(*GetInputDMXProtocolPin(), *GetFunctionsMapNodeSelectedProtocolPin);

	// Hook up outputs
	CompilerContext.MovePinLinksToIntermediate(*GetOutputFunctionsMapPin(), *GetFunctionsMapNodeOutFunctionsMapPin);
	CompilerContext.MovePinLinksToIntermediate(*GetOutputIsSuccessPin(), *GetFunctionsMapNodeOutIsSuccessPin);
	Schema->TryCreateConnection(LastThenPin, GetFunctionsMapNodeExecPin);
	LastThenPin = GetFunctionsMapNodeThenPin;

	// Loop GetFunctionsValueName nodes to execute. 
	if (UserDefinedPins.Num() > 0)
	{
		TArray<UEdGraphPin*> IntPairs;
		TArray<UEdGraphPin*> NamePairs;

		// Call functions for dmx function values
		for (const TSharedPtr<FUserPinInfo>& PinInfo : UserDefinedPins)
		{
			UEdGraphPin* Pin = FindPinChecked(PinInfo->PinName);
			{
				if (Pin->Direction == EGPD_Output)
				{
					IntPairs.Add(Pin);
				}
				else if (Pin->Direction == EGPD_Input)
				{
					NamePairs.Add(Pin);
				}
			}
		}

		check(IntPairs.Num() == NamePairs.Num());

		bool bResult = false;
		for (int32 PairIndex = 0; PairIndex < NamePairs.Num(); ++PairIndex)
		{
			const FName GetFunctionsValueName = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, GetFunctionsValue);
			UFunction* GetFunctionsValuePointer = FindUField<UFunction>(UDMXSubsystem::StaticClass(), GetFunctionsValueName);
			check(GetFunctionsValuePointer);

			UK2Node_CallFunction* GetFunctionsValueNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			GetFunctionsValueNode->FunctionReference.SetExternalMember(GetFunctionsValueName, UDMXSubsystem::StaticClass());
			GetFunctionsValueNode->AllocateDefaultPins();

			UEdGraphPin* GetFunctionsValueSelfPin = GetFunctionsValueNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
			UEdGraphPin* GetFunctionsValueExecPin = GetFunctionsValueNode->GetExecPin();
			UEdGraphPin* GetFunctionsValueNodeOutInNamePin = GetFunctionsValueNode->FindPinChecked(TEXT("InName"));
			UEdGraphPin* GetFunctionsValueNodeOutFunctionsMapPin = GetFunctionsValueNode->FindPinChecked(TEXT("InFunctionsMap"));
			UEdGraphPin* GetFunctionsValueNodeOutValuePin = GetFunctionsValueNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
			UEdGraphPin* GetFunctionsValueNodeThenPin = GetFunctionsValueNode->GetThenPin();

			// Input
			Schema->TryCreateConnection(GetFunctionsValueSelfPin, DMXSubsystemResult);
			CompilerContext.MovePinLinksToIntermediate(*NamePairs[PairIndex], *GetFunctionsValueNodeOutInNamePin);

			// Output
			Schema->TryCreateConnection(GetFunctionsValueNodeOutFunctionsMapPin, GetFunctionsMapNodeOutFunctionsMapPin);
			CompilerContext.MovePinLinksToIntermediate(*IntPairs[PairIndex], *GetFunctionsValueNodeOutValuePin);

			// Execution
			Schema->TryCreateConnection(LastThenPin, GetFunctionsValueExecPin);
			LastThenPin = GetFunctionsValueNodeThenPin;
		}

		CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *LastThenPin);
	}
	else
	{
		CompilerContext.MovePinLinksToIntermediate(*GetThenPin(), *LastThenPin);
	}
}

void UK2Node_GetDMXActiveModeFunctionValues::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();

	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_GetDMXActiveModeFunctionValues::GetMenuCategory() const
{
	return FText::FromString(DMX_K2_CATEGORY_NAME);
}

UEdGraphPin* UK2Node_GetDMXActiveModeFunctionValues::CreatePinFromUserDefinition(const TSharedPtr<FUserPinInfo> NewPinInfo)
{
	UEdGraphPin* NewPin = CreatePin(NewPinInfo->DesiredPinDirection, NewPinInfo->PinType, NewPinInfo->PinName);
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->SetPinAutogeneratedDefaultValue(NewPin, NewPinInfo->PinDefaultValue);

	if (NewPinInfo->DesiredPinDirection == EEdGraphPinDirection::EGPD_Input)
	{
		NewPin->bHidden = true;
	}

	return NewPin;
}

bool UK2Node_GetDMXActiveModeFunctionValues::CanCreateUserDefinedPin(const FEdGraphPinType& InPinType, EEdGraphPinDirection InDesiredDirection, FText& OutErrorMessage)
{
	if (!IsEditable())
	{
		return false;
	}

	// Make sure that if this is an exec node we are allowed one.
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	if (InPinType.PinCategory == UEdGraphSchema_K2::PC_Exec && !CanModifyExecutionWires())
	{
		OutErrorMessage = LOCTEXT("MultipleExecPinError", "Cannot support more exec pins!");
		return false;
	}
	else
	{
		TArray<TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>> TypeTree;
		Schema->GetVariableTypeTree(TypeTree, ETypeTreeFilter::RootTypesOnly);

		bool bIsValid = false;
		for (TSharedPtr<UEdGraphSchema_K2::FPinTypeTreeInfo>& TypeInfo : TypeTree)
		{
			FEdGraphPinType CurrentType = TypeInfo->GetPinType(false);
			// only concerned with the list of categories
			if (CurrentType.PinCategory == InPinType.PinCategory)
			{
				bIsValid = true;
				break;
			}
		}

		if (!bIsValid)
		{
			OutErrorMessage = LOCTEXT("AddInputPinError", "Cannot add pins of this type to custom event node!");
			return false;
		}
	}

	return true;
}

bool UK2Node_GetDMXActiveModeFunctionValues::ModifyUserDefinedPinDefaultValue(TSharedPtr<FUserPinInfo> PinInfo, const FString& NewDefaultValue)
{
	if (Super::ModifyUserDefinedPinDefaultValue(PinInfo, NewDefaultValue))
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->HandleParameterDefaultValueChanged(this);

		return true;
	}
	return false;
}

UEdGraphPin* UK2Node_GetDMXActiveModeFunctionValues::GetInputDMXFixturePatchPin() const
{
	UEdGraphPin* Pin = FindPinChecked(InputDMXFixturePatchPinName);
	check(Pin->Direction == EGPD_Input);

	return Pin;
}

UEdGraphPin* UK2Node_GetDMXActiveModeFunctionValues::GetInputDMXProtocolPin() const
{
	UEdGraphPin* Pin = FindPinChecked(InputDMXProtocolPinName);
	check(Pin->Direction == EGPD_Input);

	return Pin;
}

UEdGraphPin* UK2Node_GetDMXActiveModeFunctionValues::GetOutputFunctionsMapPin() const
{
	UEdGraphPin* Pin = FindPinChecked(OutputFunctionsMapPinName);
	check(Pin->Direction == EGPD_Output);

	return Pin;
}

UEdGraphPin* UK2Node_GetDMXActiveModeFunctionValues::GetOutputIsSuccessPin() const
{
	UEdGraphPin* Pin = FindPinChecked(OutputIsSuccessPinName);
	check(Pin->Direction == EGPD_Output);

	return Pin;
}

UEdGraphPin* UK2Node_GetDMXActiveModeFunctionValues::GetThenPin() const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked(UEdGraphSchema_K2::PN_Then);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

void UK2Node_GetDMXActiveModeFunctionValues::ExposeFunctions()
{
	if (bIsExposed == true && UserDefinedPins.Num())
	{
		return;
	}

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	if (const FDMXFixtureMode* ActiveFixtureMode = GetActiveFixtureMode())
	{
		for (const FDMXFixtureFunction& Function : ActiveFixtureMode->Functions)
		{
			{
				FEdGraphPinType PinType;
				PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
				UEdGraphPin* Pin = CreateUserDefinedPin(GetPinName(Function), PinType, EGPD_Output);
			}

			{
				FEdGraphPinType PinType;
				PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
				UEdGraphPin* Pin = CreateUserDefinedPin(*(GetPinName(Function).ToString() + FString("_Input")), PinType, EGPD_Input);
				K2Schema->TrySetDefaultValue(*Pin, Function.FunctionName);
			}

			const bool bIsCompiling = GetBlueprint()->bBeingCompiled;
			if (!bIsCompiling)
			{
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
			}
		}
	}
	else
	{
		UE_LOG_DMXBLUEPRINTGRAPH(Verbose, TEXT("No active mode found"));
		return;
	}


	Modify();
	bIsExposed = true;
}

void UK2Node_GetDMXActiveModeFunctionValues::ResetFunctions()
{
	if (bIsExposed)
	{
		while (UserDefinedPins.Num())
		{
			TSharedPtr<FUserPinInfo> Pin = UserDefinedPins[0];
			RemoveUserDefinedPin(Pin);
		}

		// Reconstruct the entry/exit definition and recompile the blueprint to make sure the signature has changed before any fixups
		bDisableOrphanPinSaving = true;

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->HandleParameterDefaultValueChanged(this);
	}


	bIsExposed = false;
}

UDMXEntityFixturePatch* UK2Node_GetDMXActiveModeFunctionValues::GetFixturePatchFromPin() const
{
	UEdGraphPin* FixturePatchPin = GetInputDMXFixturePatchPin();

	// Case with default object
	if (FixturePatchPin->DefaultObject != nullptr && FixturePatchPin->LinkedTo.Num() == 0)
	{
		return Cast<UDMXEntityFixturePatch>(FixturePatchPin->DefaultObject);
	}

	// Case with linked object
	if (FixturePatchPin->LinkedTo.Num() > 0)
	{
		if (UK2Node_GetDMXFixturePatch* NodeGetFixturePatch = Cast<UK2Node_GetDMXFixturePatch>(FixturePatchPin->LinkedTo[0]->GetOwningNode()))
		{
			return NodeGetFixturePatch->GetFixturePatchRefFromPin().GetFixturePatch();
		}
	}

	return nullptr;
}

FName UK2Node_GetDMXActiveModeFunctionValues::GetPinName(const FDMXFixtureFunction& Function)
{
	FString EnumString;
	EnumString = StaticEnum<EDMXFixtureSignalFormat>()->GetDisplayNameTextByIndex((int64)Function.DataType).ToString();

	return *FString::Printf(TEXT("%s_%s"), *Function.FunctionName, *EnumString);
}

const FDMXFixtureMode* UK2Node_GetDMXActiveModeFunctionValues::GetActiveFixtureMode() const
{
	if (UDMXEntityFixturePatch* FixturePatch = GetFixturePatchFromPin())
	{
		if (UDMXEntityFixtureType* FixtureType = FixturePatch->ParentFixtureTypeTemplate)
		{
			if (FixtureType->Modes.Num() && FixtureType->Modes.Num() >= FixturePatch->ActiveMode)
			{
				return &FixtureType->Modes[FixturePatch->ActiveMode];
			}
		}
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE

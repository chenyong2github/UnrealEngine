// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_GetDMXFixturePatch.h"
#include "K2Node_GetDMXActiveModeFunctionValues.h"
#include "Library/DMXEntityFixturePatch.h"
#include "DMXSubsystem.h"
#include "DMXProtocolConstants.h"

#include "KismetCompilerMisc.h"
#include "KismetCompiler.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "UK2Node_GetDMXFixturePatch"

const FName UK2Node_GetDMXFixturePatch::InputDMXFixturePatchPinName(TEXT("InFixturePatch"));
const FName UK2Node_GetDMXFixturePatch::OutputDMXFixturePatchPinName(TEXT("OutFixturePatch"));

void UK2Node_GetDMXFixturePatch::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Input pins
	UEdGraphPin* InputDMXFixturePatchPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FDMXEntityFixturePatchRef::StaticStruct(), InputDMXFixturePatchPinName);
	K2Schema->ConstructBasicPinTooltip(*InputDMXFixturePatchPin, LOCTEXT("InputDMXFixtureTypePin", "Get the fixture patch reference."), InputDMXFixturePatchPin->PinToolTip);
	InputDMXFixturePatchPin->bNotConnectable = true;

	// Output pins
	UEdGraphPin* OutputDMXFixturePatchPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, UDMXEntityFixturePatch::StaticClass(), OutputDMXFixturePatchPinName);
	K2Schema->ConstructBasicPinTooltip(*OutputDMXFixturePatchPin, LOCTEXT("OutputDMXFixturePatch", "Fixture patch."), OutputDMXFixturePatchPin->PinToolTip);
	OutputDMXFixturePatchPin->PinType.bIsReference = true;

	Super::AllocateDefaultPins();
}

FText UK2Node_GetDMXFixturePatch::GetTooltipText() const
{
	return LOCTEXT("TooltipText", "Get selected Fixture Patch");
}

FText UK2Node_GetDMXFixturePatch::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Get DMX Fixture Patch");
}

void UK2Node_GetDMXFixturePatch::PinDefaultValueChanged(UEdGraphPin* FromPin)
{
	Super::PinDefaultValueChanged(FromPin);

	if (!FromPin)
	{
		return; 
	}

	if (FromPin->PinName == InputDMXFixturePatchPinName)
	{
		NotifyInputChanged();
	}
}

void UK2Node_GetDMXFixturePatch::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();

	UDMXSubsystem* Subsystem = GEngine->GetEngineSubsystem<UDMXSubsystem>();

	UEdGraphPin* SelfPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UDMXSubsystem::StaticClass(), UEdGraphSchema_K2::PN_Self);
	SelfPin->DefaultObject = Subsystem;

	// Function to call
	const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, GetFixturePatch);
	const UFunction* Function = UDMXSubsystem::StaticClass()->FindFunctionByName(FunctionName);
	check(nullptr != Function);

	// Spawn call function node
	UK2Node_CallFunction* SendDataGetFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	SendDataGetFunction->FunctionReference.SetExternalMember(FunctionName, UK2Node_CallFunction::StaticClass());

	// Set function node pins
	SendDataGetFunction->SetFromFunction(Function);
	SendDataGetFunction->AllocateDefaultPins();

	// Hook up function node inputs
	UEdGraphPin* FunctionInFixturePatchPin = SendDataGetFunction->FindPinChecked(TEXT("InFixturePatch"));
	UEdGraphPin* FunctionOutFixturePatchPin = SendDataGetFunction->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	UEdGraphPin* FunctionSelfPin = SendDataGetFunction->FindPinChecked(UEdGraphSchema_K2::PN_Self);

	const FString&& FixturePatchStr = GetFixturePatchValueAsString();

	// Hook up input
	CompilerContext.MovePinLinksToIntermediate(*SelfPin, *FunctionSelfPin);
	K2Schema->TrySetDefaultValue(*FunctionInFixturePatchPin, FixturePatchStr);
	check(FunctionInFixturePatchPin->GetDefaultAsString().Equals(FixturePatchStr));

	// Hook up outputs
	CompilerContext.MovePinLinksToIntermediate(*GetOutputDMXFixturePatchPin(), *FunctionOutFixturePatchPin);
}

void UK2Node_GetDMXFixturePatch::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_GetDMXFixturePatch::GetMenuCategory() const
{
	return FText::FromString(DMX_K2_CATEGORY_NAME);
}

UEdGraphPin* UK2Node_GetDMXFixturePatch::GetInputDMXFixturePatchPin() const
{
	UEdGraphPin* Pin = FindPinChecked(InputDMXFixturePatchPinName);
	check(Pin->Direction == EGPD_Input);

	return Pin;
}

UEdGraphPin* UK2Node_GetDMXFixturePatch::GetOutputDMXFixturePatchPin() const
{
	UEdGraphPin* Pin = FindPinChecked(OutputDMXFixturePatchPinName);
	check(Pin->Direction == EGPD_Output);

	return Pin;
}

FString UK2Node_GetDMXFixturePatch::GetFixturePatchValueAsString() const
{
	UEdGraphPin* FixturePatchPin = GetInputDMXFixturePatchPin();

	FString PatchRefString;

	// Case with default object
	if (FixturePatchPin->LinkedTo.Num() == 0)
	{
		PatchRefString = FixturePatchPin->GetDefaultAsString();
	}
	// Case with linked object
	else
	{
		PatchRefString = FixturePatchPin->LinkedTo[0]->GetDefaultAsString();
	}

	return PatchRefString;
}

FDMXEntityFixturePatchRef UK2Node_GetDMXFixturePatch::GetFixturePatchRefFromPin() const
{
	FDMXEntityFixturePatchRef PatchRef;

	const FString&& PatchRefString = GetFixturePatchValueAsString();
	if (!PatchRefString.IsEmpty())
	{
		FDMXEntityReference::StaticStruct()->ImportText(*PatchRefString, &PatchRef, nullptr, EPropertyPortFlags::PPF_None, GLog, FDMXEntityReference::StaticStruct()->GetName());
	}

	return PatchRef;
}

void UK2Node_GetDMXFixturePatch::SetInFixturePatchPinValue(const FDMXEntityFixturePatchRef& InPatchRef) const
{
	FString ValueString;
	FDMXEntityReference::StaticStruct()->ExportText(ValueString, &InPatchRef, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr);
	UEdGraphPin* FixturePatchPin = GetInputDMXFixturePatchPin();
	FixturePatchPin->GetSchema()->TrySetDefaultValue(*FixturePatchPin, ValueString);
}

void UK2Node_GetDMXFixturePatch::NotifyInputChanged()
{
	// Notify all GetDMXActiveModeFunctionValues nodes connected to our output pin
	const TArray<UEdGraphPin*>& OutputConnections = GetOutputDMXFixturePatchPin()->LinkedTo;
	for (const UEdGraphPin* ConnectedPin : OutputConnections)
	{
		if (UK2Node_GetDMXActiveModeFunctionValues* ModeFunctionsNode = Cast<UK2Node_GetDMXActiveModeFunctionValues>(ConnectedPin->GetOwningNode()))
		{
			ModeFunctionsNode->OnFixturePatchChanged();
		}
	}

	if (UBlueprint* BP = GetBlueprint())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}

	UEdGraph* Graph = GetGraph();
	Graph->NotifyGraphChanged();
}

#undef LOCTEXT_NAMESPACE

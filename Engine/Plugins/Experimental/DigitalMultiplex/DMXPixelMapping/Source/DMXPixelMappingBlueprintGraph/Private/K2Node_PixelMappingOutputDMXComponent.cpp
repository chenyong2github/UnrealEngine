// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_PixelMappingOutputDMXComponent.h"
#include "DMXPixelMapping.h"
#include "Components/DMXPixelMappingOutputDMXComponent.h"
#include "Blueprint/DMXPixelMappingSubsystem.h"

#include "KismetCompiler.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "K2Node_CallFunction.h"

#define LOCTEXT_NAMESPACE "UK2Node_PixelMappingOutputDMXComponent"

const FName UK2Node_PixelMappingOutputDMXComponent::InOutputDMXComponentPinName(TEXT("In Component"));
const FName UK2Node_PixelMappingOutputDMXComponent::OutOutputDMXComponentPinName(TEXT("Out Component"));

FText UK2Node_PixelMappingOutputDMXComponent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Get DMX Pixel Mapping Output Component");
}

void UK2Node_PixelMappingOutputDMXComponent::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Allocate Parent pins
	Super::AllocateDefaultPins();

	// Input pins
	UEdGraphPin* InOutputDMXComponentPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, InOutputDMXComponentPinName);
	K2Schema->ConstructBasicPinTooltip(*InOutputDMXComponentPin, LOCTEXT("InOutputDMXComponentPin", "Input for Output Component"), InOutputDMXComponentPin->PinToolTip);

	// Add output pin
	UEdGraphPin* OutOutputDMXComponentPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, UDMXPixelMappingOutputDMXComponent::StaticClass(), OutOutputDMXComponentPinName);
	K2Schema->ConstructBasicPinTooltip(*OutOutputDMXComponentPin, LOCTEXT("OutOutputDMXComponentPin", "Output for Output Component"), OutOutputDMXComponentPin->PinToolTip);
}

void UK2Node_PixelMappingOutputDMXComponent::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	Super::PinDefaultValueChanged(ChangedPin);

	TryRefreshGraphCheckInputPins(ChangedPin, GetOutOutputDMXComponentPin());
}

void UK2Node_PixelMappingOutputDMXComponent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	ExecuteExpandNode(CompilerContext, SourceGraph, GET_FUNCTION_NAME_CHECKED(UDMXPixelMappingSubsystem, GetOutputDMXComponent), GetInOutputDMXComponentPin(), GetOutOutputDMXComponentPin());
}

void UK2Node_PixelMappingOutputDMXComponent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	AddBlueprintAction(GetClass(), ActionRegistrar);
}

UEdGraphPin* UK2Node_PixelMappingOutputDMXComponent::GetInOutputDMXComponentPin() const
{
	UEdGraphPin* Pin = FindPinChecked(InOutputDMXComponentPinName);
	check(Pin->Direction == EGPD_Input);

	return Pin;
}

UEdGraphPin* UK2Node_PixelMappingOutputDMXComponent::GetOutOutputDMXComponentPin() const
{
	UEdGraphPin* Pin = FindPinChecked(OutOutputDMXComponentPinName);
	check(Pin->Direction == EGPD_Output);

	return Pin;
}

void UK2Node_PixelMappingOutputDMXComponent::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);

	ExecuteEarlyValidation(MessageLog, GetInOutputDMXComponentPin());
}

void UK2Node_PixelMappingOutputDMXComponent::OnPixelMappingChanged(UDMXPixelMapping* InDMXPixelMapping)
{
	TryModifyBlueprintOnNameChanged(InDMXPixelMapping, GetInOutputDMXComponentPin());
}

#undef LOCTEXT_NAMESPACE
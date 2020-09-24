// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_PixelMappingMatrixComponent.h"
#include "DMXPixelMapping.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Blueprint/DMXPixelMappingSubsystem.h"

#include "KismetCompiler.h"
#include "BlueprintActionDatabaseRegistrar.h"

#define LOCTEXT_NAMESPACE "UK2Node_PixelMappingMatrixComponent"

const FName UK2Node_PixelMappingMatrixComponent::InMatrixComponentPinName(TEXT("In Component"));
const FName UK2Node_PixelMappingMatrixComponent::OutMatrixComponentPinName(TEXT("Out Component"));

FText UK2Node_PixelMappingMatrixComponent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Get DMX Pixel Mapping Matrix Component");
}

void UK2Node_PixelMappingMatrixComponent::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Allocate Parent pins
	Super::AllocateDefaultPins();

	// Input pins
	UEdGraphPin* InMatrixComponentPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, InMatrixComponentPinName);
	K2Schema->ConstructBasicPinTooltip(*InMatrixComponentPin, LOCTEXT("InMatrixComponentPin", "Input for Matrix Component"), InMatrixComponentPin->PinToolTip);

	// Add output pin
	UEdGraphPin* OutputMatrixComponentPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, UDMXPixelMappingMatrixComponent::StaticClass(), OutMatrixComponentPinName);
	K2Schema->ConstructBasicPinTooltip(*OutputMatrixComponentPin, LOCTEXT("OutputMatrixComponentPin", "Matrix Component"), OutputMatrixComponentPin->PinToolTip);
}

void UK2Node_PixelMappingMatrixComponent::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	Super::PinDefaultValueChanged(ChangedPin);

	TryRefreshGraphCheckInputPins(ChangedPin, GetOutMatrixComponentPin());
}

void UK2Node_PixelMappingMatrixComponent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	ExecuteExpandNode(CompilerContext, SourceGraph, GET_FUNCTION_NAME_CHECKED(UDMXPixelMappingSubsystem, GetMatrixComponent), GetInMatrixComponentPin(), GetOutMatrixComponentPin());
}

void UK2Node_PixelMappingMatrixComponent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	AddBlueprintAction(GetClass(), ActionRegistrar);
}

UEdGraphPin* UK2Node_PixelMappingMatrixComponent::GetInMatrixComponentPin() const
{
	UEdGraphPin* Pin = FindPinChecked(InMatrixComponentPinName);
	check(Pin->Direction == EGPD_Input);

	return Pin;
}

UEdGraphPin* UK2Node_PixelMappingMatrixComponent::GetOutMatrixComponentPin() const
{
	UEdGraphPin* Pin = FindPinChecked(OutMatrixComponentPinName);
	check(Pin->Direction == EGPD_Output);

	return Pin;
}

void UK2Node_PixelMappingMatrixComponent::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);

	ExecuteEarlyValidation(MessageLog, GetInMatrixComponentPin());
}

void UK2Node_PixelMappingMatrixComponent::OnPixelMappingChanged(UDMXPixelMapping* InDMXPixelMapping)
{
	TryModifyBlueprintOnNameChanged(InDMXPixelMapping, GetInMatrixComponentPin());
}

#undef LOCTEXT_NAMESPACE

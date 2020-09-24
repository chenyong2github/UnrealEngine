// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_PixelMappingFixtureGroupComponent.h"
#include "DMXPixelMapping.h"
#include "Components/DMXPixelMappingFixtureGroupComponent.h"
#include "Blueprint/DMXPixelMappingSubsystem.h"

#include "KismetCompiler.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "K2Node_CallFunction.h"

#define LOCTEXT_NAMESPACE "UK2Node_PixelMappingFixtureGroupComponent"

const FName UK2Node_PixelMappingFixtureGroupComponent::InFixtureGroupComponentPinName(TEXT("In Component"));
const FName UK2Node_PixelMappingFixtureGroupComponent::OutFixtureGroupComponentPinName(TEXT("Out Component"));

FText UK2Node_PixelMappingFixtureGroupComponent::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Get DMX Pixel Mapping Fixture Group Component");
}

void UK2Node_PixelMappingFixtureGroupComponent::AllocateDefaultPins()
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	// Allocate Parent pins
	Super::AllocateDefaultPins();

	// Input pins
	UEdGraphPin* InFixtureGroupComponentPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, InFixtureGroupComponentPinName);
	K2Schema->ConstructBasicPinTooltip(*InFixtureGroupComponentPin, LOCTEXT("InFixtureGroupComponentPin", "Input for Fixture Group Component"), InFixtureGroupComponentPin->PinToolTip);

	// Add output pin
	UEdGraphPin* OutputFixtureGroupComponentPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, UDMXPixelMappingFixtureGroupComponent::StaticClass(), OutFixtureGroupComponentPinName);
	K2Schema->ConstructBasicPinTooltip(*OutputFixtureGroupComponentPin, LOCTEXT("OutputFixtureGroupComponentPin", "Fixture Group Component"), OutputFixtureGroupComponentPin->PinToolTip);
}

void UK2Node_PixelMappingFixtureGroupComponent::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	Super::PinDefaultValueChanged(ChangedPin);

	TryRefreshGraphCheckInputPins(ChangedPin, GetOutFixtureGroupComponentPin());
}

void UK2Node_PixelMappingFixtureGroupComponent::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	ExecuteExpandNode(CompilerContext, SourceGraph, GET_FUNCTION_NAME_CHECKED(UDMXPixelMappingSubsystem, GetFixtureGroupComponent), GetInFixtureGroupComponentPin(), GetOutFixtureGroupComponentPin());
}

void UK2Node_PixelMappingFixtureGroupComponent::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	AddBlueprintAction(GetClass(), ActionRegistrar);
}

UEdGraphPin* UK2Node_PixelMappingFixtureGroupComponent::GetInFixtureGroupComponentPin() const
{
	UEdGraphPin* Pin = FindPinChecked(InFixtureGroupComponentPinName);
	check(Pin->Direction == EGPD_Input);

	return Pin;
}

UEdGraphPin* UK2Node_PixelMappingFixtureGroupComponent::GetOutFixtureGroupComponentPin() const
{
	UEdGraphPin* Pin = FindPinChecked(OutFixtureGroupComponentPinName);
	check(Pin->Direction == EGPD_Output);

	return Pin;
}

void UK2Node_PixelMappingFixtureGroupComponent::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);

	ExecuteEarlyValidation(MessageLog, GetInFixtureGroupComponentPin());
}

void UK2Node_PixelMappingFixtureGroupComponent::OnPixelMappingChanged(UDMXPixelMapping* InDMXPixelMapping)
{
	TryModifyBlueprintOnNameChanged(InDMXPixelMapping, GetInFixtureGroupComponentPin());
}

#undef LOCTEXT_NAMESPACE

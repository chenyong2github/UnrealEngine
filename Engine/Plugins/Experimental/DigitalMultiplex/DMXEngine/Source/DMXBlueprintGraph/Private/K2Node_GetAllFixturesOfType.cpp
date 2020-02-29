// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_GetAllFixturesOfType.h"
#include "DMXSubsystem.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "DMXSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "KismetCompiler.h"
#include "EditorCategoryUtils.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "K2Node_GetAllFixturesOfType"

FName FK2Node_GetAllFixturesOfType::FixtureTypePinName(TEXT("FixtureType"));
FName FK2Node_GetAllFixturesOfType::OutResultPinName(TEXT("Fixtures"));

UK2Node_GetAllFixturesOfType::UK2Node_GetAllFixturesOfType()
{
	NodeTooltip = LOCTEXT("NodeTooltip", "Returns all fixtures of selected type");
}

void UK2Node_GetAllFixturesOfType::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	UEdGraphPin* ThenPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	Super::AllocateDefaultPins();

	UEdGraphPin* FixtureTypePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, FK2Node_GetAllFixturesOfType::FixtureTypePinName);

	FCreatePinParams PinParams = FCreatePinParams();
	PinParams.ContainerType = EPinContainerType::Array;
	UEdGraphPin* OutResultPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, UDMXEntityFixturePatch::StaticClass() ,FK2Node_GetAllFixturesOfType::OutResultPinName, PinParams);
}

FText UK2Node_GetAllFixturesOfType::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("BaseTitle", "Get All Fixtures Of Type");
}

void UK2Node_GetAllFixturesOfType::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	UDMXSubsystem* Subsystem = GEngine->GetEngineSubsystem<UDMXSubsystem>();

	const FName FunctionName = GET_FUNCTION_NAME_CHECKED(UDMXSubsystem, GetAllFixturesOfType);
	static const FName ClassName(TEXT("DMXLibrary"));
	static const FName FixtureTypeName(TEXT("FixtureType"));
	static const FName OutResultName(TEXT("OutResult"));
	
	UEdGraphPin* SelfPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UDMXSubsystem::StaticClass(), UEdGraphSchema_K2::PN_Self);
	SelfPin->DefaultObject = Subsystem;

	UK2Node_GetAllFixturesOfType* GetNode = this;
	UEdGraphPin* ClassPin = GetNode->GetClassPin();
	UEdGraphPin* FixtureTypePin = GetNode->FindPin(FK2Node_GetAllFixturesOfType::FixtureTypePinName);
	UEdGraphPin* SendOutResult = GetNode->FindPin(FK2Node_GetAllFixturesOfType::OutResultPinName);
	UEdGraphPin* ThenPin = GetNode->FindPin(UEdGraphSchema_K2::PN_Then);

	UK2Node_CallFunction* GetAllFixturesNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(GetNode, SourceGraph);
	GetAllFixturesNode->FunctionReference.SetExternalMember(FunctionName, UDMXSubsystem::StaticClass());
	GetAllFixturesNode->AllocateDefaultPins();

	UEdGraphPin* CallClass = GetAllFixturesNode->FindPinChecked(ClassName);
	UEdGraphPin* CallFixtureType = GetAllFixturesNode->FindPinChecked(FixtureTypeName);
	UEdGraphPin* CallOutResult = GetAllFixturesNode->FindPinChecked(OutResultName);
	UEdGraphPin* CallThenPin = GetAllFixturesNode->GetThenPin();

	CompilerContext.MovePinLinksToIntermediate(*SelfPin, *GetAllFixturesNode->FindPin(UEdGraphSchema_K2::PN_Self));
	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *GetAllFixturesNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*ClassPin, *CallClass);
	CompilerContext.MovePinLinksToIntermediate(*FixtureTypePin, *CallFixtureType);
	CallOutResult->PinType = SendOutResult->PinType;
	CompilerContext.MovePinLinksToIntermediate(*SendOutResult, *CallOutResult);
	CompilerContext.MovePinLinksToIntermediate(*ThenPin, *CallThenPin);
}

void UK2Node_GetAllFixturesOfType::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	AllocateDefaultPins();

	RestoreSplitPins(OldPins);
}

#undef LOCTEXT_NAMESPACE

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdGraph/EdGraphSchema.h"
#include "Templates/SubclassOf.h"

#include "PCGEditorGraphSchemaActions.generated.h"

class UEdGraph;
class UEdGraphPin;
class UPCGEditorGraphNode;
class UPCGSettings;

USTRUCT()
struct FPCGEditorGraphSchemaAction_NewNode : public FEdGraphSchemaAction
{
	GENERATED_BODY()

	// Inherit the base class's constructors
	using FEdGraphSchemaAction::FEdGraphSchemaAction;

	UPROPERTY()
	TSubclassOf<UPCGSettings> SettingsClass;

	virtual UEdGraphNode* PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;
};

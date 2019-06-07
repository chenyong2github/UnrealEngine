// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "K2Node_DataprepConsumer.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"

#define LOCTEXT_NAMESPACE "UK2Node_DataprepConsumer"

void UK2Node_DataprepConsumer::AllocateDefaultPins()
{
	// The execute pin
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	Super::AllocateDefaultPins();
}

void UK2Node_DataprepConsumer::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

FText UK2Node_DataprepConsumer::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("NodeTitle", "Finish");
}

FText UK2Node_DataprepConsumer::GetTooltipText() const
{
	return LOCTEXT("NodeTooltip", "Hold onto all the consumer associated to a Dataprep asset");
}

bool UK2Node_DataprepConsumer::CanDuplicateNode() const
{
	return false;
}

bool UK2Node_DataprepConsumer::CanUserDeleteNode() const
{
	return false;
}

void UK2Node_DataprepConsumer::SetDataprepAsset( UDataprepAsset* InDataprepAsset )
{
	DataprepAsset = InDataprepAsset;
	DataprepAssetPath = FSoftObjectPath( InDataprepAsset );
}

void UK2Node_DataprepConsumer::Serialize(FArchive & Ar)
{
	Super::Serialize( Ar );
}

#undef LOCTEXT_NAMESPACE

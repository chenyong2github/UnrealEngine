// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraphSchema.h"

#include "PCGEditorGraph.h"
#include "PCGEditorGraphNode.h"
#include "PCGEditorGraphSchemaActions.h"
#include "PCGSettings.h"

#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "PCGEditorGraphSchema"

void UPCGEditorGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	Super::GetGraphContextActions(ContextMenuBuilder);

	const UPCGEditorGraph* PCGEditorGraph = CastChecked<UPCGEditorGraph>(ContextMenuBuilder.CurrentGraph);

	TArray<UClass*> SettingsClasses;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		
		if (Class->IsChildOf(UPCGSettings::StaticClass()) &&
			!Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_Hidden))
		{
			SettingsClasses.Add(Class);
		}
	}

	for (UClass* SettingsClass : SettingsClasses)
	{
		const FText MenuDesc = FText::FromName(SettingsClass->GetFName());

		TSharedPtr<FPCGEditorGraphSchemaAction_NewNode> NewAction(new FPCGEditorGraphSchemaAction_NewNode(FText::GetEmpty(), MenuDesc, FText::GetEmpty(), 0));
		NewAction->SettingsClass = SettingsClass;
		ContextMenuBuilder.AddAction(NewAction);
	}
}

FLinearColor UPCGEditorGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);
}

#undef LOCTEXT_NAMESPACE

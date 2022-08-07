// Copyright Epic Games, Inc. All Rights Reserved.

#include "Blueprints/RenderPagesBlueprint.h"

#include "EdGraphSchema_K2_Actions.h"
#include "Graph/RenderPagesGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPagesBlueprintGeneratedClass.h"


UClass* URenderPagesBlueprint::GetBlueprintClass() const
{
	return URenderPagesBlueprintGeneratedClass::StaticClass();
}

void URenderPagesBlueprint::PostLoad()
{
	Super::PostLoad();

	{// remove every URenderPagesGraph (because that class is deprecated) >>
		bool bChanged = false;
		TArray<TObjectPtr<UEdGraph>> NewUberGraphPages;

		for (const TObjectPtr<UEdGraph>& Graph : UbergraphPages)
		{
			if (UDEPRECATED_RenderPagesGraph* RenderPagesGraph = Cast<UDEPRECATED_RenderPagesGraph>(Graph))
			{
				bChanged = true;
				RenderPagesGraph->MarkAsGarbage();
				RenderPagesGraph->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders);
				continue;
			}
			NewUberGraphPages.Add(Graph);
		}

		if (bChanged)
		{
			UbergraphPages = NewUberGraphPages;
		}
	}// remove every URenderPagesGraph (because that class is deprecated) <<

	if (UbergraphPages.IsEmpty())
	{
		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(this, UEdGraphSchema_K2::GN_EventGraph, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		NewGraph->bAllowDeletion = false;

		{// create every RenderPages blueprint event >>
			int32 i = 0;
			for (const FString& Event : URenderPageCollection::GetBlueprintImplementableEvents())
			{
				int32 InOutNodePosY = (i * 256) - 48;
				FKismetEditorUtilities::AddDefaultEventNode(this, NewGraph, FName(Event), URenderPageCollection::StaticClass(), InOutNodePosY);
				i++;
			}
		}// create every RenderPages blueprint event <<

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
		FBlueprintEditorUtils::AddUbergraphPage(this, NewGraph);
		LastEditedDocuments.AddUnique(NewGraph);
	}

	FCoreUObjectDelegates::OnObjectModified.RemoveAll(this);
	OnChanged().RemoveAll(this);
	FCoreUObjectDelegates::OnObjectModified.AddUObject(this, &URenderPagesBlueprint::OnPreVariablesChange);
	OnChanged().AddUObject(this, &URenderPagesBlueprint::OnPostVariablesChange);

	OnPreVariablesChange(this);
	OnPostVariablesChange(this);
}

void URenderPagesBlueprint::OnPreVariablesChange(UObject* InObject)
{
	if (InObject != this)
	{
		return;
	}
	LastNewVariables = NewVariables;
}

void URenderPagesBlueprint::OnPostVariablesChange(UBlueprint* InBlueprint)
{
	if (InBlueprint != this)
	{
		return;
	}

	bool bFoundChange = false;

	TMap<FGuid, int32> NewVariablesByGuid;
	for (int32 VarIndex = 0; VarIndex < NewVariables.Num(); VarIndex++)
	{
		NewVariablesByGuid.Add(NewVariables[VarIndex].VarGuid, VarIndex);
	}

	TMap<FGuid, int32> OldVariablesByGuid;
	for (int32 VarIndex = 0; VarIndex < LastNewVariables.Num(); VarIndex++)
	{
		OldVariablesByGuid.Add(LastNewVariables[VarIndex].VarGuid, VarIndex);
	}

	for (FBPVariableDescription& OldVariable : LastNewVariables)
	{
		if (!NewVariablesByGuid.Contains(OldVariable.VarGuid))
		{
			bFoundChange = true;
			OnVariableRemoved(OldVariable);
		}
	}

	for (FBPVariableDescription& NewVariable : NewVariables)
	{
		if (!OldVariablesByGuid.Contains(NewVariable.VarGuid))
		{
			bFoundChange = true;
			OnVariableAdded(NewVariable);
			continue;
		}

		int32 OldVarIndex = OldVariablesByGuid.FindChecked(NewVariable.VarGuid);
		const FBPVariableDescription& OldVariable = LastNewVariables[OldVarIndex];
		if (OldVariable.VarName != NewVariable.VarName)
		{
			bFoundChange = true;
			OnVariableRenamed(NewVariable, OldVariable.VarName, NewVariable.VarName);
		}

		if (OldVariable.VarType != NewVariable.VarType)
		{
			bFoundChange = true;
			OnVariableTypeChanged(NewVariable, OldVariable.VarType, NewVariable.VarType);
		}

		if (OldVariable.PropertyFlags != NewVariable.PropertyFlags)
		{
			bFoundChange = true;
			OnVariablePropertyFlagsChanged(NewVariable, OldVariable.PropertyFlags, NewVariable.PropertyFlags);
		}
	}

	LastNewVariables = NewVariables;

	if (bFoundChange)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(this);
	}
}

void URenderPagesBlueprint::OnVariableAdded(FBPVariableDescription& InVar)
{
	MakeVariableTransientUnlessInstanceEditable(InVar);
}

void URenderPagesBlueprint::OnVariablePropertyFlagsChanged(FBPVariableDescription& InVar, const uint64 InOldVarPropertyFlags, const uint64 InNewVarPropertyFlags)
{
	if ((InOldVarPropertyFlags & CPF_DisableEditOnInstance) != (InNewVarPropertyFlags & CPF_DisableEditOnInstance))// if the value of [Instance Editable] changed
	{
		MakeVariableTransientUnlessInstanceEditable(InVar);
	}
}

void URenderPagesBlueprint::MakeVariableTransientUnlessInstanceEditable(FBPVariableDescription& InVar)
{
	if ((InVar.PropertyFlags & CPF_DisableEditOnInstance) == 0)// if [Instance Editable]
	{
		InVar.PropertyFlags &= ~CPF_Transient;// set [Transient] to false
	}
	else
	{
		InVar.PropertyFlags |= CPF_Transient;// set [Transient] to true
	}
}

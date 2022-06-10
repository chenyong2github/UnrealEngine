// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/RenderPagesGraphSchema.h"
#include "Graph/RenderPagesGraph.h"
#include "Graph/RenderPagesGraphNode.h"
#include "Blueprints/RenderPagesBlueprint.h"
#include "EdGraphSchema_K2_Actions.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"

#define LOCTEXT_NAMESPACE "RenderPagesGraphSchema"

/////////////////////////////////////////////////////
// UE::RenderPages::FRenderPagesLocalVariableNameValidator
UE::RenderPages::FRenderPagesLocalVariableNameValidator::FRenderPagesLocalVariableNameValidator(const UBlueprint* Blueprint, const URenderPagesGraph* Graph, FName InExistingName)
	: FStringSetNameValidator(InExistingName.ToString())
{
	if (Blueprint)
	{
		TSet<FName> NamesTemp;
		// We allow local variables with same name as blueprint variable

		FBlueprintEditorUtils::GetFunctionNameList(Blueprint, NamesTemp);
		FBlueprintEditorUtils::GetAllGraphNames(Blueprint, NamesTemp);
		FBlueprintEditorUtils::GetSCSVariableNameList(Blueprint, NamesTemp);
		FBlueprintEditorUtils::GetImplementingBlueprintsFunctionNameList(Blueprint, NamesTemp);

		for (FName& Name : NamesTemp)
		{
			Names.Add(Name.ToString());
		}
	}
}

/////////////////////////////////////////////////////
// UE::RenderPages::FRenderPagesNameValidator
UE::RenderPages::FRenderPagesNameValidator::FRenderPagesNameValidator(const UBlueprint* Blueprint, const UStruct* ValidationScope, FName InExistingName)
	: FStringSetNameValidator(InExistingName.ToString())
{
	if (Blueprint)
	{
		TSet<FName> NamesTemp;
		FBlueprintEditorUtils::GetClassVariableList(Blueprint, NamesTemp, true);
		FBlueprintEditorUtils::GetFunctionNameList(Blueprint, NamesTemp);
		FBlueprintEditorUtils::GetAllGraphNames(Blueprint, NamesTemp);
		FBlueprintEditorUtils::GetSCSVariableNameList(Blueprint, NamesTemp);
		FBlueprintEditorUtils::GetImplementingBlueprintsFunctionNameList(Blueprint, NamesTemp);

		for (FName& Name : NamesTemp)
		{
			Names.Add(Name.ToString());
		}
	}
}

/////////////////////////////////////////////////////
// FRenderPagesGraphSchemaAction_LocalVar
bool FRenderPagesGraphSchemaAction_LocalVar::IsValidName(const FName& NewName, FText& OutErrorMessage) const
{
	if (URenderPagesGraph* Graph = Cast<URenderPagesGraph>(GetVariableScope()))
	{
		UE::RenderPages::FRenderPagesLocalVariableNameValidator NameValidator(Graph->GetBlueprint(), Graph, GetVariableName());
		EValidatorResult Result = NameValidator.IsValid(NewName.ToString(), false);
		if (Result != EValidatorResult::Ok && Result != EValidatorResult::ExistingName)
		{
			OutErrorMessage = FText::FromString(TEXT("Name with invalid format"));
			return false;
		}
	}
	return FEdGraphSchemaAction_BlueprintVariableBase::IsValidName(NewName, OutErrorMessage);
}

/////////////////////////////////////////////////////
// FRenderPagesGraphSchemaAction_PromoteToVariable
FRenderPagesGraphSchemaAction_PromoteToVariable::FRenderPagesGraphSchemaAction_PromoteToVariable(UEdGraphPin* InEdGraphPin, bool InLocalVariable)
	: FEdGraphSchemaAction(FText(),
		InLocalVariable ? LOCTEXT("PromoteToLocalVariable", "Promote to local variable") : LOCTEXT("PromoteToVariable", "Promote to variable"),
		InLocalVariable ? LOCTEXT("PromoteToLocalVariable", "Promote to local variable") : LOCTEXT("PromoteToVariable", "Promote to variable"),
		1)
	, EdGraphPin(InEdGraphPin)
	, bLocalVariable(InLocalVariable)
{}

UEdGraphNode* FRenderPagesGraphSchemaAction_PromoteToVariable::PerformAction(UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	return nullptr;
}

/////////////////////////////////////////////////////
// UE::RenderPages::FRenderPagesFunctionDragDropAction
UE::RenderPages::FRenderPagesFunctionDragDropAction::FRenderPagesFunctionDragDropAction()
	: FGraphSchemaActionDragDropAction()
	, SourceRenderPagesBlueprint(nullptr)
	, SourceRenderPagesGraph(nullptr)
	, bControlDrag(false)
	, bAltDrag(false)
{}

TSharedRef<UE::RenderPages::FRenderPagesFunctionDragDropAction> UE::RenderPages::FRenderPagesFunctionDragDropAction::New(TSharedPtr<FEdGraphSchemaAction> InAction, URenderPagesBlueprint* InRenderPagesBlueprint, URenderPagesGraph* InRenderPagesGraph)
{
	TSharedRef<FRenderPagesFunctionDragDropAction> Action = MakeShareable(new FRenderPagesFunctionDragDropAction);
	Action->SourceAction = InAction;
	Action->SourceRenderPagesBlueprint = InRenderPagesBlueprint;
	Action->SourceRenderPagesGraph = InRenderPagesGraph;
	Action->Construct();
	return Action;
}

/////////////////////////////////////////////////////
// URenderPagesGraphSchema

const FName URenderPagesGraphSchema::GraphName_RenderPages(TEXT("Logic Graph"));

URenderPagesGraphSchema::URenderPagesGraphSchema() {}

void URenderPagesGraphSchema::GetGraphDisplayInformation(const UEdGraph& Graph, FGraphDisplayInfo& DisplayInfo) const
{
	Super::GetGraphDisplayInformation(Graph, DisplayInfo);

	if (Cast<URenderPagesGraph>(const_cast<UEdGraph*>(&Graph)))
	{
		static const FText MainGraphText = FText::FromString(TEXT("The logic graph for the Render Pages."));
		DisplayInfo.Tooltip = MainGraphText;
	}
}

FLinearColor URenderPagesGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	return GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(PinType);
}

bool URenderPagesGraphSchema::CanGraphBeDropped(TSharedPtr<FEdGraphSchemaAction> InAction) const
{
	if (!InAction.IsValid())
	{
		return false;
	}

	if (InAction->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Graph* FuncAction = static_cast<FEdGraphSchemaAction_K2Graph*>(InAction.Get());
		if (Cast<URenderPagesGraph>(FuncAction->EdGraph))
		{
			return true;
		}
	}
	else if (InAction->GetTypeId() == FRenderPagesGraphSchemaAction_LocalVar::StaticGetTypeId())
	{
		FRenderPagesGraphSchemaAction_LocalVar* VarAction = static_cast<FRenderPagesGraphSchemaAction_LocalVar*>(InAction.Get());
		if (Cast<URenderPagesGraph>(VarAction->GetVariableScope()))
		{
			return true;
		}
	}

	return false;
}

FReply URenderPagesGraphSchema::BeginGraphDragAction(TSharedPtr<FEdGraphSchemaAction> InAction, const FPointerEvent& MouseEvent) const
{
	if (!InAction.IsValid())
	{
		return FReply::Unhandled();
	}

	if (InAction->GetTypeId() == FEdGraphSchemaAction_K2Graph::StaticGetTypeId())
	{
		FEdGraphSchemaAction_K2Graph* FuncAction = static_cast<FEdGraphSchemaAction_K2Graph*>(InAction.Get());
		if (URenderPagesGraph* RenderPagesGraph = Cast<URenderPagesGraph>(FuncAction->EdGraph))
		{
			if (URenderPagesBlueprint* RenderPagesBlueprint = Cast<URenderPagesBlueprint>(FBlueprintEditorUtils::FindBlueprintForGraph(RenderPagesGraph)))
			{
				TSharedRef<UE::RenderPages::FRenderPagesFunctionDragDropAction> Action = UE::RenderPages::FRenderPagesFunctionDragDropAction::New(InAction, RenderPagesBlueprint, RenderPagesGraph);
				Action->SetAltDrag(MouseEvent.IsAltDown());
				Action->SetCtrlDrag(MouseEvent.IsControlDown());
				return FReply::Handled().BeginDragDrop(Action);
			}
		}
	}
	else if (InAction->GetTypeId() == FRenderPagesGraphSchemaAction_LocalVar::StaticGetTypeId())
	{
		FRenderPagesGraphSchemaAction_LocalVar* VarAction = static_cast<FRenderPagesGraphSchemaAction_LocalVar*>(InAction.Get());
		if (URenderPagesGraph* RenderPagesGraph = Cast<URenderPagesGraph>(VarAction->GetVariableScope()))
		{
			if (URenderPagesBlueprint* RenderPagesBlueprint = Cast<URenderPagesBlueprint>(FBlueprintEditorUtils::FindBlueprintForGraph(RenderPagesGraph)))
			{
				TSharedRef<UE::RenderPages::FRenderPagesFunctionDragDropAction> Action = UE::RenderPages::FRenderPagesFunctionDragDropAction::New(InAction, RenderPagesBlueprint, RenderPagesGraph);
				Action->SetAltDrag(MouseEvent.IsAltDown());
				Action->SetCtrlDrag(MouseEvent.IsControlDown());
				return FReply::Handled().BeginDragDrop(Action);
			}
		}
	}
	return FReply::Unhandled();
}

bool URenderPagesGraphSchema::CanVariableBeDropped(UEdGraph* InGraph, FProperty* InVariableToDrop) const
{
	return true;
}

bool URenderPagesGraphSchema::RequestVariableDropOnPanel(UEdGraph* InGraph, FProperty* InVariableToDrop, const FVector2D& InDropPosition, const FVector2D& InScreenPosition)
{
	if (CanVariableBeDropped(InGraph, InVariableToDrop))
	{
		UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(InGraph);
		if (Cast<URenderPagesBlueprint>(Blueprint))
		{
			return true;
		}
	}
	return false;
}

bool URenderPagesGraphSchema::RequestVariableDropOnPin(UEdGraph* InGraph, FProperty* InVariableToDrop, UEdGraphPin* InPin, const FVector2D& InDropPosition, const FVector2D& InScreenPosition)
{
	if (CanVariableBeDropped(InGraph, InVariableToDrop))
	{
		if (Cast<URenderPagesGraph>(InGraph))
		{
			return true;
		}
	}
	return false;
}

void URenderPagesGraphSchema::InsertAdditionalActions(TArray<UBlueprint*> InBlueprints, TArray<UEdGraph*> InGraphs, TArray<UEdGraphPin*> InPins, FGraphActionListBuilderBase& OutAllActions) const
{
	Super::InsertAdditionalActions(InBlueprints, InGraphs, InPins, OutAllActions);

	if (InPins.Num() > 0)
	{
		if (URenderPagesGraphNode* RenderPagesNode = Cast<URenderPagesGraphNode>(InPins[0]->GetOwningNode()))
		{
			if (UEdGraphPin* ModelPin = RenderPagesNode->GetPinAt(0))
			{
				OutAllActions.AddAction(MakeShared<FRenderPagesGraphSchemaAction_PromoteToVariable>(ModelPin, false));
			}
		}
	}
}

TSharedPtr<INameValidatorInterface> URenderPagesGraphSchema::GetNameValidator(const UBlueprint* BlueprintObj, const FName& OriginalName, const UStruct* ValidationScope, const FName& ActionTypeId) const
{
	if (ActionTypeId == FRenderPagesGraphSchemaAction_LocalVar::StaticGetTypeId())
	{
		if (const URenderPagesGraph* RenderPagesGraph = Cast<URenderPagesGraph>(ValidationScope))
		{
			return MakeShareable(new UE::RenderPages::FRenderPagesLocalVariableNameValidator(BlueprintObj, RenderPagesGraph, OriginalName));
		}
	}
	return MakeShareable(new UE::RenderPages::FRenderPagesNameValidator(BlueprintObj, ValidationScope, OriginalName));
}

#undef LOCTEXT_NAMESPACE

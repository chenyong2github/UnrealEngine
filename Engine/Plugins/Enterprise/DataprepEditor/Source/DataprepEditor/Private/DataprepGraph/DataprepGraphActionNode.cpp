// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepGraph/DataprepGraphActionNode.h"

// Dataprep includes
#include "DataprepAsset.h"
#include "DataprepActionAsset.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphEditor.h"

// Engine includes
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "K2Node_CallFunction.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "KismetCompiler.h"
#include "Misc/AssertionMacros.h"
#include "UObject/Object.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#define LOCTEXT_NAMESPACE "DataprepGraphActionNode"

UDataprepGraphActionNode::UDataprepGraphActionNode()
	: DataprepActionAsset(nullptr)
	, ExecutionOrder(INDEX_NONE)
{
	bCanRenameNode = true;
	ActionTitle = LOCTEXT("DefaultNodeTitle", "New Action").ToString();
}

void UDataprepGraphActionNode::Initialize(UDataprepActionAsset* InDataprepActionAsset, int32 InExecutionOrder)
{
	if(InDataprepActionAsset)
	{
		DataprepActionAsset = InDataprepActionAsset;
		ActionTitle = DataprepActionAsset->GetLabel();
		ExecutionOrder = InExecutionOrder;
	}
	else
	{
		ensure(false);
	}
}

FLinearColor UDataprepGraphActionNode::GetNodeTitleColor() const
{
	return FLinearColor(0.0036765f, 0.3864294f, 0.2501584);
}

FText UDataprepGraphActionNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::FromString( ActionTitle );
}

void UDataprepGraphActionNode::OnRenameNode(const FString& NewName)
{
	ActionTitle = NewName;
	if ( DataprepActionAsset )
	{
		DataprepActionAsset->SetLabel( *NewName );
	}
}

void UDataprepGraphActionNode::DestroyNode()
{
	if ( DataprepActionAsset )
	{
		Modify();

		DataprepActionAsset->NotifyDataprepSystemsOfRemoval();

		// Force the transaction system to restore the action
		DataprepActionAsset = nullptr;
	}
 
	Super::DestroyNode();
}

TSharedPtr<class INameValidatorInterface> UDataprepGraphActionNode::MakeNameValidator() const
{
	// The name doesn't matter
	return MakeShareable(new FDummyNameValidator(EValidatorResult::Ok));
}

UDataprepGraphActionStepNode::UDataprepGraphActionStepNode()
	: DataprepActionAsset(nullptr)
	, StepIndex(INDEX_NONE)
{
	bCanRenameNode = false;
}

const UDataprepActionStep* UDataprepGraphActionStepNode::GetDataprepActionStep() const
{
	return DataprepActionAsset ? DataprepActionAsset->GetStep(StepIndex).Get() : nullptr;
}

UDataprepActionStep* UDataprepGraphActionStepNode::GetDataprepActionStep()
{
	return DataprepActionAsset ? DataprepActionAsset->GetStep(StepIndex).Get() : nullptr;
}

FLinearColor UDataprepGraphActionStepNode::GetNodeTitleColor() const
{
	return FLinearColor(0.0036765f, 0.3864294f, 0.2501584);
}

FText UDataprepGraphActionStepNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	static FText DummyTitle;
	return DummyTitle;
}

void UDataprepGraphActionStepNode::DestroyNode()
{
	if ( DataprepActionAsset )
	{
		Modify();

		//DataprepActionAsset->NotifyDataprepSystemsOfRemoval();

		// Force the transaction system to restore the action
		DataprepActionAsset = nullptr;
	}

	Super::DestroyNode();
}

#undef LOCTEXT_NAMESPACE

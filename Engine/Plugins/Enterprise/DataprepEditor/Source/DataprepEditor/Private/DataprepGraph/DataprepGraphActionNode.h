// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node.h"
#include "Math/Color.h"

#include "DataprepGraphActionNode.generated.h"


class FBlueprintActionDatabaseRegistrar;
class UDataprepActionAsset;
class UDataprepActionStep;

/**
 * The UDataprepGraphActionStepNode class is used as the UEdGraphNode associated
 * to an SGraphNode in order to display the action's steps in a SDataprepGraphEditor.
 */
UCLASS(MinimalAPI)
class UDataprepGraphActionStepNode : public UEdGraphNode
{
	GENERATED_BODY()

public:
	UDataprepGraphActionStepNode();

	// Begin EdGraphNode interface
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool ShowPaletteIconOnNode() const override { return false; }
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void DestroyNode() override;
	// End EdGraphNode interface


	void Initialize(UDataprepActionAsset* InDataprepActionAsset, int32 InStepIndex)
	{
		DataprepActionAsset = InDataprepActionAsset;
		StepIndex = InStepIndex;
	}

	const UDataprepActionAsset* GetDataprepActionAsset() const { return DataprepActionAsset; }
	UDataprepActionAsset* GetDataprepActionAsset() { return DataprepActionAsset; }

	int32 GetStepIndex() const { return StepIndex; }

	const UDataprepActionStep* GetDataprepActionStep() const;
	UDataprepActionStep* GetDataprepActionStep();

protected:
	UPROPERTY()
	UDataprepActionAsset* DataprepActionAsset;

	UPROPERTY()
	int32 StepIndex;

	// Is this node currently driving the filter preview
	bool bIsPreviewed = false;
};

/**
 * The UDataprepGraphActionNode class is used as the UEdGraphNode associated
 * to an SGraphNode in order to display actions in a SDataprepGraphEditor.
 */
UCLASS(MinimalAPI)
class UDataprepGraphActionNode final : public UEdGraphNode
{
	GENERATED_BODY()

public:
	UDataprepGraphActionNode();

	// Begin EdGraphNode interface
	virtual FLinearColor GetNodeTitleColor() const override;
	virtual bool ShowPaletteIconOnNode() const override { return false; }
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void OnRenameNode(const FString& NewName) override;
	virtual void DestroyNode() override;
	TSharedPtr<class INameValidatorInterface> MakeNameValidator() const override;
	// End EdGraphNode interface

	void Initialize(UDataprepActionAsset* InDataprepActionAsset, int32 InExecutionOrder);

	const UDataprepActionAsset* GetDataprepActionAsset() const { return DataprepActionAsset; }
	UDataprepActionAsset* GetDataprepActionAsset() { return DataprepActionAsset; }

	int32 GetExecutionOrder() const { return ExecutionOrder; }

protected:
	UPROPERTY()
	FString ActionTitle;

	UPROPERTY()
	UDataprepActionAsset* DataprepActionAsset;

	UPROPERTY()
	int32 ExecutionOrder;
};

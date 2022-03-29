// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_BaseAsyncTask.h"

#include "K2Node_WebAPIOperation.generated.h"

class FBlueprintActionDatabaseRegistrar;

UENUM()
enum class EWebAPIOperationAsyncType : uint8
{
	LatentAction = 0,
	Callback = 1
};

/**
 * 
 */
UCLASS()
class WEBAPIBLUEPRINTGRAPH_API UK2Node_WebAPIOperation
	: public UK2Node_BaseAsyncTask
{
	GENERATED_BODY()

public:
	UK2Node_WebAPIOperation();

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void GetNodeContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual void PostPlacedNewNode() override;
	virtual void PostPasteNode() override;
	virtual bool CanPasteHere(const UEdGraph* TargetGraph) const override;
	//~ End UEdGraphNode Interface.

	// UK2Node interface
	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;
	virtual void PostReconstructNode() override;
	// End of UK2Node interface

	void SetAsyncType(EWebAPIOperationAsyncType InAsyncType);

	bool IsValid() const;

private:
	UEdGraphPin* FindPinContainingName(const EEdGraphPinDirection& InPinDirection, const FString& InName) const;
	TArray<UEdGraphPin*> FindPinsContainingName(const EEdGraphPinDirection& InPinDirection, const FString& InName) const;
	
	TArray<UEdGraphPin*> GetRequestPins() const;
	TArray<UEdGraphPin*> GetResponsePins() const;
	TArray<UEdGraphPin*> GetErrorResponsePins() const;

	void SplitRequestPins(const TArray<UEdGraphPin*>& InPins) const;	
	void SplitResponsePins(const TArray<UEdGraphPin*>& InPins) const;

	static void CleanupPinNameInline(FString& InPinName);

	/** Flips the node's Async Type. */
	void ToggleAsyncType();

	/** Update exec pins when converting between LatentAction and Callback Async Types. */
	bool ReconnectExecPins(TArray<UEdGraphPin*>& OldPins);

	/** Latent Action is preferred, but not compatible when used in a function, so allow conversion between the two. */
	UPROPERTY()
	EWebAPIOperationAsyncType OperationAsyncType = EWebAPIOperationAsyncType::LatentAction;
};

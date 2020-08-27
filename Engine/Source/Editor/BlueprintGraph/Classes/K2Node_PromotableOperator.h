// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_PromotableOperator.generated.h"

/** The promotable operator node allows for pin types to be promoted to others, i.e. float to double */
UCLASS(MinimalAPI)
class UK2Node_PromotableOperator : public UK2Node_CallFunction
{
	GENERATED_UCLASS_BODY()

public:

	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
	virtual FText GetTooltipText() const override;
	// End of UEdGraphNode interface

	// UK2Node interface
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void NotifyPinConnectionListChanged(UEdGraphPin* ChangedPin) override;
	virtual void PostReconstructNode() override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual bool IsActionFilteredOut(class FBlueprintActionFilter const& Filter) override;
	// End of UK2Node interface

private:

	/** Helper function to recombine all split pins that this node may have */
	void RecombineAllSplitPins();

	/** 
	* @return	True if this node has any connections attached to it, 
	*			or the default values have been modified by the user
	*/
	bool HasAnyConnectionsOrDefaults() const;

	/** Recombines all split pins and sets the node to have default values (all wildcard pins) */
	void ResetNodeToWildcard();

	/**
	* Attempts to create a cast node and connect it to a call function node
	* 
	* @return	True if the intermediate connection was made successfully	
	*/
	bool CreateIntermediateCast(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InputPin, UEdGraphPin* OutputPin);

	/** Helper to make sure we have the most up to date operation name. Returns true upon success */
	bool UpdateOpName();

	/** 
	* Update the pins on this node based on the given function. This modifies pins, meant 
	* for use by PinConnectionListChanged, not during node construction. 
	*/
	void UpdatePinsFromFunction(const UFunction* Function, UEdGraphPin* ChangedPin = nullptr);

	/** Called when the user attempts conversion from the context menu. Records a transaction and calls UpdatePinsFromFunction */
	void ConvertNodeToFunction(const UFunction* Function, UEdGraphPin* ChangedPin);

	/** Updates the PossibleConversions function array based on the current pin types */
	void UpdatePossibleConversionFuncs();

	/**
	* Returns all pins that have the EGPD_Input direction
	* @param bIncludeLinks	If true, than this will also include all the pins that are linked to the inputs.
	*						This is useful for gathering what the highest type may be
	*/
	TArray<UEdGraphPin*> GetInputPins(bool bIncludeLinks = false) const;

	/** The name that this operation uses ("Add", "Multiply", etc) */
	FString OperationName;

	/** Array of functions that we could possibly convert this node to via the right-click context menu */
	UPROPERTY(Transient)
	TArray<UFunction*> PossibleConversions;

public:

	/** Returns the first pin with the EGPD_Output direction */
	UEdGraphPin* GetOutputPin() const;

	const FString& GetOperationName() const { return OperationName; }
};

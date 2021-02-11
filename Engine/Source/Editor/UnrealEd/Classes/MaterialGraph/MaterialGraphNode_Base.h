// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EdGraph/EdGraphNode.h"
#include "MaterialGraphNode_Base.generated.h"

class UEdGraphPin;
class UEdGraphSchema;

enum class EMaterialGraphPinType
{
	Data,
	Exec,
};

struct FMaterialGraphPinInfo
{
	EMaterialGraphPinType PinType;
	int32 Index; // index into the expression's list of inputs/outputs (exec inputs/outpus are indexed separately)
};

UCLASS(MinimalAPI)
class UMaterialGraphNode_Base : public UEdGraphNode
{
	GENERATED_UCLASS_BODY()

	/** Contains additional information about material graph pins, avoid adding material-specific data to base pin type */
	TMap<UEdGraphPin*, FMaterialGraphPinInfo> PinInfoMap;

	/** Lists of pins that match up with the underlying UMaterialExpression's (non-exec) inputs and outputs */
	TArray<UEdGraphPin*> InputPins;
	TArray<UEdGraphPin*> OutputPins;

	/** The exec input pin */
	UEdGraphPin* ExecInputPin = nullptr;

	/** Create all of the input pins required */
	virtual void CreateInputPins() {};
	/** Create all of the output pins required */
	virtual void CreateOutputPins() {};
	/** Is this the undeletable root node */
	virtual bool IsRootNode() const {return false;}
	/** Get a single Input Pin via its index */
	class UEdGraphPin* GetInputPin(int32 InputIndex) const { return InputPins[InputIndex]; }
	/** Get a single Output Pin via its index */
	class UEdGraphPin* GetOutputPin(int32 OutputIndex) const { return OutputPins[OutputIndex]; }
	/** Gets the exec input pin */
	class UEdGraphPin* GetExecInputPin() const { return ExecInputPin; }
	/** Replace a given node with this one, changing all pin links */
	UNREALED_API void ReplaceNode(UMaterialGraphNode_Base* OldNode);

	UNREALED_API const FMaterialGraphPinInfo& GetPinInfo(const class UEdGraphPin* Pin) const;

	/** Get the Material value type of an input pin */
	uint32 GetInputType(const UEdGraphPin* InputPin) const;

	/** Get the Material value type of an output pin */
	uint32 GetOutputType(const UEdGraphPin* OutputPin) const;

	/**
	 * Handles inserting the node between the FromPin and what the FromPin was original connected to
	 *
	 * @param FromPin			The pin this node is being spawned from
	 * @param NewLinkPin		The new pin the FromPin will connect to
	 * @param OutNodeList		Any nodes that are modified will get added to this list for notification purposes
	 */
	void InsertNewNode(UEdGraphPin* FromPin, UEdGraphPin* NewLinkPin, TSet<UEdGraphNode*>& OutNodeList);

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual void ReconstructNode() override;
	virtual void RemovePinAt(const int32 PinIndex, const EEdGraphPinDirection PinDirection) override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const override;
	virtual FString GetDocumentationLink() const override;
	//~ End UEdGraphNode Interface.

protected:
	void ModifyAndCopyPersistentPinData(UEdGraphPin& TargetPin, const UEdGraphPin& SourcePin) const;

	void RegisterPin(UEdGraphPin* Pin, EMaterialGraphPinType Type, int32 Index);

	virtual uint32 GetPinMaterialType(const UEdGraphPin* Pin, const FMaterialGraphPinInfo& PinInfo) const;

	void EmptyPins();
};

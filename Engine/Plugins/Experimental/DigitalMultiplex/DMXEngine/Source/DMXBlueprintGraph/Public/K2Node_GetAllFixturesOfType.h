// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "K2Node_DMXBase.h"
#include "K2Node_GetAllFixturesOfType.generated.h"

struct FK2Node_GetAllFixturesOfType
{
	static FName FixtureTypePinName;
	static FName OutResultPinName;
};

/**   */
UCLASS()
class DMXBLUEPRINTGRAPH_API UK2Node_GetAllFixturesOfType : public UK2Node_DMXBase
{
	GENERATED_BODY()
	
	UK2Node_GetAllFixturesOfType();

	//~ Begin UK2Node implementation
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	//~ End UK2Node implementation

	virtual bool IsNodeSafeToIgnore() const override { return true; }
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;

};
